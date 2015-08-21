#include <stdio.h>
#include <stdlib.h>
#include <string.h> /* memset() */
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include <signal.h>

#define PORT     "53" /* Port to listen on */
#define BACKLOG  10      /* Passed to listen() */
#define BUF_SIZE 4096    /* Buffer for  transfers */

#ifdef EMBEDDED
#include <sys/utsname.h>
#include <sys/mount.h>
#include <linux/types.h>
#include <linux/if.h>

void showip(char *interface) {
    int fd;
    struct ifreq ifr;
    
    fd = socket(AF_INET, SOCK_DGRAM, 0);
    
    /* I want to get an IPv4 IP address */
    ifr.ifr_addr.sa_family = AF_INET;
    
    /* I want IP address attached to "eth0" */
    strncpy(ifr.ifr_name, interface, IFNAMSIZ-1);
    
    ioctl(fd, SIOCGIFADDR, &ifr);
    
    close(fd);
    
    /* display result */
    printf("This device's IP address: %s\n", inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}


void unameinfo(void) {
    struct utsname buffer;
    if (uname(&buffer) != 0) {
        perror("uname");
        return;
    }
    printf("Running on %s %s %s %s\n", buffer.sysname,buffer.release,buffer.version,buffer.machine);
}


char* readfilestr(char *filename)
{
    char *buffer = NULL;
    int string_size,read_size;
    FILE *handler = fopen(filename,"r");
    
    if (handler)
    {
        //seek the last byte of the file
        fseek(handler,0,SEEK_END);
        //offset from the first to the last byte, or in other words, filesize
        string_size = ftell (handler);
        //go back to the start of the file
        rewind(handler);
        
        //allocate a string that can hold it all
        buffer = (char*) malloc (sizeof(char) * (string_size + 1) );
        //read it all in one operation
        read_size = fread(buffer,sizeof(char),string_size,handler);
        //fread doesnt set it so put a \0 in the last position
        //and buffer is now officialy a string
        buffer[string_size] = '\0';
        
        if (string_size != read_size) {
            //something went wrong, throw away the memory and set
            //the buffer to NULL
            free(buffer);
            buffer = NULL;
        }
    }
    
    return buffer;
}
#endif

unsigned int transfer(int from, int to)
{
    char buf[BUF_SIZE];
    unsigned int disconnected = 0;
    size_t bytes_read, bytes_written;
    bytes_read = read(from, buf, BUF_SIZE);
    if (bytes_read == 0) {
        disconnected = 1;
    }
    else {
        bytes_written = write(to, buf, bytes_read);
        if (bytes_written == (size_t)-1) {
            disconnected = 1;
        }
    }
    return disconnected;
}

void handle(int client, char *host, int port)
{
    int server = -1;
    unsigned int disconnected = 0;
    fd_set set;
    unsigned int max_sock;
    
    
    /* Create the socket */
    server = socket(PF_INET,SOCK_STREAM,IPPROTO_IP);
    if (server == -1) {
        perror("TCP error: socket");
        close(client);
        return;
    }
    struct sockaddr_in serv_addr;
    serv_addr.sin_addr.s_addr = inet_addr(host);
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);
    
    /* Connect to the host */
    if (connect(server, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) == -1) {
        perror("TCP error: connect");
        close(client);
        return;
    }
    
    if (client > server) {
        max_sock = client;
    }
    else {
        max_sock = server;
    }
    
    /* Main transfer loop */
    while (!disconnected) {
        FD_ZERO(&set);
        FD_SET(client, &set);
        FD_SET(server, &set);
        if (select(max_sock + 1, &set, NULL, NULL, NULL) == -1) {
            perror("TCP error: select");
            break;
        }
        if (FD_ISSET(client, &set)) {
            disconnected = transfer(client, server);
        }
        if (FD_ISSET(server, &set)) {
            disconnected = transfer(server, client);
        }
    }
    close(server);
    close(client);
}

