#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <pthread.h>
#include <mosquitto.h>
#include <json-c/json.h>

#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <termios.h>
#include <sys/signal.h>
#include <sys/ioctl.h>

// Server connection parameters
//#define MQTT_HOSTNAME "106.51.48.231"
#define MQTT_HOSTNAME "iot.eclipse.org"
#define MQTT_PORT 1883
//#define MQTT_USERNAME "clanmqtt"
//#define MQTT_PASSWORD "clan123"
//#define MQTT_TOPIC "test"

#define MODEMDEVICE "/dev/ttyAMA0"
#define BAUDRATE 115200
#define FALSE 0
#define TRUE 1

char mpayload[20480];

int STATUS=0;

void signal_handler_IO (int status);   /* definition of signal handler */
int wait_flag=TRUE;                    /* TRUE while no signal received */

const char s[2] = "~",sbuf[512];
size_t json_size;
char *token, *r_machine_id, *cmd, machine_id[10], topic[128], *execmd;
char time_buff[128],hwadd_buff[128],user_buff[128],volatge_buff[64],current_buff[64],power_buff[64],temperature_buff[64];

volatile unsigned int count=0,internet_connection=0,connection_status=0,count_temp=0;
FILE *mfile;

#define PACKETSIZE  64
struct packet
{
    struct icmphdr hdr;
    char msg[PACKETSIZE-sizeof(struct icmphdr)];
};

int pid=-1;
struct protoent *proto=NULL;
int cnt=1;

unsigned short checksum(void *b, int len)
{
    unsigned short *buf = b;
    unsigned int sum=0;
    unsigned short result;

    for ( sum = 0; len > 1; len -= 2 )
        sum += *buf++;
    if ( len == 1 )
        sum += *(unsigned char*)buf;
    sum = (sum >> 16) + (sum & 0xFFFF);
    sum += (sum >> 16);
    result = ~sum;
    return result;
}

int pingf(char *adress)
{
    const int val=255;
    unsigned int i;
    int sd;
    struct packet pckt;
    struct sockaddr_in r_addr;
    int loop;
    struct hostent *hname;
    struct sockaddr_in addr_ping,*addr;

    pid = getpid();
    proto = getprotobyname("ICMP");
    hname = gethostbyname(adress);
    bzero(&addr_ping, sizeof(addr_ping));
    addr_ping.sin_family = hname->h_addrtype;
    addr_ping.sin_port = 0;
    addr_ping.sin_addr.s_addr = *(long*)hname->h_addr;

    addr = &addr_ping;

    sd = socket(PF_INET, SOCK_RAW, proto->p_proto);
    if ( sd < 0 )
    {
        perror("socket");
        close(sd);
        return -1;
    }
    if ( setsockopt(sd, SOL_IP, IP_TTL, &val, sizeof(val)) != 0)
    {
        perror("Set TTL option");
        close(sd);
        return -1;
    }
    if ( fcntl(sd, F_SETFL, O_NONBLOCK) != 0 )
    {
        perror("Request nonblocking I/O");
        close(sd);
        return -1;
    }
    for (loop=0;loop < 10; loop++)
    {
        int len=sizeof(r_addr);

        if ( recvfrom(sd, &pckt, sizeof(pckt), 0, (struct sockaddr*)&r_addr, &len) > 0 )
        {
            close(sd);
            return 0;
        }

        bzero(&pckt, sizeof(pckt));
        pckt.hdr.type = ICMP_ECHO;
        pckt.hdr.un.echo.id = pid;
        for ( i = 0; i < sizeof(pckt.msg)-1; i++ )
            pckt.msg[i] = i+'0';
        pckt.msg[i] = 0;
        pckt.hdr.un.echo.sequence = cnt++;
        pckt.hdr.checksum = checksum(&pckt, sizeof(pckt));
        if ( sendto(sd, &pckt, sizeof(pckt), 0, (struct sockaddr*)addr, sizeof(*addr)) <= 0 )
        {
            perror("sendto");
            close(sd);
            return -1;
        }
        usleep(500000);
    }
    close(sd);
    return 0;
}

