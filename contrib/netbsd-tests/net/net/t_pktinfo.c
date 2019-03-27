/*	$NetBSD: t_pktinfo.c,v 1.2 2013/10/19 17:45:01 christos Exp $	*/

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_pktinfo.c,v 1.2 2013/10/19 17:45:01 christos Exp $");

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static const char traffic[] = "foo";

#ifdef TEST
#include <err.h>
#define ERR(msg) err(EXIT_FAILURE, msg)
#define ERRX(msg, a) errx(EXIT_FAILURE, msg, a)
#define ERRX2(msg, a1, a2) errx(EXIT_FAILURE, msg, a1, a2)
#else
#include <atf-c.h>
#define ERR(msg) ATF_REQUIRE_MSG(0, "%s: %s", msg, strerror(errno))
#define ERRX(msg, a) ATF_REQUIRE_MSG(0, msg, a)
#define ERRX2(msg, a1, a2) ATF_REQUIRE_MSG(0, msg, a1, a2)
#endif

static int
server(struct sockaddr_in *sin) {
	int s, one;
	socklen_t len = sizeof(*sin);

	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		ERR("socket");

	memset(sin, 0, len);
	sin->sin_family = AF_INET;
	sin->sin_len = len;
	sin->sin_port = 0;
	sin->sin_addr.s_addr = htonl(INADDR_LOOPBACK);

	if (bind(s, (const struct sockaddr *)sin, len) == -1)
		ERR("bind");

	if (getsockname(s, (struct sockaddr *)sin, &len) == -1)
		ERR("getsockname");

	one = 1;
	if (setsockopt(s, IPPROTO_IP, IP_PKTINFO, &one, sizeof(one)) == -1)
		ERR("setsockopt");
	if (setsockopt(s, IPPROTO_IP, IP_RECVPKTINFO, &one, sizeof(one)) == -1)
		ERR("setsockopt");

	return s;
}

static int
client(struct sockaddr_in *sin) {
	int s;

	if ((s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) == -1)
		ERR("socket");
	if (sendto(s, traffic, sizeof(traffic), 0,
	    (const struct sockaddr *)sin, sizeof(*sin)) == -1)
		ERR("sendto");
	return s;
}

static void
receive(int s) {
	struct msghdr msg;
	struct cmsghdr *cmsg;
	struct iovec iov;
	char buf[sizeof(traffic)];
	struct in_pktinfo *ipi;
	char control[CMSG_SPACE(sizeof(*ipi)) * 2];

	memset(&msg, 0, sizeof(msg));
	msg.msg_name = NULL;
	msg.msg_namelen = 0;
	msg.msg_iov = &iov;
	memset(&iov, 0, sizeof(iov));
	iov.iov_base = buf;
	iov.iov_len = sizeof(buf);
	msg.msg_iovlen = 1;
	msg.msg_control = control;
	msg.msg_controllen = sizeof(control);
	msg.msg_flags = 0;
	
	if (recvmsg(s, &msg, 0) == -1)
		ERR("recvmsg");

	for (cmsg = CMSG_FIRSTHDR(&msg); cmsg != NULL;
	    cmsg = CMSG_NXTHDR(&msg, cmsg)) {
		if (cmsg->cmsg_level != IPPROTO_IP)
			ERRX("bad level %d", cmsg->cmsg_level);
		const char *m;
		switch (cmsg->cmsg_type) {
		case IP_PKTINFO:
			m = "pktinfo";
			break;
		case IP_RECVPKTINFO:
			m = "recvpktinfo";
			break;
		default:
			m = NULL;
			ERRX("bad type %d", cmsg->cmsg_type);
		}
		ipi = (void *)CMSG_DATA(cmsg);
#ifdef TEST
		printf("%s message received on address %s at interface %d\n",
		    m, inet_ntoa(ipi->ipi_addr), ipi->ipi_ifindex);
#else
		__USE(m);
		ATF_REQUIRE_MSG(ipi->ipi_addr.s_addr == htonl(INADDR_LOOPBACK),
			"address 0x%x != 0x%x", ipi->ipi_addr.s_addr,
			htonl(INADDR_LOOPBACK));
#endif
	}

	if (strcmp(traffic, buf) != 0)
		ERRX2("Bad message '%s' != '%s'", buf, traffic);
}

static void
doit(void)
{
	struct sockaddr_in sin;
	int s, c;
	s = server(&sin);
	c = client(&sin);
	receive(s);
	close(s);
	close(c);
}

#ifndef TEST
ATF_TC(pktinfo);
ATF_TC_HEAD(pktinfo, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that IP_PKTINFO and "
	    "IP_RECVPKTINFO work");
}

ATF_TC_BODY(pktinfo, tc)
{
	doit();
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, pktinfo);
	return atf_no_error();
}
#else

int
main(int argc, char *argv[]) {
	doit();
	return 0;
}
#endif
