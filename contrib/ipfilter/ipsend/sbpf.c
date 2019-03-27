/* $FreeBSD$ */
/*
 * (C)opyright 1995-1998 Darren Reed. (from tcplog)
 *
 * See the IPFILTER.LICENCE file for details on licencing.
 *
 */
#include <sys/param.h>
#include <sys/types.h>
#include <sys/mbuf.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#if BSD < 199103
#include <sys/fcntlcom.h>
#endif
#if (__FreeBSD_version >= 300000)
# include <sys/dirent.h>
#else
# include <sys/dir.h>
#endif
#include <net/bpf.h>

#include <net/if.h>
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/ip.h>
#include <netinet/udp.h>
#include <netinet/tcp.h>

#include <stdio.h>
#include <netdb.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#ifdef __NetBSD__
# include <paths.h>
#endif
#include <ctype.h>
#include <signal.h>
#include <errno.h>

#include "ipsend.h"

#if !defined(lint)
static const char sccsid[] = "@(#)sbpf.c	1.3 8/25/95 (C)1995 Darren Reed";
static const char rcsid[] = "@(#)$Id$";
#endif

/*
 * the code herein is dervied from libpcap.
 */
static	u_char	*buf = NULL;
static	int	bufsize = 0, timeout = 1;


int	initdevice(device, tout)
	char	*device;
	int	tout;
{
	struct	bpf_version bv;
	struct	timeval to;
	struct	ifreq ifr;
#ifdef _PATH_BPF
	char	*bpfname = _PATH_BPF;
	int	fd;

	if ((fd = open(bpfname, O_RDWR)) < 0)
	    {
		fprintf(stderr, "no bpf devices available as /dev/bpfxx\n");
		return -1;
	    }
#else
	char	bpfname[16];
	int	fd = 0, i;

	for (i = 0; i < 16; i++)
	    {
		(void) sprintf(bpfname, "/dev/bpf%d", i);
		if ((fd = open(bpfname, O_RDWR)) >= 0)
			break;
	    }
	if (i == 16)
	    {
		fprintf(stderr, "no bpf devices available as /dev/bpfxx\n");
		return -1;
	    }
#endif

	if (ioctl(fd, BIOCVERSION, (caddr_t)&bv) < 0)
	    {
		perror("BIOCVERSION");
		return -1;
	    }
	if (bv.bv_major != BPF_MAJOR_VERSION ||
	    bv.bv_minor < BPF_MINOR_VERSION)
	    {
		fprintf(stderr, "kernel bpf (v%d.%d) filter out of date:\n",
			bv.bv_major, bv.bv_minor);
		fprintf(stderr, "current version: %d.%d\n",
			BPF_MAJOR_VERSION, BPF_MINOR_VERSION);
		return -1;
	    }

	(void) strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name));
	if (ioctl(fd, BIOCSETIF, &ifr) == -1)
	    {
		fprintf(stderr, "%s(%d):", ifr.ifr_name, fd);
		perror("BIOCSETIF");
		exit(1);
	    }
	/*
	 * get kernel buffer size
	 */
	if (ioctl(fd, BIOCGBLEN, &bufsize) == -1)
	    {
		perror("BIOCSBLEN");
		exit(-1);
	    }
	buf = (u_char*)malloc(bufsize);
	/*
	 * set the timeout
	 */
	timeout = tout;
	to.tv_sec = 1;
	to.tv_usec = 0;
	if (ioctl(fd, BIOCSRTIMEOUT, (caddr_t)&to) == -1)
	    {
		perror("BIOCSRTIMEOUT");
		exit(-1);
	    }

	(void) ioctl(fd, BIOCFLUSH, 0);
	return fd;
}


/*
 * output an IP packet onto a fd opened for /dev/bpf
 */
int	sendip(fd, pkt, len)
	int	fd, len;
	char	*pkt;
{
	if (write(fd, pkt, len) == -1)
	    {
		perror("send");
		return -1;
	    }

	return len;
}
