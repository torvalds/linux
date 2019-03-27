/*	$NetBSD: mcast.c,v 1.3 2015/05/28 10:19:17 ozaki-r Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/cdefs.h>
#ifdef __RCSID
__RCSID("$NetBSD: mcast.c,v 1.3 2015/05/28 10:19:17 ozaki-r Exp $");
#else
extern const char *__progname;
#define getprogname() __progname
#endif

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <netinet/in.h>

#include <assert.h>
#include <netdb.h>
#include <time.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <err.h>
#include <errno.h>
#include <poll.h>
#include <stdbool.h>

#ifdef ATF
#include <atf-c.h>

#define ERRX(ev, msg, ...)	ATF_REQUIRE_MSG(0, msg, __VA_ARGS__)
#define ERRX0(ev, msg)		ATF_REQUIRE_MSG(0, msg)

#define SKIPX(ev, msg, ...)	do {			\
	atf_tc_skip(msg, __VA_ARGS__);			\
	return;						\
} while(/*CONSTCOND*/0)

#else
#define ERRX(ev, msg, ...)	errx(ev, msg, __VA_ARGS__)
#define ERRX0(ev, msg)		errx(ev, msg)
#define SKIPX(ev, msg, ...)	errx(ev, msg, __VA_ARGS__)
#endif

static int debug;

#define TOTAL 10
#define PORT_V4MAPPED "6666"
#define HOST_V4MAPPED "::FFFF:239.1.1.1"
#define PORT_V4 "6666"
#define HOST_V4 "239.1.1.1"
#define PORT_V6 "6666"
#define HOST_V6 "FF05:1:0:0:0:0:0:1"

struct message {
	size_t seq;
	struct timespec ts;
};

static int
addmc(int s, struct addrinfo *ai, bool bug)
{
	struct ip_mreq m4;
	struct ipv6_mreq m6;
	struct sockaddr_in *s4;
	struct sockaddr_in6 *s6;
	unsigned int ifc;

	switch (ai->ai_family) {
	case AF_INET:
		s4 = (void *)ai->ai_addr;
		assert(sizeof(*s4) == ai->ai_addrlen);
		m4.imr_multiaddr = s4->sin_addr;
		m4.imr_interface.s_addr = htonl(INADDR_ANY);
		return setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
		    &m4, sizeof(m4));
	case AF_INET6:
		s6 = (void *)ai->ai_addr;
		/*
		 * Linux:	Does not support the v6 ioctls on v4 mapped
		 *		sockets but it does support the v4 ones and
		 *		it works.
		 * MacOS/X:	Supports the v6 ioctls on v4 mapped sockets,
		 *		but does not work and also does not support
		 *		the v4 ioctls. So no way to make multicasting
		 *		work with mapped addresses.
		 * NetBSD:	Supports both and works for both.
		 */
		if (bug && IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr)) {
			memcpy(&m4.imr_multiaddr, &s6->sin6_addr.s6_addr[12],
			    sizeof(m4.imr_multiaddr));
			m4.imr_interface.s_addr = htonl(INADDR_ANY);
			return setsockopt(s, IPPROTO_IP, IP_ADD_MEMBERSHIP,
			    &m4, sizeof(m4));
		}
		assert(sizeof(*s6) == ai->ai_addrlen);
		memset(&m6, 0, sizeof(m6));
#if 0
		ifc = 1;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_LOOP,
		    &ifc, sizeof(ifc)) == -1)
			return -1;
		ifc = 224;
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_HOPS,
		    &ifc, sizeof(ifc)) == -1)
			return -1;
		ifc = 1; /* XXX should pick a proper interface */
		if (setsockopt(s, IPPROTO_IPV6, IPV6_MULTICAST_IF, &ifc,
		    sizeof(ifc)) == -1)
			return -1;
#else
		ifc = 0; /* Let pick an appropriate interface */
