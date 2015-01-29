/*
 * ICMP.c
 *
 * Simple C program to send
 * and receive an ICMP packages.
 *
 * This program is sending and tracking
 * incoming frames by frame sequence number
 * And saving latency for each frame and average.
 *
 * Output: Error rate, Latency time.
 * Input : pingable IP Address.
 *
 * avrahamcohen.ac@gmail.com
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h> 
 
#include <netinet/in.h>
#include <netinet/ip.h>
#include <netinet/ip_icmp.h>
#include <arpa/inet.h>
#include <netdb.h>

#include <sys/signal.h>
#include <sys/fcntl.h>
#include <string.h>
#include <pthread.h>

#define true 		1
#define false 		0
#define DEFDATALEN  0
#define MAXIPLEN    60
#define MAXICMPLEN  64
#define ERROR_RATE 	18
#define SENT_FRAME	20

/* Global Variables */

/* Frame sequence number */
static int seq = 0;

/* Latency average */
double latencyAvg; 
double latency[20];

int sendICMPframe(const char *host);
int checkSum(unsigned short *buf, int sz);

int main (int argc, char * argv[]) {
	if (sendICMPframe("10.0.0.3") == true)
		printf ("Error rate is less than or equal to 10%%, Average latency time is %lf \n",latencyAvg);
	else printf ("Error rate is more than 10%%, Average latency time is %lf \n",latencyAvg);
}

int checkSum(unsigned short *buf, int sz) {
  int nleft = sz;
  int sum = 0;
  unsigned short *w = buf;
  unsigned short ans = 0;

  while (nleft > 1) {
    sum += *w++;
    nleft -= 2;
  }

  if (nleft == 1) {
    *(unsigned char *) (&ans) = *(unsigned char *) w;
    sum += ans;
  }

  sum = (sum >> 16) + (sum & 0xFFFF);
  sum += (sum >> 16);
  ans = ~sum;
  return (ans);
}

int sendICMPframe(const char *host) {
	  int frameCount, pingsock, c, count, result;

	  struct hostent *h;
	  struct sockaddr_in pingaddr;
	  struct icmp *pkt, *_pkt;
	
	  char *hostname = NULL;
	  char packet[DEFDATALEN + MAXIPLEN + MAXICMPLEN];

	  if ((pingsock = socket(AF_INET, SOCK_RAW, 1)) < 0) {
	    perror("ping: creating a raw socket");
	    return (-1);
	  }

	  /* Define non block socket */
	  fcntl(pingsock, F_SETFL, O_NONBLOCK);

	  /* Drop root privs if running setuid */
	  setuid(getuid());

	  count  = 0;
	  result = 0;

	  for(frameCount=0; frameCount<SENT_FRAME; frameCount++)
	  {
		/* Transmit the frame */
		struct timespec tstart={0,0}, tend={0,0};
		double sec = 0;
		memset(&pingaddr, 0, sizeof(struct sockaddr_in));

		pingaddr.sin_family = AF_INET;
		if (!(h = gethostbyname(host))) {
		  fprintf(stderr, "ping: unknown host %s\n", host);
		  return (-1);
		}
		memcpy(&pingaddr.sin_addr, h->h_addr, sizeof(pingaddr.sin_addr));
		hostname = h->h_name;

		pkt = (struct icmp *) packet;
		memset(pkt, 0, sizeof(packet));
		pkt->icmp_type = ICMP_ECHO;

		/* Increment the sequence, and clear the checksum field so that it won't be
	    	included in the new calculation */
	    pkt->icmp_hun.ih_idseq.icd_seq = seq++;
	    pkt->icmp_cksum = 0;

		pkt->icmp_cksum = checkSum((unsigned short *) pkt, sizeof(packet));

	    c = sendto(pingsock, packet, sizeof(packet), 0,
	        (struct sockaddr *) &pingaddr, sizeof(struct sockaddr_in));

	    if (c < 0 || c != sizeof(packet)) {
	        if (c < 0)
	            perror("ping: sendto");
	        fprintf(stderr, "ping: write incomplete\n");
	        return (-1);
	        }
	    else
	    {
	    	/* Let's take timestamp */
	    	clock_gettime(CLOCK_MONOTONIC, &tstart);
	    	count++;
	    }

	    /* Listen for replies */
		while(true)
		{
			struct sockaddr_in from;
			struct timeval tv;
			tv.tv_usec = 20000; /* 20 Mili Sec */
			tv.tv_sec = 0;
			size_t fromlen = sizeof(from);
			fd_set readfds;

			FD_ZERO(&readfds);
			FD_SET(pingsock, &readfds);

			int res = select(pingsock + 1, &readfds, NULL, NULL, &tv);
			if(res == 1)
			{
				if ((c = recvfrom(pingsock, packet, sizeof(packet), 0, (struct sockaddr *) &from, (socklen_t *)&fromlen)) < 0)
				{
				  if (errno == EINTR)
					continue;
				  perror("ping: recvfrom");
				  continue;
				}
				if (c >= 76) { /* ip + icmp */
				  struct iphdr *iphdr = (struct iphdr *) packet;
				  _pkt = (struct icmp *) (packet + (iphdr->ihl << 2));      /* skip ip hdr */
				  if (_pkt->icmp_type == ICMP_ECHOREPLY) {
						if((_pkt->icmp_hun.ih_idseq.icd_seq >= (seq - 20)) && (_pkt->icmp_hun.ih_idseq.icd_seq < seq)){
							result++;
							clock_gettime(CLOCK_MONOTONIC, &tend);
							sec =  ((double)tend.tv_sec + 1.0e-9*tend.tv_nsec) - ((double)tstart.tv_sec + 1.0e-9*tstart.tv_nsec);
							latency[_pkt->icmp_hun.ih_idseq.icd_seq%SENT_FRAME] = sec;
						}
				  }
				}
			}
			else
			{
				sec = 1;
				FD_CLR(pingsock, &readfds);
				break;
			}
		}
	    if (sec < 0.02) usleep((0.02 - sec) * 1000);
	  }

	  close(pingsock);

	  /* Calculate Latency Average */
	  for(frameCount=0; frameCount<SENT_FRAME; frameCount++)
		  latencyAvg+=latency[frameCount];
	  latencyAvg = (double)latencyAvg/20;

	  return (result >= ERROR_RATE)?true:false; /* 10% Error Rate */
}


