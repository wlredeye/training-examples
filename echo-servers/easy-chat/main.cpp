#include <iostream>
#include <map>

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <arpa/inet.h>

#define MAX_EVENTS 32

int set_nonblock(int fd){
    int flags;
#if defined(O_NONBLOCK)
    if(-1 == (flags = fcntl(fd, F_GETFL, 0)))
        flags = 0;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#else
    flags = 1;
    return ioctl(fd, FIOBIO, &flags);
#endif
}

void sendMsg2All(const std::map<int,std::string> &SlaveSockets, const std::string &msg, int notSend = -1){
	for(auto Iter = SlaveSockets.begin(); Iter != SlaveSockets.end(); Iter++){
		if(Iter->first == notSend) {
			continue;
		}
		send(Iter->first,msg.c_str(),msg.size(),MSG_NOSIGNAL);
	}
}

int main(int argc, const char * argv[]) {
    int MasterSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	std::map<int, std::string> SlaveSockets;

    struct sockaddr_in SockAddr;
    SockAddr.sin_family = AF_INET;
    SockAddr.sin_port = htons(8080);
    SockAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    
    bind(MasterSocket, (struct sockaddr*)(&SockAddr), sizeof(SockAddr));
    
    set_nonblock(MasterSocket);
    
    listen(MasterSocket,SOMAXCONN);
    
	int Epoll = epoll_create1(0);

	struct epoll_event Event;
	Event.data.fd = MasterSocket;
	Event.events = EPOLLIN; //which events we should receive. EPOLLIN - read events
	epoll_ctl(Epoll, EPOLL_CTL_ADD, MasterSocket, &Event);

    while(true){
		struct epoll_event Events[MAX_EVENTS];
		int N = epoll_wait(Epoll, Events, MAX_EVENTS, -1);

		for(unsigned int i = 0; i < N; i++){
			if(Events[i].data.fd == MasterSocket){
				struct sockaddr_in clientaddr;
				socklen_t clientaddr_size = sizeof(clientaddr);
				int SlaveSocket = accept(MasterSocket, (struct sockaddr*)&clientaddr, &clientaddr_size);
				std::string ip(inet_ntoa(clientaddr.sin_addr));
				std::cout << ip << " connected" <<  std::endl;

				set_nonblock(SlaveSocket);
				SlaveSockets.insert(std::pair<int,std::string>(SlaveSocket, ip));

				struct epoll_event Event;
				Event.data.fd = SlaveSocket;
				Event.events = EPOLLIN;
				epoll_ctl(Epoll, EPOLL_CTL_ADD, SlaveSocket, &Event);

				//new user connected! Send message to all clients
				std::string msg = ip + " connected!\n";
				sendMsg2All(SlaveSockets, msg, SlaveSocket);
			} else {
				static char Buffer[1024];
				int RecvResult = recv(Events[i].data.fd, Buffer, 1024, MSG_NOSIGNAL);
				if((RecvResult == 0) &&(errno != EAGAIN)){
					shutdown(Events[i].data.fd, SHUT_RDWR);
					close(Events[i].data.fd);
					std::string msg = SlaveSockets[Events[i].data.fd] + " disconnected!\n";
					SlaveSockets.erase(Events[i].data.fd);
					sendMsg2All(SlaveSockets, msg);
				} else if(RecvResult > 0) {
					Buffer[RecvResult] = '\0';
					std::string msg = SlaveSockets[Events[i].data.fd] + " says: " + Buffer + "\n";
					sendMsg2All(SlaveSockets, msg, Events[i].data.fd);
				}
			}
		}
	}
    
    return 0;
}