#endif
		m6.ipv6mr_interface = ifc;
		m6.ipv6mr_multiaddr = s6->sin6_addr;
		return setsockopt(s, IPPROTO_IPV6, IPV6_JOIN_GROUP,
		    &m6, sizeof(m6));
	default:
		errno = EOPNOTSUPP;
		return -1;
	}
}

static int
allowv4mapped(int s, struct addrinfo *ai)
{
	struct sockaddr_in6 *s6;
	int zero = 0;

	if (ai->ai_family != AF_INET6)
		return 0;

	s6 = (void *)ai->ai_addr;

	if (!IN6_IS_ADDR_V4MAPPED(&s6->sin6_addr))
		return 0;
	return setsockopt(s, IPPROTO_IPV6, IPV6_V6ONLY, &zero, sizeof(zero));
}

static struct sockaddr_storage ss;
static int
connector(int fd, const struct sockaddr *sa, socklen_t slen)
{
	assert(sizeof(ss) > slen);
	memcpy(&ss, sa, slen);
	return 0;
}

static void
show(const char *prefix, const struct message *msg)
{
	printf("%10.10s: %zu [%jd.%ld]\n", prefix, msg->seq, (intmax_t)
	    msg->ts.tv_sec, msg->ts.tv_nsec);
}

static int
getsocket(const char *host, const char *port,
    int (*f)(int, const struct sockaddr *, socklen_t), socklen_t *slen,
    bool bug)
{
	int e, s, lasterrno = 0;
	struct addrinfo hints, *ai0, *ai;
	const char *cause = "?";

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	e = getaddrinfo(host, port, &hints, &ai0);
	if (e)
		ERRX(EXIT_FAILURE, "Can't resolve %s:%s (%s)", host, port,
		    gai_strerror(e));

	s = -1;
	for (ai = ai0; ai; ai = ai->ai_next) {
		s = socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
		if (s == -1) {
			lasterrno = errno;
			cause = "socket";
			continue;
		}
		if (allowv4mapped(s, ai) == -1) {
			cause = "allow v4 mapped";
			goto out;
		}
		if ((*f)(s, ai->ai_addr, ai->ai_addrlen) == -1) {
			cause = f == bind ? "bind" : "connect";
			goto out;
		}
		if ((f == bind || f == connector) && addmc(s, ai, bug) == -1) {
			cause = "join group";
			goto out;
		}
		*slen = ai->ai_addrlen;
		break;
out:
		lasterrno = errno;
		close(s);
		s = -1;
		continue;
	}
	freeaddrinfo(ai0);
	if (s == -1)
		ERRX(EXIT_FAILURE, "%s (%s)", cause, strerror(lasterrno));
	return s;
}

static int
synchronize(const int fd, bool waiter)
{
	int syncmsg = 0;
	int r;
	struct pollfd pfd;

	if (waiter) {
		pfd.fd = fd;
		pfd.events = POLLIN;

		/* We use poll to avoid lock up when the peer died unexpectedly */
		r = poll(&pfd, 1, 10000);
		if (r == -1)
			ERRX(EXIT_FAILURE, "poll (%s)", strerror(errno));
		if (r == 0)
			/* Timed out */
			return -1;

		if (read(fd, &syncmsg, sizeof(syncmsg)) == -1)
			ERRX(EXIT_FAILURE, "read (%s)", strerror(errno));
	} else {
		if (write(fd, &syncmsg, sizeof(syncmsg)) == -1)
			ERRX(EXIT_FAILURE, "write (%s)", strerror(errno));
	}

	return 0;
}