void signal_handler_IO(int status){
    wait_flag = FALSE;
}

void read_sensor_serialdata(void) {
    int fd, res;
    struct termios oldtio,newtio;
    char buf[128];
    struct sigaction saio;

    saio.sa_handler = signal_handler_IO;
    saio.sa_flags=0;
    saio.sa_restorer = NULL;
    sigaction(SIGIO, &saio, NULL);

    fd = open(MODEMDEVICE, O_RDWR | O_NOCTTY );
    if (fd <0) {perror(MODEMDEVICE); exit(-1); }

    fcntl(fd, F_SETFL, O_NONBLOCK);

    tcgetattr(fd,&oldtio); /* save current_buff serial port settings */
    bzero(&newtio, sizeof(newtio)); /* clear struct for new port settings */

    newtio.c_cflag = BAUDRATE | CRTSCTS | CS8 | CLOCAL | CREAD;
    newtio.c_iflag = IGNPAR | ICRNL;
    newtio.c_oflag = 0;
    newtio.c_lflag = ICANON;

    newtio.c_cc[VINTR]    = 0;     /* Ctrl-c */
    newtio.c_cc[VQUIT]    = 0;     /* Ctrl-\ */
    newtio.c_cc[VERASE]   = 0;     /* del */
    newtio.c_cc[VKILL]    = 0;     /* @ */
    newtio.c_cc[VEOF]     = 4;     /* Ctrl-d */
    newtio.c_cc[VTIME]    = 0;     /* inter-character timer unused */
    newtio.c_cc[VMIN]     = 1;     /* blocking read until 1 character arrives */
    newtio.c_cc[VSWTC]    = 0;     /* '\0' */
    newtio.c_cc[VSTART]   = 0;     /* Ctrl-q */
    newtio.c_cc[VSTOP]    = 0;     /* Ctrl-s */
    newtio.c_cc[VSUSP]    = 0;     /* Ctrl-z */
    newtio.c_cc[VEOL]     = 0;     /* '\0' */
    newtio.c_cc[VREPRINT] = 0;     /* Ctrl-r */
    newtio.c_cc[VDISCARD] = 0;     /* Ctrl-u */
    newtio.c_cc[VWERASE]  = 0;     /* Ctrl-w */
    newtio.c_cc[VLNEXT]   = 0;     /* Ctrl-v */
    newtio.c_cc[VEOL2]    = 0;     /* '\0' */

    //    printf("Signal check \n");
    tcflush(fd, TCIOFLUSH);
    tcsetattr(fd,TCSANOW,&newtio);

    /**************************Read volatge_buff Data********************************/

    write(fd, "command", 4);
    STATUS=1;
    while (STATUS) {
        usleep(50000);
        res = read(fd, volatge_buff, sizeof(volatge_buff));
        buf[res]=0;
        printf("Data1: %s\n",buf);
        memset(buf,0x00,sizeof(buf));
    }
    /**************************Read volatge_buff Data********************************/

    /**************************Read current_buff Data********************************/
    write(fd, "command", 4);
    STATUS=1;
    while (STATUS) {
        usleep(50000);
        res = read(fd, current_buff, sizeof(current_buff));
        current_buff[res]=0;
        printf("Data2: %s\n",current_buff);
        memset(buf,0x00,sizeof(current_buff));
    }
    /**************************Read current_buff Data********************************/

    /**************************Read power_buff Data********************************/
    write(fd, "command", 4);
    STATUS=1;
    while (STATUS) {
        usleep(50000);
        res = read(fd, power_buff, sizeof(power_buff));
        power_buff[res]=0;
        printf("Data3: %s\n",power_buff);
        memset(buf,0x00,sizeof(power_buff));
    }
    /**************************Read power_buff Data********************************/

    /**************************Read temperature_buff Data********************************/
    write(fd, "command", 4);
    STATUS=1;
    while (STATUS) {
        usleep(50000);
        res = read(fd, temperature_buff, sizeof(temperature_buff));
        temperature_buff[res]=0;
        printf("Data4: %s\n",temperature_buff);
        memset(temperature_buff,0x00,sizeof(temperature_buff));
    }
    /**************************Read temperature_buff Data********************************/

}

