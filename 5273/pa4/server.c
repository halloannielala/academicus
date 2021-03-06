/*
 * server.c
 *
 	Ari Summer
 	Programming Assignment 3
 	11/16/2012
	TCP Client
 */

#include <stdio.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <pthread.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/time.h>
#include <stdlib.h>
#include <netdb.h>
#include <signal.h>

#include "hw4lib.h"

#include "openssl/bio.h"
#include "openssl/ssl.h"
#include "openssl/err.h"


#define MAX_CONNECTS 50
#define MAXBUFSIZE (24*1024)
#define OFFSET1 20
#define NAMESIZE 20
#define OFFSET2 200
#define FILELISTSIZE 4048
#define LISTSIZE 2048
#define CLIENTLISTHDR "\nFile name\t\t||\tFILE SIZE B\t\t||\tFile owner\n\n"
#define CMDSIZE 100
#define EXITCMD "exit"
#define LISTCMD "list"
#define SENDFILES "sendmyfileslist"
#define GET "get"
#define IP "ip"
#define DELM " "
#define COMMA ","
#define NAME "name"
#define MSG "msg"


//Function Declarations
void connection(int sockid);
void processNewConnection();
void build_sock_set();
void getCommand(char buffer[MAXBUFSIZE], int sockid);
int findIndex(int sockid);
void sendList(int index);
void manageGet(char fname[CMDSIZE], int sockid);
void printList();

//global variables
pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
char logFileName[64];

typedef struct{
	int sock;
	double t;
	int valid;
	char address[INET6_ADDRSTRLEN];
	char name[512];
	char files[LISTSIZE];
}clientInfo;

clientInfo clientArray[4];
int sockfd;
struct sockaddr_storage their_addr; // connector's address information
fd_set readfds;

int main(int argc,char *argv[])
{

	struct timeval currTime;
	//pthread_t th;
	int i;
	//int r;
	int selRet;
	//FILE *logFile;
	struct addrinfo hints, *servinfo, *p;
	char portnum[50];

	char s[INET6_ADDRSTRLEN];
	int yes=1;
	int rv;

	//check arguments here
	if (argc != 2)  {
		printf("usage is: ./pserver <port#>\n");
		return 0;
	}

	strcpy(portnum,argv[1]);


	//==================================================================================
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE; // use my IP


	if ((rv = getaddrinfo(NULL, portnum, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}

	//==================================================================================


	gettimeofday(&currTime,NULL);

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("server: socket");
			continue;
		}

		//Set socket options
		if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes,
				sizeof(int)) == -1) {
			perror("setsockopt");
			exit(1);
		}

		/*
		* Bind our local address so that the client can send to us.
		*/
		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("server: bind");
			continue;
		}

		break;
	}

	if (p == NULL)  {
		fprintf(stderr, "server: failed to bind\n");
		return 2;
	}

	//Get server IP
	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),s, sizeof s);
	printf("server IP: %s\n", s);

	freeaddrinfo(servinfo); // all done with this structure

	//Listen on socket
	if (listen(sockfd, MAX_CONNECTS) == -1) {
	        perror("listen");
	        exit(1);
	}

	bzero(&clientArray,sizeof(clientArray));

	printf("server: waiting for connections...\n");


	while(1) //endless loop
	{

		//Build the socket set before calling select
		build_sock_set();

		//Wait for communication on main socket
		selRet = select(FD_SETSIZE, &readfds, NULL, NULL, NULL);


		if ((selRet != -1) && (selRet != 0)) {

			//If communication is on main socket 
			//process the new connection
			if(FD_ISSET(sockfd,&readfds)){

				processNewConnection();

			}

			//Loop through set and see if there are messages to be handled
			for(i = 0; i < 4; i++){
				if(FD_ISSET(clientArray[i].sock,&readfds) && clientArray[i].valid == 1){

					connection(clientArray[i].sock);

				}

			}

		}

		else if (selRet == 0) {
			printf("timeout\n");
		}

		else if (selRet == -1) {
			perror("select()");
			printf("select() failed : \n");
		}


	}

	close(sockfd);
}


