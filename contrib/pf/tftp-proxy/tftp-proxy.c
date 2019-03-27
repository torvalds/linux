/* $OpenBSD: tftp-proxy.c,v 1.2 2006/12/20 03:33:38 joel Exp $
 *
 * Copyright (c) 2005 DLS Internet Services
 * Copyright (c) 2004, 2005 Camiel Dobbelaar, <cd@sentia.nl>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/ioctl.h>
#include <sys/uio.h>
#include <unistd.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <arpa/tftp.h>
#include <sys/socket.h>
#include <net/if.h>
#include <net/pfvar.h>

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <syslog.h>
#include <string.h>
#include <stdlib.h>

#include "filter.h"

#define CHROOT_DIR	"/var/empty"
#define NOPRIV_USER	"proxy"

#define PF_NAT_PROXY_PORT_LOW	50001
#define PF_NAT_PROXY_PORT_HIGH	65535

#define DEFTRANSWAIT	2
#define NTOP_BUFS	4
#define PKTSIZE		SEGSIZE+4

const char *opcode(int);
const char *sock_ntop(struct sockaddr *);
u_int16_t pick_proxy_port(void);
static void usage(void);

extern	char *__progname;
char	ntop_buf[NTOP_BUFS][INET6_ADDRSTRLEN];
int	verbose = 0;

int
main(int argc, char *argv[])
{
	int c, fd = 0, on = 1, out_fd = 0, peer, reqsize = 0;
	int transwait = DEFTRANSWAIT;
	char *p;
	struct tftphdr *tp;
	struct passwd *pw;

	char cbuf[CMSG_SPACE(sizeof(struct sockaddr_storage))];
	char req[PKTSIZE];
	struct cmsghdr *cmsg;
	struct msghdr msg;
	struct iovec iov;

	struct sockaddr_storage from, proxy, server, proxy_to_server, s_in;
	struct sockaddr_in sock_out;
	socklen_t j;
	in_port_t bindport;

	openlog(__progname, LOG_PID | LOG_NDELAY, LOG_DAEMON);

	while ((c = getopt(argc, argv, "vw:")) != -1)
		switch (c) {
		case 'v':
			verbose++;
			break;
		case 'w':
			transwait = strtoll(optarg, &p, 10);
			if (transwait < 1) {
				syslog(LOG_ERR, "invalid -w value");
				exit(1);
			}
			break;
		default:
			usage();
			break;
		}

	/* open /dev/pf */
	init_filter(NULL, verbose);

	tzset();

	pw = getpwnam(NOPRIV_USER);
	if (!pw) {
		syslog(LOG_ERR, "no such user %s: %m", NOPRIV_USER);
		exit(1);
	}
	if (chroot(CHROOT_DIR) || chdir("/")) {
		syslog(LOG_ERR, "chroot %s: %m", CHROOT_DIR);
		exit(1);
	}
	if (setgroups(1, &pw->pw_gid) ||
	    setresgid(pw->pw_gid, pw->pw_gid, pw->pw_gid) ||
	    setresuid(pw->pw_uid, pw->pw_uid, pw->pw_uid)) {
		syslog(LOG_ERR, "can't revoke privs: %m");
		exit(1);
	}

	/* non-blocking io */
	if (ioctl(fd, FIONBIO, &on) < 0) {
		syslog(LOG_ERR, "ioctl(FIONBIO): %m");
		exit(1);
	}

	if (setsockopt(fd, IPPROTO_IP, IP_RECVDSTADDR, &on, sizeof(on)) == -1) {
		syslog(LOG_ERR, "setsockopt(IP_RECVDSTADDR): %m");
		exit(1);
	}

	j = sizeof(s_in);
	if (getsockname(fd, (struct sockaddr *)&s_in, &j) == -1) {
		syslog(LOG_ERR, "getsockname: %m");
		exit(1);
	}

	bindport = ((struct sockaddr_in *)&s_in)->sin_port;

	/* req will be pushed back out at the end, unchanged */
	j = sizeof(from);
	if ((reqsize = recvfrom(fd, req, sizeof(req), MSG_PEEK,
	    (struct sockaddr *)&from, &j)) < 0) {
		syslog(LOG_ERR, "recvfrom: %m");
		exit(1);
	}

	bzero(&msg, sizeof(msg));
	iov.iov_base = req;
	iov.iov_len = sizeof(req);
	msg.msg_name = &from;
	msg.msg_namelen = sizeof(from);
	msg.msg_iov = &iov;
	msg.msg_iovlen = 1;
	msg.msg_control = cbuf;
	msg.msg_controllen = CMSG_LEN(sizeof(struct sockaddr_storage));

	if (recvmsg(fd, &msg, 0) < 0) {
		syslog(LOG_ERR, "recvmsg: %m");
		exit(1);
	}

	close(fd);
	close(1);

	peer = socket(from.ss_family, SOCK_DGRAM, 0);
	if (peer < 0) {
		syslog(LOG_ERR, "socket: %m");
		exit(1);
	}
	memset(&s_in, 0, sizeof(s_in));
	s_in.ss_family = from.ss_family;
	s_in.ss_len = from.ss_len;

	/* get local address if possible */
	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level == IPPROTO_IP &&
		    cmsg->cmsg_type == IP_RECVDSTADDR) {
			memcpy(&((struct sockaddr_in *)&s_in)->sin_addr,
			    CMSG_DATA(cmsg), sizeof(struct in_addr));
			break;
		}
	}

	if (bind(peer, (struct sockaddr *)&s_in, s_in.ss_len) < 0) {
		syslog(LOG_ERR, "bind: %m");
		exit(1);
	}
	if (connect(peer, (struct sockaddr *)&from, from.ss_len) < 0) {
		syslog(LOG_ERR, "connect: %m");
		exit(1);
	}

	tp = (struct tftphdr *)req;
	if (!(ntohs(tp->th_opcode) == RRQ || ntohs(tp->th_opcode) == WRQ)) {
		/* not a tftp request, bail */
		if (verbose) {
			syslog(LOG_WARNING, "not a valid tftp request");
			exit(1);
		} else
			/* exit 0 so inetd doesn't log anything */
			exit(0);
	}

	j = sizeof(struct sockaddr_storage);
	if (getsockname(fd, (struct sockaddr *)&proxy, &j) == -1) {
		syslog(LOG_ERR, "getsockname: %m");
		exit(1);
	}

	((struct sockaddr_in *)&proxy)->sin_port = bindport;

	/* find the un-rdr'd server and port the client wanted */
	if (server_lookup((struct sockaddr *)&from,
	    (struct sockaddr *)&proxy, (struct sockaddr *)&server,
	    IPPROTO_UDP) != 0) {
		syslog(LOG_ERR, "pf connection lookup failed (no rdr?)");
		exit(1);
	}

	/* establish a new outbound connection to the remote server */
	if ((out_fd = socket(((struct sockaddr *)&from)->sa_family,
	    SOCK_DGRAM, IPPROTO_UDP)) < 0) {
		syslog(LOG_ERR, "couldn't create new socket");
		exit(1);
	}

	bzero((char *)&sock_out, sizeof(sock_out));
	sock_out.sin_family = from.ss_family;
	sock_out.sin_port = htons(pick_proxy_port());
	if (bind(out_fd, (struct sockaddr *)&sock_out, sizeof(sock_out)) < 0) {
		syslog(LOG_ERR, "couldn't bind to new socket: %m");
		exit(1);
	}

	if (connect(out_fd, (struct sockaddr *)&server,
	    ((struct sockaddr *)&server)->sa_len) < 0 && errno != EINPROGRESS) {
		syslog(LOG_ERR, "couldn't connect to remote server: %m");
		exit(1);
	}

	j = sizeof(struct sockaddr_storage);
	if ((getsockname(out_fd, (struct sockaddr *)&proxy_to_server,
	    &j)) < 0) {
		syslog(LOG_ERR, "getsockname: %m");
		exit(1);
	}

	if (verbose)
		syslog(LOG_INFO, "%s:%d -> %s:%d/%s:%d -> %s:%d \"%s %s\"",
			sock_ntop((struct sockaddr *)&from),
			ntohs(((struct sockaddr_in *)&from)->sin_port),
			sock_ntop((struct sockaddr *)&proxy),
			ntohs(((struct sockaddr_in *)&proxy)->sin_port),
			sock_ntop((struct sockaddr *)&proxy_to_server),
			ntohs(((struct sockaddr_in *)&proxy_to_server)->sin_port),
			sock_ntop((struct sockaddr *)&server),
			ntohs(((struct sockaddr_in *)&server)->sin_port),
			opcode(ntohs(tp->th_opcode)),
			tp->th_stuff);

	/* get ready to add rdr and pass rules */
	if (prepare_commit(1) == -1) {
		syslog(LOG_ERR, "couldn't prepare pf commit");
		exit(1);
	}

	/* rdr from server to us on our random port -> client on its port */
	if (add_rdr(1, (struct sockaddr *)&server,
	    (struct sockaddr *)&proxy_to_server, ntohs(sock_out.sin_port),
	    (struct sockaddr *)&from,
	    ntohs(((struct sockaddr_in *)&from)->sin_port),
	    IPPROTO_UDP) == -1) {
		syslog(LOG_ERR, "couldn't add rdr");
		exit(1);
	}

	/* explicitly allow the packets to return back to the client (which pf
	 * will see post-rdr) */
	if (add_filter(1, PF_IN, (struct sockaddr *)&server,
	    (struct sockaddr *)&from,
	    ntohs(((struct sockaddr_in *)&from)->sin_port),
	    IPPROTO_UDP) == -1) {
		syslog(LOG_ERR, "couldn't add pass in");
		exit(1);
	}
	if (add_filter(1, PF_OUT, (struct sockaddr *)&server,
	    (struct sockaddr *)&from,
	    ntohs(((struct sockaddr_in *)&from)->sin_port),
	    IPPROTO_UDP) == -1) {
		syslog(LOG_ERR, "couldn't add pass out");
		exit(1);
	}

	/* and just in case, to pass out from us to the server */
	if (add_filter(1, PF_OUT, (struct sockaddr *)&proxy_to_server,
	    (struct sockaddr *)&server,
	    ntohs(((struct sockaddr_in *)&server)->sin_port),
	    IPPROTO_UDP) == -1) {
		syslog(LOG_ERR, "couldn't add pass out");
		exit(1);
	}

	if (do_commit() == -1) {
		syslog(LOG_ERR, "couldn't commit pf rules");
		exit(1);
	}

	/* forward the initial tftp request and start the insanity */
	if (send(out_fd, tp, reqsize, 0) < 0) {
		syslog(LOG_ERR, "couldn't forward tftp packet: %m");
		exit(1);
	}

	/* allow the transfer to start to establish a state */
	sleep(transwait);

	/* delete our rdr rule and clean up */
	prepare_commit(1);
	do_commit();

	return(0);
}

