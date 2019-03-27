/*
 * Copyright (c) 2000
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/* XXX Yes this is WAY too complicated */

#ifndef lint
static const char rcsid[] =
    "@(#) $Id: findsaddr-socket.c,v 1.1 2000/11/23 20:17:12 leres Exp $ (LBL)";
#endif

#include <sys/param.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#ifdef HAVE_SYS_SOCKIO_H
#include <sys/sockio.h>
#endif
#include <sys/time.h>				/* concession to AIX */

#if __STDC__
struct mbuf;
struct rtentry;
#endif

#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <netinet/in.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "gnuc.h"
#ifdef HAVE_OS_PROTO_H
#include "os-proto.h"
#endif

#include "findsaddr.h"

#ifdef HAVE_SOCKADDR_SA_LEN
#define SALEN(sa) ((sa)->sa_len)
#else
#define SALEN(sa) salen(sa)
#endif

#ifndef roundup
#define roundup(x, y)   ((((x)+((y)-1))/(y))*(y))  /* to any y */
#endif

struct rtmsg {
        struct rt_msghdr rtmsg;
        u_char data[512];
};

static struct rtmsg rtmsg = {
	{ 0, RTM_VERSION, RTM_GET, 0,
	RTF_UP | RTF_GATEWAY | RTF_HOST | RTF_STATIC,
	RTA_DST | RTA_IFA, 0, 0, 0, 0, 0, { 0 } },
	{ 0 }
};

#ifndef HAVE_SOCKADDR_SA_LEN
static int salen(struct sockaddr *);
#endif

/*
 * Return the source address for the given destination address
 */
const char *
findsaddr(register const struct sockaddr_in *to,
    register struct sockaddr_in *from)
{
	register struct rt_msghdr *rp;
	register u_char *cp;

	register struct sockaddr_in *sp, *ifa;
	register struct sockaddr *sa;
	register int s, size, cc, seq, i;
	register pid_t pid;
	static char errbuf[512];

	s = socket(PF_ROUTE, SOCK_RAW, AF_UNSPEC);
	if (s < 0) {
		sprintf(errbuf, "socket: %.128s", strerror(errno));
		return (errbuf);
	}

	seq = 0;
	pid = getpid();

	rp = &rtmsg.rtmsg;
	rp->rtm_seq = ++seq;
	cp = (u_char *)(rp + 1);

	sp = (struct sockaddr_in *)cp;
	*sp = *to;
	cp += roundup(SALEN((struct sockaddr *)sp), sizeof(u_int32_t));

	size = cp - (u_char *)rp;
	rp->rtm_msglen = size;

	cc = write(s, (char *)rp, size);
	if (cc < 0) {
		sprintf(errbuf, "write: %.128s", strerror(errno));
		close(s);
		return (errbuf);
	}
	if (cc != size) {
		sprintf(errbuf, "short write (%d != %d)", cc, size);
		close(s);
		return (errbuf);
	}

	size = sizeof(rtmsg);
	do {
		memset(rp, 0, size);
		cc = read(s, (char *)rp, size);
		if (cc < 0) {
			sprintf(errbuf, "read: %.128s", strerror(errno));
			close(s);
			return (errbuf);
		}

	} while (rp->rtm_type != RTM_GET || rp->rtm_seq != seq ||
	    rp->rtm_pid != pid);
	close(s);


	if (rp->rtm_version != RTM_VERSION) {
		sprintf(errbuf, "bad version %d", rp->rtm_version);
		return (errbuf);
	}
	if (rp->rtm_msglen > cc) {
		sprintf(errbuf, "bad msglen %d > %d", rp->rtm_msglen, cc);
		return (errbuf);
	}
	if (rp->rtm_errno != 0) {
		sprintf(errbuf, "rtm_errno: %.128s", strerror(rp->rtm_errno));
		return (errbuf);
	}

	/* Find the interface sockaddr */
	cp = (u_char *)(rp + 1);
	for (i = 1; i != 0; i <<= 1)
		if ((i & rp->rtm_addrs) != 0) {
			sa = (struct sockaddr *)cp;
			switch (i) {

			case RTA_IFA:
				if (sa->sa_family == AF_INET) {
					ifa = (struct sockaddr_in *)cp;
					if (ifa->sin_addr.s_addr != 0) {
						*from = *ifa;
						return (NULL);
					}
				}
				break;

			}

			if (SALEN(sa) == 0)
				cp += sizeof(long);
			else
				cp += roundup(SALEN(sa), sizeof(long));
		}

	return ("failed!");
}

#ifndef HAVE_SOCKADDR_SA_LEN
static int
salen(struct sockaddr *sa)
{
	switch (sa->sa_family) {

	case AF_INET:
		return (sizeof(struct sockaddr_in));

	case AF_LINK:
		return (sizeof(struct sockaddr_dl));

	default:
		return (sizeof(struct sockaddr));
	}
}
#endif
