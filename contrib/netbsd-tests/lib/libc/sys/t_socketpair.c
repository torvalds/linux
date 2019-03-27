/* $NetBSD: t_socketpair.c,v 1.2 2017/01/13 20:04:52 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
__RCSID("$NetBSD: t_socketpair.c,v 1.2 2017/01/13 20:04:52 christos Exp $");

#include <atf-c.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>

static void
connected(int fd)
{
	struct sockaddr_un addr;
	socklen_t len = (socklen_t)sizeof(addr);
	ATF_REQUIRE(getpeername(fd, (struct sockaddr*)(void *)&addr,
	    &len) == 0);
}

static void
run(int domain, int type, int flags)
{
	int fd[2], i;

	while ((i = open("/", O_RDONLY)) < 3)
		ATF_REQUIRE(i != -1);

#ifdef __FreeBSD__
	closefrom(3);
#else
	ATF_REQUIRE(closefrom(3) != -1);
#endif

	ATF_REQUIRE(socketpair(domain, type | flags, 0, fd) == 0);

	ATF_REQUIRE(fd[0] == 3);
	ATF_REQUIRE(fd[1] == 4);

	connected(fd[0]);
	connected(fd[1]);

	if (flags & SOCK_CLOEXEC) {
		ATF_REQUIRE((fcntl(fd[0], F_GETFD) & FD_CLOEXEC) != 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFD) & FD_CLOEXEC) != 0);
	} else {
		ATF_REQUIRE((fcntl(fd[0], F_GETFD) & FD_CLOEXEC) == 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFD) & FD_CLOEXEC) == 0);
	}

	if (flags & SOCK_NONBLOCK) {
		ATF_REQUIRE((fcntl(fd[0], F_GETFL) & O_NONBLOCK) != 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFL) & O_NONBLOCK) != 0);
	} else {
		ATF_REQUIRE((fcntl(fd[0], F_GETFL) & O_NONBLOCK) == 0);
		ATF_REQUIRE((fcntl(fd[1], F_GETFL) & O_NONBLOCK) == 0);
	}

	ATF_REQUIRE(close(fd[0]) != -1);
	ATF_REQUIRE(close(fd[1]) != -1);
}

ATF_TC(inet);
ATF_TC_HEAD(inet, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "socketpair(2) does not work in the PF_INET domain");
}

ATF_TC_BODY(inet, tc)
{
	int fd[2];

	ATF_REQUIRE_EQ(socketpair(PF_INET, SOCK_DGRAM, 0, fd), -1);
	ATF_REQUIRE_EQ(EOPNOTSUPP, errno);
	ATF_REQUIRE_EQ(socketpair(PF_INET, SOCK_STREAM, 0, fd), -1);
	ATF_REQUIRE_EQ(EOPNOTSUPP, errno);
}

ATF_TC(null_sv);
ATF_TC_HEAD(null_sv, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "socketpair(2) should fail without return storage");
}

ATF_TC_BODY(null_sv, tc)
{
	int fd;

	closefrom(3);
	ATF_REQUIRE_EQ(socketpair(AF_UNIX, SOCK_DGRAM, 0, NULL), -1);
	ATF_REQUIRE_EQ(EFAULT, errno);
	fd = open("/", O_RDONLY);
	ATF_REQUIRE_EQ_MSG(fd, 3,
	    "socketpair(..., NULL) allocated descriptors");
}

ATF_TC(socketpair_basic);
ATF_TC_HEAD(socketpair_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of socketpair(2)");
}

ATF_TC_BODY(socketpair_basic, tc)
{
	run(AF_UNIX, SOCK_DGRAM, 0);
}

ATF_TC(socketpair_nonblock);
ATF_TC_HEAD(socketpair_nonblock, tc)
{
	atf_tc_set_md_var(tc, "descr", "A non-blocking test of socketpair(2)");
}

ATF_TC_BODY(socketpair_nonblock, tc)
{
	run(AF_UNIX, SOCK_DGRAM, SOCK_NONBLOCK);
}

ATF_TC(socketpair_cloexec);
ATF_TC_HEAD(socketpair_cloexec, tc)
{
	atf_tc_set_md_var(tc, "descr", "A close-on-exec of socketpair(2)");
}

ATF_TC_BODY(socketpair_cloexec, tc)
{
	run(AF_UNIX, SOCK_DGRAM, SOCK_CLOEXEC);
}

ATF_TC(socketpair_stream);
ATF_TC_HEAD(socketpair_stream, tc)
{
	atf_tc_set_md_var(tc, "descr", "A stream-oriented socketpair(2)");
}

ATF_TC_BODY(socketpair_stream, tc)
{
	run(AF_UNIX, SOCK_STREAM, 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, inet);
	ATF_TP_ADD_TC(tp, null_sv);
	ATF_TP_ADD_TC(tp, socketpair_basic);
	ATF_TP_ADD_TC(tp, socketpair_nonblock);
	ATF_TP_ADD_TC(tp, socketpair_cloexec);
	ATF_TP_ADD_TC(tp, socketpair_stream);

	return atf_no_error();
}
