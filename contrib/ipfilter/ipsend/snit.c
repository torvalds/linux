/*	$FreeBSD$	*/

/*
 * (C)opyright 1992-1998 Darren Reed. (from tcplog)
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */

#include <stdio.h>
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
#include <net/nit.h>
#include <sys/fcntlcom.h>
#include <sys/dir.h>
#include <net/nit_if.h>
#include <net/nit_pf.h>
#include <net/nit_buf.h>
#include <net/packetfilt.h>
#include <sys/stropts.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/if_ether.h>
#include <netinet/ip_var.h>
#include <netinet/udp.h>
#include <netinet/udp_var.h>
#include <netinet/tcp.h>

#include "ipsend.h"

#if !defined(lint)
static const char sccsid[] = "@(#)snit.c	1.5 1/11/96 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#define	CHUNKSIZE	8192
#define BUFSPACE	(4*CHUNKSIZE)

/*
 * Be careful to only include those defined in the flags option for the
 * interface are included in the header size.
 */
#define BUFHDR_SIZE  (sizeof(struct nit_bufhdr))
#define NIT_HDRSIZE  (BUFHDR_SIZE)

static	int	timeout;


int	initdevice(device, tout)
	char	*device;
	int	tout;
{
	struct	strioctl si;
	struct	timeval to;
	struct	ifreq ifr;
	int	fd;

	if ((fd = open("/dev/nit", O_RDWR)) < 0)
	    {
		perror("/dev/nit");
		exit(-1);
	    }

	/*
	 * arrange to get messages from the NIT STREAM and use NIT_BUF option
	 */
	ioctl(fd, I_SRDOPT, (char*)RMSGD);
	ioctl(fd, I_PUSH, "nbuf");

	/*
	 * set the timeout
	 */
	timeout = tout;
	si.ic_timout = 1;
	to.tv_sec = 1;
	to.tv_usec = 0;
	si.ic_cmd = NIOCSTIME;
	si.ic_len = sizeof(to);
	si.ic_dp = (char*)&to;
	if (ioctl(fd, I_STR, (char*)&si) == -1)
	    {
		perror("ioctl: NIT timeout");
		exit(-1);
	    }

	/*
	 * request the interface
	 */
	strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	ifr.ifr_name[sizeof(ifr.ifr_name) - 1] = ' ';
	si.ic_cmd = NIOCBIND;
	si.ic_len = sizeof(ifr);
	si.ic_dp = (char*)&ifr;
	if (ioctl(fd, I_STR, (char*)&si) == -1)
	    {
		perror(ifr.ifr_name);
		exit(1);
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
	struct	sockaddr sk, *sa = &sk;
	struct	strbuf	cbuf, *cp = &cbuf, dbuf, *dp = &dbuf;

	/*
	 * For ethernet, need at least 802.3 header and IP header.
	 */
	if (len < (sizeof(sa->sa_data) + sizeof(struct ip)))
		return -1;
	/*
	 * to avoid any output processing for IP, say we're not.
	 */
	sa->sa_family = AF_UNSPEC;
	bcopy(pkt, sa->sa_data, sizeof(sa->sa_data));
	pkt += sizeof(sa->sa_data);
	len -= sizeof(sa->sa_data);

	/*
	 * construct NIT STREAMS messages, first control then data.
	 */
	cp->len = sizeof(*sa);
	cp->maxlen = sizeof(*sa);
	cp->buf = (char *)sa;

	dp->buf = pkt;
	dp->len = len;
	dp->maxlen = dp->len;

	if (putmsg(fd, cp, dp, 0) == -1)
	    {
		perror("putmsg");
		return -1;
	    }

	if (ioctl(fd, I_FLUSH, FLUSHW) == -1)
	    {
		perror("I_FLUSH");
		return -1;
	    }
	return len;
}