//Handle communications
void connection(int sockid) {
	int s = sockid;

	char buffer[MAXBUFSIZE];
	struct timeval;
	char command[CMDSIZE];
	int rc = 0;


	bzero(buffer, sizeof(buffer));
	bzero(command, sizeof(command));

	/*
		Get message and call getCommand method
	*/
	rc = recv(s, buffer, MAXBUFSIZE, MSG_WAITALL);
	if (rc > 0)
	{

		getCommand(buffer, sockid);

	}

	//If client has disconnected, remove it from list
	else if(rc == 0){
		//Client has disconnected
		int k;

		int j = findIndex(s);
		if (j != -1){clientArray[j].valid = 0;}
		printf("Client %s Closed Socket Connection!\n",clientArray[j].name);
		close(sockid);

		for(k = 0; k < 4; k++){
			if(clientArray[k].valid == 1){
				sendList(k);
			}
		}

	}
}

/*
	If a new client has connected add them to the list and 
	give them a socket id

*/
void processNewConnection(){
	socklen_t sin_size;
	int new_fd;
	struct timeval currentTime;
	//int r;
	char ip[INET6_ADDRSTRLEN];
	int i;
	int room = 0;

	sin_size = sizeof their_addr;
	new_fd = accept(sockfd, (struct sockaddr *)&their_addr, &sin_size);
	if (new_fd == -1) {
		perror("accept");
	}

	inet_ntop(their_addr.ss_family,get_in_addr((struct sockaddr *)&their_addr),ip, sizeof ip);
	printf("server: got connection from %s\n", ip);

	//Add client to list if there is room
	for(i = 0; (i < 4) && (room != 1); i++){
		if(clientArray[i].valid == 0){


			bzero(&clientArray[i],sizeof(clientArray[i]));

			gettimeofday(&currentTime,NULL);
			double time=currentTime.tv_sec+(currentTime.tv_usec/1000000.0);


			clientArray[i].valid = 1;
			clientArray[i].sock = new_fd;
			clientArray[i].t = time;
			strcpy(clientArray[i].address,ip);
			room = 1;
		}

	}

	//If there is no room send them a message and close socket
	if(room != 1){
		//SEND message to client to exit cause no room

		char sendbuffer[MAXBUFSIZE];
		char nofilemsg[] = "\nThere server is full right now. Try again later.\n";
		bzero(&sendbuffer,sizeof(sendbuffer));

		memcpy(sendbuffer, MSG, sizeof(MSG));
		memcpy(&sendbuffer[OFFSET1], nofilemsg, sizeof(nofilemsg));

		int s;

		s = send(new_fd, sendbuffer, sizeof(sendbuffer), 0);

		if( s == -1){
			printf("Sending error!\n");
			perror("send()");
		}

		printf("Client tried to connect but no room\n");
		close(new_fd);
	}

}

//Build socket set using main socket and sockets
//in the clientArray
void build_sock_set() {
	int listnum;	     /* Current item in connectlist for for loops */


	FD_ZERO(&readfds);


	FD_SET(sockfd,&readfds);

	/* Loops through all the possible connections and adds
		those sockets to the fd_set */
	for (listnum = 0; listnum < 4; listnum++) {
		if (clientArray[listnum].valid != 0) {
			FD_SET(clientArray[listnum].sock,&readfds);

		}
	}
}