static int
sender(const int fd, const char *host, const char *port, size_t n, bool conn,
    bool bug)
{
	int s;
	ssize_t l;
	struct message msg;

	socklen_t slen;

	s = getsocket(host, port, conn ? connect : connector, &slen, bug);

	/* Wait until receiver gets ready. */
	if (synchronize(fd, true) == -1)
		return -1;

	for (msg.seq = 0; msg.seq < n; msg.seq++) {
#ifdef CLOCK_MONOTONIC
		if (clock_gettime(CLOCK_MONOTONIC, &msg.ts) == -1)
			ERRX(EXIT_FAILURE, "clock (%s)", strerror(errno));
#else
		struct timeval tv;
		if (gettimeofday(&tv, NULL) == -1)
			ERRX(EXIT_FAILURE, "clock (%s)", strerror(errno));
		msg.ts.tv_sec = tv.tv_sec;
		msg.ts.tv_nsec = tv.tv_usec * 1000;
#endif
		if (debug)
			show("sending", &msg);
		l = conn ? send(s, &msg, sizeof(msg), 0) :
		    sendto(s, &msg, sizeof(msg), 0, (void *)&ss, slen);
		if (l == -1)
			ERRX(EXIT_FAILURE, "send (%s)", strerror(errno));
		usleep(100);
	}

	/* Wait until receiver finishes its work. */
	if (synchronize(fd, true) == -1)
		return -1;

	return 0;
}

static void
receiver(const int fd, const char *host, const char *port, size_t n, bool conn,
    bool bug)
{
	int s;
	ssize_t l;
	size_t seq;
	struct message msg;
	struct pollfd pfd;
	socklen_t slen;

	s = getsocket(host, port, bind, &slen, bug);
	pfd.fd = s;
	pfd.events = POLLIN;

	/* Tell I'm ready */
	synchronize(fd, false);

	for (seq = 0; seq < n; seq++) {
		if (poll(&pfd, 1, 10000) == -1)
			ERRX(EXIT_FAILURE, "poll (%s)", strerror(errno));
		l = conn ? recv(s, &msg, sizeof(msg), 0) :
		    recvfrom(s, &msg, sizeof(msg), 0, (void *)&ss, &slen);
		if (l == -1)
			ERRX(EXIT_FAILURE, "recv (%s)", strerror(errno));
		if (debug)
			show("got", &msg);
		if (seq != msg.seq)
			ERRX(EXIT_FAILURE, "seq: expect=%zu actual=%zu",
			    seq, msg.seq);
	}

	/* Tell I'm finished */
	synchronize(fd, false);
}

static void
run(const char *host, const char *port, size_t n, bool conn, bool bug)
{
	pid_t pid;
	int status;
	int syncfds[2];
	int error;

	if (socketpair(AF_UNIX, SOCK_STREAM, 0, syncfds) == -1)
		ERRX(EXIT_FAILURE, "socketpair (%s)", strerror(errno));

	switch ((pid = fork())) {
	case 0:
		receiver(syncfds[0], host, port, n, conn, bug);
		return;
	case -1:
		ERRX(EXIT_FAILURE, "fork (%s)", strerror(errno));
	default:
		error = sender(syncfds[1], host, port, n, conn, bug);
	again:
		switch (waitpid(pid, &status, WNOHANG)) {
		case -1:
			ERRX(EXIT_FAILURE, "wait (%s)", strerror(errno));
		case 0:
			if (error == 0)
				/*
				 * Receiver is still alive, but we know
				 * it will exit soon.
				 */
				goto again;

			if (kill(pid, SIGTERM) == -1)
				ERRX(EXIT_FAILURE, "kill (%s)",
				    strerror(errno));
			goto again;
		default:
			if (WIFSIGNALED(status)) {
				if (WTERMSIG(status) == SIGTERM)
					ERRX0(EXIT_FAILURE,
					    "receiver failed and was killed" \
					    "by sender");
				else
					ERRX(EXIT_FAILURE,
					    "receiver got signaled (%s)",
					    strsignal(WTERMSIG(status)));
			} else if (WIFEXITED(status)) {
				if (WEXITSTATUS(status) != 0)
					ERRX(EXIT_FAILURE,
					    "receiver exited with status %d",
					    WEXITSTATUS(status));
			} else {
				ERRX(EXIT_FAILURE,
				    "receiver exited with unexpected status %d",
				    status);
			}
			break;
		}
		return;
	}
}

