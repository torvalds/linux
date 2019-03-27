/* $NetBSD: t_closefrom.c,v 1.4 2011/05/11 08:11:36 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_closefrom.c,v 1.4 2011/05/11 08:11:36 jruoho Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <unistd.h>

static const char  path[] = "closefrom";

ATF_TC_WITH_CLEANUP(closefrom_basic);
ATF_TC_HEAD(closefrom_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of closefrom(3), #1");
}

ATF_TC_BODY(closefrom_basic, tc)
{
	int fd, cur1, cur2;

	(void)closefrom(STDERR_FILENO + 1);

	fd = open(path, O_RDONLY | O_CREAT, 0400);
	ATF_REQUIRE(fd >= 0);

	cur1 = fcntl(0, F_MAXFD);

	ATF_REQUIRE(cur1 == STDERR_FILENO + 1);
	ATF_REQUIRE(closefrom(cur1) == 0);

	cur2 = fcntl(0, F_MAXFD);

	ATF_REQUIRE(cur1 - 1 == cur2);
	ATF_REQUIRE(close(fd) == -1);
	ATF_REQUIRE(unlink(path) == 0);
}

ATF_TC_CLEANUP(closefrom_basic, tc)
{
	(void)unlink(path);
}

ATF_TC_WITH_CLEANUP(closefrom_buffer);
ATF_TC_HEAD(closefrom_buffer, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of closefrom(3), #2");
}

ATF_TC_BODY(closefrom_buffer, tc)
{
	int buf[16], cur, half;
	size_t i;

	/*
	 * Open a buffer of descriptors, close the half of
	 * these and verify that the result is consistent.
	 */
	ATF_REQUIRE(closefrom(STDERR_FILENO + 1) == 0);

	cur = fcntl(0, F_MAXFD);
	ATF_REQUIRE(cur == STDERR_FILENO);

	for (i = 0; i < __arraycount(buf); i++) {
		buf[i] = open(path, O_RDWR | O_CREAT, 0600);
		ATF_REQUIRE(buf[i] >= 0);
	}

	cur = fcntl(0, F_MAXFD);
	ATF_REQUIRE(cur == __arraycount(buf) + STDERR_FILENO);

	half = STDERR_FILENO + __arraycount(buf) / 2;
	ATF_REQUIRE(closefrom(half) == 0);

	cur = fcntl(0, F_MAXFD);
	ATF_REQUIRE(cur == half - 1);

	for (i = 0; i < __arraycount(buf); i++)
		(void)close(buf[i]);
}

ATF_TC_CLEANUP(closefrom_buffer, tc)
{
	(void)unlink(path);
}

ATF_TC(closefrom_err);
ATF_TC_HEAD(closefrom_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from closefrom(3)");
}

ATF_TC_BODY(closefrom_err, tc)
{

	errno = 0;
	ATF_REQUIRE_ERRNO(EBADF, closefrom(-INT_MAX) == -1);
}

ATF_TC(closefrom_one);
ATF_TC_HEAD(closefrom_one, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test closefrom(1)");
}

ATF_TC_BODY(closefrom_one, tc)
{
	pid_t pid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (closefrom(1) != 0)
			_exit(10);

		_exit(fcntl(0, F_MAXFD));
	}


	(void)wait(&sta);

	/*
	 * STDIN_FILENO sould still be open; WEXITSTATUS(1) == 0.
	 */
	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != 0)
		atf_tc_fail("not all descriptors were closed");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, closefrom_basic);
	ATF_TP_ADD_TC(tp, closefrom_buffer);
	ATF_TP_ADD_TC(tp, closefrom_err);
	ATF_TP_ADD_TC(tp, closefrom_one);

	return atf_no_error();
}