//Process commands from clients
void getCommand(char buffer[MAXBUFSIZE], int sockid){
	char command[CMDSIZE];
	int index;

	bzero(&command,sizeof(command));
	index = findIndex(sockid);

	strncpy(command, buffer, OFFSET1);

	//Store name of client
	if(!strcmp(command, NAME)){
		char clientname[NAMESIZE];
		bzero(clientname, sizeof(clientname));
		memcpy(&clientname[0], &buffer[OFFSET1], sizeof(clientname));
		printf("<<Client %s has connected to the server.>>\n", clientname);

		strcpy(clientArray[index].name,clientname);

	}

	//Store list of files in clientArray for client
	else if(!strcmp(command, SENDFILES)){
		char flist[LISTSIZE];
		int k;

		bzero(flist, sizeof(flist));
		bzero(clientArray[index].files, sizeof(clientArray[index].files));

		memcpy(&flist[0], &buffer[OFFSET1], sizeof(flist));


		strcpy(clientArray[index].files ,flist);

		printList();

		for(k = 0; k < 4; k++){

			if(clientArray[k].valid == 1){
				sendList(k);
			}
		}

	}

	//remove client from list
	else if(!strcmp(command, EXITCMD)){
		clientArray[index].valid = 0;
		close(clientArray[index].sock);
	}

	//Send list of files on server to client
	else if(!strcmp(command, LISTCMD)){
		sendList(index);
	}

	//Send filename to client that has a file and ip to client
	//that wants file
	else if(!strcmp(command,GET)){
		char fname[CMDSIZE];
		bzero(&fname, sizeof(fname));

		memcpy(&fname[0], &buffer[OFFSET1], sizeof(fname));
		manageGet(fname, index);
	}

	//Invalid command
	else{

		printf("Received invalid command.\n");
	}

}

//Find index into clientArray of client with socket sockid
int findIndex(int sockid){
	int i;
	int found = 0;

	for(i = 0; i < 4; i++){
		if((clientArray[i].sock == sockid) && clientArray[i].valid == 1){
			found = 1;
			return i;
		}
	}


	if(found == 0){
		printf("Error finding Client in Server Array!!\n");
		return -1;

	}

	return -1;

}

//Send list of files to client with index in clientArray
void sendList(int index){
	char sndbuffer[MAXBUFSIZE];
	char flisttemp[FILELISTSIZE];
	char filelist[FILELISTSIZE];
	int i;
	char* p;

	bzero(&filelist,sizeof(filelist));
	strcat(filelist, CLIENTLISTHDR);

	for(i = 0; i < 4; i++){
		if(clientArray[i].valid == 1){
			bzero(&flisttemp,sizeof(flisttemp));

			memcpy(flisttemp, clientArray[i].files, sizeof(clientArray[i].files));

			int flag = 0;
			p = strtok(flisttemp,COMMA);
			strcat(filelist,p);

			//Create formatted table of files
			while(p != NULL){

				if(flag == 1){
					strcat(filelist,p);
				}


				strcat(filelist,"\t\t||\t");
				p = strtok(NULL,COMMA);
				strcat(filelist,p);
				strcat(filelist,"\t\t||\t");
				strcat(filelist, clientArray[i].name);
				strcat(filelist, "\n");

				p = strtok(NULL,COMMA);

				flag = 1;

			}
		}

	}

	memcpy(&sndbuffer[0], LISTCMD, sizeof(LISTCMD));
	memcpy(&sndbuffer[OFFSET1], filelist, sizeof(filelist));

	int s;

	s = send(clientArray[index].sock, sndbuffer, sizeof(sndbuffer), 0);
	if( s == -1){
		printf("Sending error!\n");
		perror("send()");
	}


}

//Print list of files on server side.
//This list adds the ip address of each client

