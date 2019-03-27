/*	$NetBSD: t_connect.c,v 1.3 2017/01/13 20:09:48 christos Exp $	*/
/*
 * Copyright (c) 2007, 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/socket.h>
#include <err.h>
#include <errno.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <atf-c.h>

ATF_TC(connect_low_port);
ATF_TC_HEAD(connect_low_port, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that low-port allocation "
	    "works");
	atf_tc_set_md_var(tc, "require.user", "root");
}
ATF_TC_BODY(connect_low_port, tc)
{
	struct sockaddr_in sin, sinlist;
	int sd, val, slist;
	socklen_t slen;

	slist = socket(AF_INET, SOCK_STREAM, 0);
	sd = socket(AF_INET, SOCK_STREAM, 0);

	ATF_REQUIRE(sd > 0);
	ATF_REQUIRE(slist > 0);

	/* bind listening socket */
	memset(&sinlist, 0, sizeof(sinlist));
	sinlist.sin_family = AF_INET;
	sinlist.sin_port = htons(31522);
	sinlist.sin_addr.s_addr = inet_addr("127.0.0.1");

	ATF_REQUIRE_EQ(bind(slist,
	    (struct sockaddr *)&sinlist, sizeof(sinlist)), 0);
	ATF_REQUIRE_EQ(listen(slist, 1), 0);

	val = IP_PORTRANGE_LOW;
	if (setsockopt(sd, IPPROTO_IP, IP_PORTRANGE, &val,
	    sizeof(val)) == -1)
		atf_tc_fail("setsockopt failed: %s", strerror(errno));

	memset(&sin, 0, sizeof(sin));

	sin.sin_port = htons(31522);
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");
	sin.sin_family = AF_INET;

	if (connect(sd, (struct sockaddr *)&sin, sizeof(sin)) == -1) {
		int serrno = errno;
		atf_tc_fail("connect failed: %s%s",
		    strerror(serrno),
		    serrno != EACCES ? "" :
		    " (see http://mail-index.netbsd.org/"
		    "source-changes/2007/12/16/0011.html)");
	}

	slen = sizeof(sin);
	ATF_REQUIRE_EQ(getsockname(sd, (struct sockaddr *)&sin, &slen), 0);
	ATF_REQUIRE_EQ(slen, sizeof(sin));
	ATF_REQUIRE(ntohs(sin.sin_port) <= IPPORT_RESERVEDMAX);

	close(sd);
	close(slist);
}

ATF_TC(connect_foreign_family);
ATF_TC_HEAD(connect_foreign_family, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that connecting a socket "
	    "with a different address family fails");
}
ATF_TC_BODY(connect_foreign_family, tc)
{
	struct sockaddr_in addr;

	/* addr.sin_family = AF_UNSPEC = 0 */
	memset(&addr, 0, sizeof(addr));

	/*
	 * it is not necessary to initialize sin_{addr,port} since
	 * those structure members shall not be accessed if connect
	 * fails correctly.
	 */

	int sock = socket(AF_LOCAL, SOCK_STREAM, 0);
	ATF_REQUIRE(sock != -1);

	ATF_REQUIRE(-1 == connect(sock, (struct sockaddr *)&addr, sizeof(addr)));
	ATF_REQUIRE(EAFNOSUPPORT == errno);

	close(sock);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, connect_low_port);
	ATF_TP_ADD_TC(tp, connect_foreign_family);

	return atf_no_error();
}
