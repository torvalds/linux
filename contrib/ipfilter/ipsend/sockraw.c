/*	$FreeBSD$	*/

/*
 * (C)opyright 2000 Darren Reed.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 * WARNING: Attempting to use this .c file on HP-UX 11.00 will cause the
 *          system to crash.
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include "ipsend.h"

#if !defined(lint) && defined(LIBC_SCCS)
static	char	sirix[] = "@(#)sirix.c	1.0 10/9/97 (C)1997 Marc Boucher";
#endif


int	initdevice(char *device, int tout)
{
	struct sockaddr s;
	struct ifreq ifr;
	int fd;

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, device, sizeof ifr.ifr_name);

	if ((fd = socket(AF_INET, SOCK_RAW, IPPROTO_RAW)) < 0)
	    {
		perror("socket(AF_INET, SOCK_RAW, IPPROTO_RAW)");
		return -1;
	    }

	if (ioctl(fd, SIOCGIFADDR, &ifr) == -1)
	    {
		perror("ioctl SIOCGIFADDR");
		return -1;
	    }

	bzero((char *)&s, sizeof(s));
	s.sa_family = AF_INET;
	bcopy(&ifr.ifr_addr, s.sa_data, 4);
	if (bind(fd, &s, sizeof(s)) == -1)
		perror("bind");
	return fd;
}


/*
 * output an IP packet
 */
int	sendip(int fd, char *pkt, int len)
{
	struct ether_header *eh;
	struct sockaddr_in sin;

	eh = (struct ether_header *)pkt;
	bzero((char *)&sin, sizeof(sin));
	sin.sin_family = AF_INET;
	pkt += 14;
	len -= 14;
	bcopy(pkt + 12, (char *)&sin.sin_addr, 4);

	if (sendto(fd, pkt, len, 0, &sin, sizeof(sin)) == -1)
	    {
		perror("send");
		return -1;
	    }

	return len;
}
