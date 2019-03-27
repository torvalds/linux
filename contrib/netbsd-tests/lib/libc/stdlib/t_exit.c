/* $NetBSD: t_exit.c,v 1.1 2011/05/09 07:31:51 jruoho Exp $ */

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
__RCSID("$NetBSD: t_exit.c,v 1.1 2011/05/09 07:31:51 jruoho Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool	fail;
static void	func(void);

static void
func(void)
{
	fail = false;
}

ATF_TC(exit_atexit);
ATF_TC_HEAD(exit_atexit, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of atexit(3)");
}

ATF_TC_BODY(exit_atexit, tc)
{
	pid_t pid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		if (atexit(func) != 0)
			_exit(EXIT_FAILURE);

		fail = true;

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("atexit(3) failed");

	if (fail != false)
		atf_tc_fail("atexit(3) was not called");
}

ATF_TC(exit_basic);
ATF_TC_HEAD(exit_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of exit(3)");
}

ATF_TC_BODY(exit_basic, tc)
{
	pid_t pid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {
		exit(EXIT_SUCCESS);
		exit(EXIT_FAILURE);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("exit(3) did not exit successfully");
}

ATF_TC(exit_status);
ATF_TC_HEAD(exit_status, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test exit(3) status");
}

ATF_TC_BODY(exit_status, tc)
{
	const int n = 10;
	int i, sta;
	pid_t pid;

	for (i = 0; i < n; i++) {

		pid = fork();

		if (pid < 0)
			exit(EXIT_FAILURE);

		if (pid == 0)
			exit(i);

		(void)wait(&sta);

		if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != i)
			atf_tc_fail("invalid exit(3) status");
	}
}

ATF_TC(exit_tmpfile);
ATF_TC_HEAD(exit_tmpfile, tc)
{
	atf_tc_set_md_var(tc, "descr", "Temporary files are unlinked?");
}

ATF_TC_BODY(exit_tmpfile, tc)
{
	int sta, fd = -1;
	char buf[12];
	pid_t pid;
	FILE *f;

	(void)strlcpy(buf, "exit.XXXXXX", sizeof(buf));

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		fd = mkstemp(buf);

		if (fd < 0)
			exit(EXIT_FAILURE);

		exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("failed to create temporary file");

	f = fdopen(fd, "r");

	if (f != NULL)
		atf_tc_fail("exit(3) did not clear temporary file");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, exit_atexit);
	ATF_TP_ADD_TC(tp, exit_basic);
	ATF_TP_ADD_TC(tp, exit_status);
	ATF_TP_ADD_TC(tp, exit_tmpfile);

	return atf_no_error();
}
