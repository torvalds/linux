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
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/timeb.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/stropts.h>

#ifdef sun
# include <sys/pfmod.h>
# include <sys/bufmod.h>
#endif
# include <sys/dlpi.h>

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
static const char sccsid[] = "@(#)sdlpi.c	1.3 10/30/95 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

#define	CHUNKSIZE	8192
#define BUFSPACE	(4*CHUNKSIZE)


/*
 * Be careful to only include those defined in the flags option for the
 * interface are included in the header size.
 */
int	initdevice(device, tout)
	char	*device;
	int	tout;
{
	char	devname[16], *s, buf[256];
	int	i, fd;

	(void) strcpy(devname, "/dev/");
	(void) strncat(devname, device, sizeof(devname) - strlen(devname));

	s = devname + 5;
	while (*s && !ISDIGIT(*s))
		s++;
	if (!*s)
	    {
		fprintf(stderr, "bad device name %s\n", devname);
		exit(-1);
	    }
	i = atoi(s);
	*s = '\0';
	/*
	 * For writing
	 */
	if ((fd = open(devname, O_RDWR)) < 0)
	    {
		fprintf(stderr, "O_RDWR(1) ");
		perror(devname);
		exit(-1);
	    }

	if (dlattachreq(fd, i) == -1)
	    {
		fprintf(stderr, "dlattachreq: DLPI error\n");
		exit(-1);
	    }
	else if (dlokack(fd, buf) == -1)
	    {
		fprintf(stderr, "dlokack(attach): DLPI error\n");
		exit(-1);
	    }
#ifdef DL_HP_RAWDLS
	if (dlpromisconreq(fd, DL_PROMISC_SAP) < 0)
	    {
		fprintf(stderr, "dlpromisconreq: DL_PROMISC_PHYS error\n");
		exit(-1);
	    }
	else if (dlokack(fd, buf) < 0)
	    {
		fprintf(stderr, "dlokack(promisc): DLPI error\n");
		exit(-1);
	    }
	/* 22 is INSAP as per the HP-UX DLPI Programmer's Guide */

	dlbindreq(fd, 22, 1, DL_HP_RAWDLS, 0, 0);
#else
	dlbindreq(fd, ETHERTYPE_IP, 0, DL_CLDLS, 0, 0);
#endif
	dlbindack(fd, buf);
	/*
	 * write full headers
	 */
#ifdef DLIOCRAW /* we require RAW DLPI mode, which is a Sun extension */
	if (strioctl(fd, DLIOCRAW, -1, 0, NULL) == -1)
	    {
		fprintf(stderr, "DLIOCRAW error\n");
		exit(-1);
	    }
#endif
	return fd;
}


/*
 * output an IP packet onto a fd opened for /dev/nit
 */
int	sendip(fd, pkt, len)
	int	fd, len;
	char	*pkt;
{
	struct strbuf dbuf, *dp = &dbuf, *cp = NULL;
	int pri = 0;
#ifdef DL_HP_RAWDLS
	struct strbuf cbuf;
	dl_hp_rawdata_req_t raw;

	cp = &cbuf;
	raw.dl_primitive = DL_HP_RAWDATA_REQ;
	cp->len = sizeof(raw);
	cp->buf = (char *)&raw;
	cp->maxlen = cp->len;
	pri = MSG_HIPRI;
#endif
	/*
	 * construct NIT STREAMS messages, first control then data.
	 */
	dp->buf = pkt;
	dp->len = len;
	dp->maxlen = dp->len;

	if (putmsg(fd, cp, dp, pri) == -1)
	    {
		perror("putmsg");
		return -1;
	    }
	if (ioctl(fd, I_FLUSH, FLUSHW) == -1)
	    {
		perror("I_FLUSHW");
		return -1;
	    }
	return len;
}