void udpthread(char *ip, int port) {
    int os=socket(PF_INET,SOCK_DGRAM,IPPROTO_IP);
    
    struct sockaddr_in a;
    a.sin_family=AF_INET;
    a.sin_addr.s_addr=inet_addr("0.0.0.0"); a.sin_port=htons(53);
    if(bind(os,(struct sockaddr *)&a,sizeof(a)) == -1) {
        perror("UDP error: bind");
        exit(1); 
    }
    
    a.sin_addr.s_addr=inet_addr(ip); a.sin_port=htons(port);
    
    struct sockaddr_in sa;
    struct sockaddr_in da; da.sin_addr.s_addr=0;
    puts("Started UDP thread");
    while(1) {
        char buf[256];
        socklen_t sn=sizeof(sa);
        int n=recvfrom(os,buf,sizeof(buf),0,(struct sockaddr *)&sa,&sn);
        if(n<=0) continue;
        
        if(sa.sin_addr.s_addr==a.sin_addr.s_addr && sa.sin_port==a.sin_port) {
            if(da.sin_addr.s_addr) sendto(os,buf,n,0,(struct sockaddr *)&da,sizeof(da));
        } else {
            sendto(os,buf,n,0,(struct sockaddr *)&a,sizeof(a));
            da=sa;
        }
    }
}


int main(int argc, char **argv)
{
    int sock;
    int reuseaddr = 1; /* True */
    char * host, * port;
#ifdef EMBEDDED
    nice(-20);
    puts("ProxyDNS OS v0.9 starting");
    unameinfo();
    showip("eth0");
    puts("Waiting for the SD card");
    while ( access( "/dev/mmcblk0p1", R_OK ) == -1 ) {}
    mount("/dev/mmcblk0p1","/mnt","vfat", MS_RDONLY| MS_SILENT| MS_NODEV| MS_NOEXEC| MS_NOSUID,"");
    puts("Loading configuration files");
    host = readfilestr("/mnt/proxydns2/host.txt");
    if(!host) {
        puts("ERROR: proxydns2/host.txt MISSING ON SD CARD!");
        return 1;
    }
    port = readfilestr("/mnt/proxydns2/port.txt");
    if(!port) {
        puts("ERROR: proxydns2/port.txt MISSING ON SD CARD!");
        return 1;
    }
    umount("/mnt");
#else
    /* Get the server host and port from the command line */
    if (argc < 3) {
        fprintf(stderr, "Usage: proxydns2 host port\n");
        return 1;
    }
    host = argv[1];
    port = argv[2];
    printf("Using proxy DNS server at %s port %s\n",host,port);
#endif
    /* Create the socket */
    sock = socket(PF_INET,SOCK_STREAM,IPPROTO_IP);
    if (sock == -1) {
        perror("TCP error: socket");
        
        return 1;
    }
    
    /* Enable the socket to reuse the address */
    if (setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &reuseaddr, sizeof(int)) == -1) {
        perror("TCP error: setsockopt");
        
        return 1;
    }
    struct sockaddr_in atcp;
    atcp.sin_family=AF_INET;
    atcp.sin_addr.s_addr=inet_addr("0.0.0.0"); atcp.sin_port=htons(53);
    /* Bind to the address */
    if (bind(sock, (struct sockaddr *)&atcp,sizeof(atcp)) == -1) {
        perror("TCP error: bind");
        
        return 1;
    }
    
    /* Listen */
    if (listen(sock, BACKLOG) == -1) {
        perror("TCP error: listen");
        
        return 1;
    }
    
    /* Ignore broken pipe signal */
    signal(SIGPIPE, SIG_IGN);
    pid_t pid = fork();
    
    if (pid == 0)
    {
        // child process
        udpthread(host,atoi(port));
    }
    else if (pid > 0)
    {
        // parent process
        /* Main loop */
        puts("Started TCP thread");
        while (1) {
            socklen_t size = sizeof(struct sockaddr_in);
            struct sockaddr_in their_addr;
            int newsock = accept(sock, (struct sockaddr*)&their_addr, &size);
            
            if (newsock == -1) {
                perror("TCP error: accept");
            }
            else {
                handle(newsock, host, atoi(port));
            }
        }
        
        close(sock);
    }
    else
    {
        // fork failed
        perror("fork");
        close(sock);
        return 1;
    }
    return 0;
}