int main(int argc, char *argv[])
{
    if( argc == 2 ) {
        if(strcmp(argv[1],"-d")==0)
        {
            pid_t pid, sid;
            pid = fork();
            if (pid < 0) { exit(EXIT_FAILURE); }
            if (pid > 0) { exit(EXIT_SUCCESS); }
            umask(0);
            sid = setsid();
            if (sid < 0) { exit(EXIT_FAILURE); }
            if ((chdir("/")) < 0) { exit(EXIT_FAILURE); }
            close(STDIN_FILENO);
            close(STDOUT_FILENO);
            close(STDERR_FILENO);
        }
    }
    else if( argc > 2 ) {
        printf("Too many arguments supplied.\n");
    }
    else {
        printf("Debug Mode.\n");
    }

    struct json_object *jobj;
    struct {
        int flag;
        const char *flag_str;
    } json_flags[] = {
    { JSON_C_TO_STRING_PLAIN, "JSON_C_TO_STRING_PLAIN" },
    { JSON_C_TO_STRING_SPACED, "JSON_C_TO_STRING_SPACED" },
    { JSON_C_TO_STRING_PRETTY, "JSON_C_TO_STRING_PRETTY" },
    { JSON_C_TO_STRING_NOZERO, "JSON_C_TO_STRING_NOZERO" },
    { JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY, "JSON_C_TO_STRING_SPACED | JSON_C_TO_STRING_PRETTY" },
    { -1, NULL }
};

    while(1){
        count_temp++;
        printf("Internet Connection: %d\n", internet_connection);
        //     int ret = pingf(MQTT_HOSTNAME);
        //     int ret = system("ping iot.eclipse.org");

        if ( system("ping -c 2 iot.eclipse.org -w 2 > /dev/null") == 0)
        {
            struct mosquitto *mosq = NULL;

            // Initialize the Mosquitto library
            mosquitto_lib_init();

            // Create a new Mosquito runtime instance with a random client ID,
            //  and no application-specific callback data.
            mosq = mosquitto_new ("mqttdash-eece1100", true, NULL);
            if (!mosq)
            {
                fprintf (stderr, "Can't initialize Mosquitto library\n");
                connection_status=0;
            }
            else
            {
                connection_status=1;
            }
            //mosquitto_username_pw_set (mosq, MQTT_USERNAME, MQTT_PASSWORD);
            mosquitto_username_pw_set (mosq, NULL, NULL);

            // Establish a connection to the MQTT server. Do not use a keep-alive ping
            int ret = mosquitto_connect (mosq, MQTT_HOSTNAME, MQTT_PORT, 0);
            if (ret)
            {
                fprintf (stderr, "Can't connect to Mosquitto server\n");
                connection_status=0;
            }
            else
            {
                connection_status=1;
            }
            if(connection_status==1)
            {
                //                read_sensor_serialdata();
                system("hostname > /tmp/data");
                mfile = fopen("/tmp/data","r");
                if(mfile)
                {
                    fscanf(mfile,"%s",user_buff);
                    fclose(mfile);
                }
                system("date +%d%m%y%H%M%S > /tmp/data");
                mfile = fopen("/tmp/data","r");
                if(mfile)
                {
                    fscanf(mfile,"%s",time_buff);
                    fclose(mfile);
                }
                system("ifconfig eth0 | grep \"ether\" | awk '{print$2}' > /tmp/data");
                mfile = fopen("/tmp/data","r");
                if(mfile)
                {
                    fscanf(mfile,"%s",hwadd_buff);
                    fclose(mfile);
                }

                printf("%s\n",mpayload);
                jobj = json_object_new_object();
                json_object_object_add(jobj,"hostname",json_object_new_string(user_buff));
                json_object_object_add(jobj,"timestamp",json_object_new_string(time_buff));
                json_object_object_add(jobj,"macaddress",json_object_new_string(hwadd_buff));
                json_object_object_add(jobj,"voltage",json_object_new_string("230"));
                json_object_object_add(jobj,"current",json_object_new_string("12"));
                json_object_object_add(jobj,"power",json_object_new_string("35"));
                json_object_object_add(jobj,"temperature",json_object_new_string("95"));

                printf("%s\n",json_object_to_json_string_ext(jobj, json_flags[0].flag));
                json_size = strlen(json_object_to_json_string_ext(jobj, json_flags[0].flag));
                printf("Json Length: %d\n", json_size);
                ret = mosquitto_publish (mosq, NULL, "device_transaction", json_size, json_object_to_json_string_ext(jobj, json_flags[0].flag), 0, false);
                if (ret)
                {
                    fprintf (stderr, "Can't publish to Mosquitto server\n");
                    connection_status=0;
                }
                else
                {
                    connection_status=1;
                }
                json_object_put(jobj);
                json_size=0;

                memset(volatge_buff,'\0',sizeof(volatge_buff));
                memset(current_buff,'\0',sizeof(current_buff));
                memset(power_buff,'\0',sizeof(power_buff));
                memset(temperature_buff,'\0',sizeof(temperature_buff));
                sleep(5);

                jobj = json_object_new_object();
                json_object_object_add(jobj,"hostname",json_object_new_string(user_buff));
                json_object_object_add(jobj,"timestamp",json_object_new_string(time_buff));
                json_object_object_add(jobj,"macaddress",json_object_new_string(hwadd_buff));
                json_object_object_add(jobj,"voltage",json_object_new_string("220"));
                json_object_object_add(jobj,"current",json_object_new_string("18"));
                json_object_object_add(jobj,"power",json_object_new_string("40"));
                json_object_object_add(jobj,"temperature",json_object_new_string("90"));

                printf("%s\n",json_object_to_json_string_ext(jobj, json_flags[0].flag));
                json_size = strlen(json_object_to_json_string_ext(jobj, json_flags[0].flag));
                printf("Json Length: %d\n", json_size);
                ret = mosquitto_publish (mosq, NULL, "device_transaction", json_size, json_object_to_json_string_ext(jobj, json_flags[0].flag), 0, false);
                if (ret)
                {
                    fprintf (stderr, "Can't publish to Mosquitto server\n");
                    connection_status=0;
                }
                else
                {
                    connection_status=1;
                }
                json_object_put(jobj);
                json_size=0;

                memset(volatge_buff,'\0',sizeof(volatge_buff));
                memset(current_buff,'\0',sizeof(current_buff));
                memset(power_buff,'\0',sizeof(power_buff));
                memset(temperature_buff,'\0',sizeof(temperature_buff));
                sleep(5);

                jobj = json_object_new_object();
                json_object_object_add(jobj,"hostname",json_object_new_string(user_buff));
                json_object_object_add(jobj,"timestamp",json_object_new_string(time_buff));
                json_object_object_add(jobj,"macaddress",json_object_new_string(hwadd_buff));
                json_object_object_add(jobj,"voltage",json_object_new_string("235"));
                json_object_object_add(jobj,"current",json_object_new_string("15"));
                json_object_object_add(jobj,"power",json_object_new_string("30"));
                json_object_object_add(jobj,"temperature",json_object_new_string("99"));

                printf("%s\n",json_object_to_json_string_ext(jobj, json_flags[0].flag));
                json_size = strlen(json_object_to_json_string_ext(jobj, json_flags[0].flag));
                printf("Json Length: %d\n", json_size);
                ret = mosquitto_publish (mosq, NULL, "device_transaction", json_size, json_object_to_json_string_ext(jobj, json_flags[0].flag), 0, false);
                if (ret)
                {
                    fprintf (stderr, "Can't publish to Mosquitto server\n");
                    connection_status=0;
                }
                else
                {
                    connection_status=1;
                }
                json_object_put(jobj);
                json_size=0;

                memset(volatge_buff,'\0',sizeof(volatge_buff));
                memset(current_buff,'\0',sizeof(current_buff));
                memset(power_buff,'\0',sizeof(power_buff));
                memset(temperature_buff,'\0',sizeof(temperature_buff));
                sleep(5);
            }

        } else {
            sleep(1);
        }
    }
    return 0;
}