#ifndef ATF
int
main(int argc, char *argv[])
{
	const char *host, *port;
	int c;
	size_t n;
	bool conn, bug;

	host = HOST_V4;
	port = PORT_V4;
	n = TOTAL;
	bug = conn = false;

	while ((c = getopt(argc, argv, "46bcdmn:")) != -1)
		switch (c) {
		case '4':
			host = HOST_V4;
			port = PORT_V4;
			break;
		case '6':
			host = HOST_V6;
			port = PORT_V6;
			break;
		case 'b':
			bug = true;
			break;
		case 'c':
			conn = true;
			break;
		case 'd':
			debug++;
			break;
		case 'm':
			host = HOST_V4MAPPED;
			port = PORT_V4MAPPED;
			break;
		case 'n':
			n = atoi(optarg);
			break;
		default:
			fprintf(stderr, "Usage: %s [-cdm46] [-n <tot>]",
			    getprogname());
			return 1;
		}

	run(host, port, n, conn, bug);
	return 0;
}
#else

ATF_TC(conninet4);
ATF_TC_HEAD(conninet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks connected multicast for ipv4");
}

ATF_TC_BODY(conninet4, tc)
{
	run(HOST_V4, PORT_V4, TOTAL, true, false);
}

ATF_TC(connmappedinet4);
ATF_TC_HEAD(connmappedinet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks connected multicast for mapped ipv4");
}

ATF_TC_BODY(connmappedinet4, tc)
{
	run(HOST_V4MAPPED, PORT_V4MAPPED, TOTAL, true, false);
}

ATF_TC(connmappedbuginet4);
ATF_TC_HEAD(connmappedbuginet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks connected multicast for mapped ipv4 using the v4 ioctls");
}

ATF_TC_BODY(connmappedbuginet4, tc)
{
	run(HOST_V4MAPPED, PORT_V4MAPPED, TOTAL, true, true);
}

ATF_TC(conninet6);
ATF_TC_HEAD(conninet6, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks connected multicast for ipv6");
}

ATF_TC_BODY(conninet6, tc)
{
	run(HOST_V6, PORT_V6, TOTAL, true, false);
}

ATF_TC(unconninet4);
ATF_TC_HEAD(unconninet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks unconnected multicast for ipv4");
}

ATF_TC_BODY(unconninet4, tc)
{
	run(HOST_V4, PORT_V4, TOTAL, false, false);
}

ATF_TC(unconnmappedinet4);
ATF_TC_HEAD(unconnmappedinet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks unconnected multicast for mapped ipv4");
}

ATF_TC_BODY(unconnmappedinet4, tc)
{
	run(HOST_V4MAPPED, PORT_V4MAPPED, TOTAL, false, false);
}

ATF_TC(unconnmappedbuginet4);
ATF_TC_HEAD(unconnmappedbuginet4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks unconnected multicast for mapped ipv4 using the v4 ioctls");
}

ATF_TC_BODY(unconnmappedbuginet4, tc)
{
	run(HOST_V4MAPPED, PORT_V4MAPPED, TOTAL, false, true);
}

ATF_TC(unconninet6);
ATF_TC_HEAD(unconninet6, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks unconnected multicast for ipv6");
}

ATF_TC_BODY(unconninet6, tc)
{
	run(HOST_V6, PORT_V6, TOTAL, false, false);
}

ATF_TP_ADD_TCS(tp)
{
	debug++;
	ATF_TP_ADD_TC(tp, conninet4);
	ATF_TP_ADD_TC(tp, connmappedinet4);
	ATF_TP_ADD_TC(tp, connmappedbuginet4);
	ATF_TP_ADD_TC(tp, conninet6);
	ATF_TP_ADD_TC(tp, unconninet4);
	ATF_TP_ADD_TC(tp, unconnmappedinet4);
	ATF_TP_ADD_TC(tp, unconnmappedbuginet4);
	ATF_TP_ADD_TC(tp, unconninet6);

	return atf_no_error();
}
#endif
