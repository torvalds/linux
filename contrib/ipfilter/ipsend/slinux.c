/*	$FreeBSD$	*/

/*
 * (C)opyright 1992-1998 Darren Reed. (from tcplog)
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */

#include <stdio.h>
#include <string.h>
#include <netdb.h>
#include <ctype.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/dir.h>
#include <linux/netdevice.h>
#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include "ipsend.h"

#if !defined(lint)
static const char sccsid[] = "@(#)slinux.c	1.2 8/25/95";
static const char rcsid[] = "@(#)$Id$";
#endif

#define	CHUNKSIZE	8192
#define BUFSPACE	(4*CHUNKSIZE)

/*
 * Be careful to only include those defined in the flags option for the
 * interface are included in the header size.
 */

static	int	timeout;
static	char	*eth_dev = NULL;


int	initdevice(dev, spare)
	char	*dev;
	int	spare;
{
	int fd;

	eth_dev = strdup(dev);
	if ((fd = socket(AF_INET, SOCK_PACKET, htons(ETHERTYPE_IP))) == -1)
	    {
		perror("socket(SOCK_PACKET)");
		exit(-1);
	    }

	return fd;
}


/*
 * output an IP packet onto a fd opened for /dev/nit
 */
int	sendip(fd, pkt, len)
	int	fd, len;
	char	*pkt;
{
	struct	sockaddr	s;
	struct	ifreq	ifr;

	strncpy(ifr.ifr_name, eth_dev, sizeof(ifr.ifr_name));
	if (ioctl(fd, SIOCGIFHWADDR, &ifr) == -1)
	    {
		perror("SIOCGIFHWADDR");
		return -1;
	    }
	bcopy(ifr.ifr_hwaddr.sa_data, pkt + 6, 6);
	s.sa_family = ETHERTYPE_IP;
	strncpy(s.sa_data, eth_dev, sizeof(s.sa_data));

	if (sendto(fd, pkt, len, 0, &s, sizeof(s)) == -1)
	    {
		perror("send");
		return -1;
	    }

	return len;
}