void printList(){
	char flisttemp[FILELISTSIZE];
	char filelist[FILELISTSIZE];
	int i;
	char* p;

	bzero(&filelist,sizeof(filelist));
	strcat(filelist, CLIENTLISTHDR);

	for(i = 0; i < 4; i++){
		if(clientArray[i].valid == 1){
			bzero(&flisttemp,sizeof(flisttemp));

			memcpy(flisttemp, clientArray[i].files, sizeof(clientArray[i].files));

			int flag = 0;
			p = strtok(flisttemp,COMMA);
			strcat(filelist,p);
			while(p != NULL){

				if(flag == 1){
					strcat(filelist,p);
				}


				strcat(filelist,"\t\t||\t");
				p = strtok(NULL,COMMA);
				strcat(filelist,p);
				strcat(filelist,"\t\t||\t");
				strcat(filelist, clientArray[i].name);
				strcat(filelist,"\t\t||\t");
				strcat(filelist, clientArray[i].address);
				strcat(filelist, "\n");

				p = strtok(NULL,COMMA);

				flag = 1;

			}
		}

	}

	printf("\n%s\n",filelist);

}

//Manages the getting and sending of files between clients
void manageGet(char fname[CMDSIZE], int index){
	int k;
	char* p;
	char flisttemp[FILELISTSIZE];

	int fowner = -1;
	int found = 0;


	//Fine client that owns file requested
	for(k = 0; k < 4 && found != 1; k++){
		if(clientArray[k].valid == 1){
			bzero(&flisttemp,sizeof(flisttemp));
			memcpy(flisttemp, clientArray[k].files, sizeof(clientArray[k].files));

			p = strtok(flisttemp,COMMA);
			while(p != NULL){
				if(!strcmp(p,fname)){
					fowner = k;
					found = 1;
					break;
				}
				p = strtok(NULL,COMMA);
				p = strtok(NULL,COMMA);
			}
		}
	}

	//If there is a client who owns the file send them the file name they 
	//should get ready to send
	if(fowner != -1){
		char ownersndbuffer[MAXBUFSIZE];
		char recvsndbuffer[MAXBUFSIZE];
		printf("Owner of file: %s\n: ", clientArray[fowner].name);

		if(!strcmp(clientArray[fowner].name, clientArray[index].name)){
			char sendbuffer[MAXBUFSIZE];
			char nofilemsg[] = "\nYou already own the requested file.\n";
			bzero(&sendbuffer,sizeof(sendbuffer));

			memcpy(sendbuffer, MSG, sizeof(MSG));
			memcpy(&sendbuffer[OFFSET1], nofilemsg, sizeof(nofilemsg));

			int s;

			s = send(clientArray[index].sock, sendbuffer, sizeof(sendbuffer), 0);

			if( s == -1){
				printf("Sending error!\n");
				perror("send()");
			}

			return;
		}

		bzero(&ownersndbuffer,sizeof(ownersndbuffer));
		bzero(&recvsndbuffer, sizeof(recvsndbuffer));

		int s;
		int r;
		memcpy(&ownersndbuffer[0], GET,sizeof(GET));
		memcpy(&ownersndbuffer[OFFSET1], fname, CMDSIZE);

		s = send(clientArray[fowner].sock, ownersndbuffer, sizeof(ownersndbuffer), 0);

		memcpy(&recvsndbuffer[0], IP, sizeof(IP));
		memcpy(&recvsndbuffer[OFFSET1], clientArray[fowner].address,sizeof(clientArray[fowner].address));

		//Send the ip address of the client who owns the file to the client who
		//wants the file
		r = send(clientArray[index].sock, recvsndbuffer, sizeof(recvsndbuffer), 0);

		if( s == -1 || r == -1){
			printf("Sending error!\n");
			perror("send()");
		}

	}

	//If the requested file is not on the server else
	else{
		char sendbuffer[MAXBUFSIZE];
		char nofilemsg[] = "\nFile Requested Is Not Available\n";
		bzero(&sendbuffer,sizeof(sendbuffer));

		memcpy(sendbuffer, MSG, sizeof(MSG));
		memcpy(&sendbuffer[OFFSET1], nofilemsg, sizeof(nofilemsg));

		int s;

		s = send(clientArray[index].sock, sendbuffer, sizeof(sendbuffer), 0);

		if( s == -1){
			printf("Sending error!\n");
			perror("send()");
		}
	}
}

