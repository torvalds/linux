/* $NetBSD: t_getlogin.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

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
__RCSID("$NetBSD: t_getlogin.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $");

#include <sys/param.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

ATF_TC(getlogin_r_err);
ATF_TC_HEAD(getlogin_r_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from getlogin_r(2)");
}

ATF_TC_BODY(getlogin_r_err, tc)
{
	char small[0];

	ATF_REQUIRE(getlogin_r(small, sizeof(small)) == ERANGE);
}

ATF_TC(getlogin_same);
ATF_TC_HEAD(getlogin_same, tc)
{
	atf_tc_set_md_var(tc, "descr", "getlogin(2) vs. getlogin_r(2)");
}

ATF_TC_BODY(getlogin_same, tc)
{
	char buf[MAXLOGNAME];
	char *str;

	str = getlogin();

	if (str == NULL)
		return;

	ATF_REQUIRE(getlogin_r(buf, sizeof(buf)) == 0);

	if (strcmp(str, buf) != 0)
		atf_tc_fail("getlogin(2) and getlogin_r(2) differ");
}

ATF_TC(setlogin_basic);
ATF_TC_HEAD(setlogin_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that setlogin(2) works");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(setlogin_basic, tc)
{
	char *name;
	pid_t pid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		(void)setsid();

		if (setlogin("foobar") != 0)
			_exit(EXIT_FAILURE);

		name = getlogin();

		if (name == NULL)
			_exit(EXIT_FAILURE);

		if (strcmp(name, "foobar") != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("setlogin(2) failed to set login name");
}

ATF_TC(setlogin_err);
ATF_TC_HEAD(setlogin_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test errors from setlogin(2)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(setlogin_err, tc)
{
	char buf[MAXLOGNAME + 1];
	char *name;
	pid_t pid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	(void)memset(buf, 'x', sizeof(buf));

	if (pid == 0) {

		(void)setsid();

		errno = 0;

		if (setlogin(buf) != -1)
			_exit(EINVAL);

		if (errno != EINVAL)
			_exit(EINVAL);

		errno = 0;

		if (setlogin((void *)-1) != -1)
			_exit(EFAULT);

		if (errno != EFAULT)
			_exit(EFAULT);

		name = getlogin();

		if (name == NULL)
			_exit(EXIT_FAILURE);

		if (strcmp(name, "foobar") == 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS) {

		if (WEXITSTATUS(sta) == EFAULT)
			atf_tc_fail("expected EFAULT, but the call succeeded");

		if (WEXITSTATUS(sta) == EINVAL)
			atf_tc_fail("expected EINVAL, but the call succeeded");

		atf_tc_fail("setlogin(2) failed, but login name was set");
	}
}

ATF_TC(setlogin_perm);
ATF_TC_HEAD(setlogin_perm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test setlogin(2) as normal user");
	atf_tc_set_md_var(tc, "require.user", "unprivileged");
}

ATF_TC_BODY(setlogin_perm, tc)
{
	char *name;
	pid_t pid;
	int sta;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		(void)setsid();

		errno = 0;

		if (setlogin("foobar") != -1)
			_exit(EXIT_FAILURE);

		if (errno != EPERM)
			_exit(EXIT_FAILURE);

		name = getlogin();

		if (name == NULL)
			_exit(EXIT_FAILURE);

		if (strcmp(name, "foobar") == 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("login name was set as an unprivileged user");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getlogin_r_err);
	ATF_TP_ADD_TC(tp, getlogin_same);
	ATF_TP_ADD_TC(tp, setlogin_basic);
	ATF_TP_ADD_TC(tp, setlogin_err);
	ATF_TP_ADD_TC(tp, setlogin_perm);

	return atf_no_error();
}
