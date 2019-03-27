/*	$NetBSD: t_fd.c,v 1.6 2017/01/13 21:30:41 christos Exp $	*/

/*
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>

#include <netinet/in.h>
#include <arpa/inet.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>

#include <rump/rumpclient.h>
#include <rump/rump_syscalls.h>

#include "h_macros.h"

ATF_TC_WITH_CLEANUP(bigenough);
ATF_TC_HEAD(bigenough, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that rumpclient uses "
	    "fd > 2");
}
ATF_TC_WITH_CLEANUP(sigio);
ATF_TC_HEAD(sigio, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check that rump client receives "
	    "SIGIO");
}

#define RUMPSERV "unix://sucket"

ATF_TC_CLEANUP(bigenough, tc){system("env RUMP_SERVER=" RUMPSERV " rump.halt");}
ATF_TC_CLEANUP(sigio, tc) { system("env RUMP_SERVER=" RUMPSERV " rump.halt"); }

ATF_TC_BODY(bigenough, tc)
{
	struct stat sb;

	RZ(system("rump_server " RUMPSERV));
	RL(setenv("RUMP_SERVER", RUMPSERV, 1));

	RL(dup2(0, 10));
	RL(dup2(1, 11));
	RL(dup2(2, 12));

	RL(close(0));
	RL(close(1));
	RL(close(2));

	RL(rumpclient_init());
	RL(rump_sys_getpid());

	ATF_REQUIRE_ERRNO(EBADF, fstat(0, &sb) == -1);
	ATF_REQUIRE_ERRNO(EBADF, fstat(1, &sb) == -1);
	ATF_REQUIRE_ERRNO(EBADF, fstat(2, &sb) == -1);

	RL(rump_sys_getpid());

	/* restore these.  does it help? */
	dup2(10, 0);
	dup2(11, 1);
	dup2(12, 2);
}

static volatile sig_atomic_t sigcnt;
static void
gotsig(int sig)
{

	sigcnt++;
}

ATF_TC_BODY(sigio, tc)
{
	struct sockaddr_in sin;
	int ls;
	int cs;
	int fl;
	int sc;

	signal(SIGIO, gotsig);
	RZ(system("rump_server -lrumpnet -lrumpnet_net -lrumpnet_netinet "
	    "-lrumpdev -lrumpvfs " RUMPSERV));
	RL(setenv("RUMP_SERVER", RUMPSERV, 1));

	RL(rumpclient_init());
	RL(ls = rump_sys_socket(PF_INET, SOCK_STREAM, 0));

	RL(rump_sys_fcntl(ls, F_SETOWN, rump_sys_getpid()));
	RL(fl = rump_sys_fcntl(ls, F_GETFL));
	RL(rump_sys_fcntl(ls, F_SETFL, fl | O_ASYNC));

	memset(&sin, 0, sizeof(sin));
	sin.sin_len = sizeof(sin);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(12345);
	RL(rump_sys_bind(ls, (struct sockaddr *)&sin, sizeof(sin)));
	RL(rump_sys_listen(ls, 5));

	RL(cs = rump_sys_socket(PF_INET, SOCK_STREAM, 0));
	sin.sin_addr.s_addr = inet_addr("127.0.0.1");

	ATF_REQUIRE_EQ(sigcnt, 0);
	RL(rump_sys_connect(cs, (struct sockaddr *)&sin, sizeof(sin)));
	sc = sigcnt;
	printf("sigcnt after connect: %d\n", sc);
	ATF_REQUIRE(sc >= 1);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, bigenough);
	ATF_TP_ADD_TC(tp, sigio);

	return atf_no_error();
}
