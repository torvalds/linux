/*	$FreeBSD$	*/

/*
 * (C)opyright 1992-1998 Darren Reed.
 * (C)opyright 1997 Marc Boucher.
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */
#include <stdio.h>
#include <sys/types.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

#include <net/if.h>
#include <net/raw.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include "ipsend.h"
#include <netinet/udp_var.h>

#if !defined(lint) && defined(LIBC_SCCS)
static	char	sirix[] = "@(#)sirix.c	1.0 10/9/97 (C)1997 Marc Boucher";
#endif


int	initdevice(char *device, int tout)
{
	int fd;
	struct sockaddr_raw sr;

	if ((fd = socket(PF_RAW, SOCK_RAW, RAWPROTO_DRAIN)) < 0)
	    {
		perror("socket(PF_RAW, SOCK_RAW, RAWPROTO_DRAIN)");
		return -1;
	    }

	memset(&sr, 0, sizeof(sr));
	sr.sr_family = AF_RAW;
	sr.sr_port = ETHERTYPE_IP;
	strncpy(sr.sr_ifname, device, sizeof(sr.sr_ifname));
	if (bind(fd, &sr, sizeof(sr)) < 0)
	    {
		perror("bind AF_RAW");
		close(fd);
		return -1;
	    }
	return fd;
}


/*
 * output an IP packet
 */
int	sendip(int fd, char *pkt, int len)
{
	struct sockaddr_raw sr;
	int srlen = sizeof(sr);
	struct ifreq ifr;
	struct ether_header *eh = (struct ether_header *)pkt;

	if (getsockname(fd, &sr, &srlen) == -1)
	    {
		perror("getsockname");
		return -1;
	    }

	memset(&ifr, 0, sizeof(ifr));
	strncpy(ifr.ifr_name, sr.sr_ifname, sizeof ifr.ifr_name);

	if (ioctl(fd, SIOCGIFADDR, &ifr) == -1)
	    {
		perror("ioctl SIOCGIFADDR");
		return -1;
	    }

	memcpy(eh->ether_shost, ifr.ifr_addr.sa_data, sizeof(eh->ether_shost));

	if (write(fd, pkt, len) == -1)
	    {
		perror("send");
		return -1;
	    }

	return len;
}