const char *
opcode(int code)
{
	static char str[6];

	switch (code) {
	case 1:
		(void)snprintf(str, sizeof(str), "RRQ");
		break;
	case 2:
		(void)snprintf(str, sizeof(str), "WRQ");
		break;
	default:
		(void)snprintf(str, sizeof(str), "(%d)", code);
		break;
	}

	return (str);
}

const char *
sock_ntop(struct sockaddr *sa)
{
	static int n = 0;

	/* Cycle to next buffer. */
	n = (n + 1) % NTOP_BUFS;
	ntop_buf[n][0] = '\0';

	if (sa->sa_family == AF_INET) {
		struct sockaddr_in *sin = (struct sockaddr_in *)sa;

		return (inet_ntop(AF_INET, &sin->sin_addr, ntop_buf[n],
		    sizeof ntop_buf[0]));
	}

	if (sa->sa_family == AF_INET6) {
		struct sockaddr_in6 *sin6 = (struct sockaddr_in6 *)sa;

		return (inet_ntop(AF_INET6, &sin6->sin6_addr, ntop_buf[n],
		    sizeof ntop_buf[0]));
	}

	return (NULL);
}

u_int16_t
pick_proxy_port(void)
{
	return (IPPORT_HIFIRSTAUTO + (arc4random() %
	    (IPPORT_HILASTAUTO - IPPORT_HIFIRSTAUTO)));
}

static void
usage(void)
{
	syslog(LOG_ERR, "usage: %s [-v] [-w transwait]", __progname);
	exit(1);
}
