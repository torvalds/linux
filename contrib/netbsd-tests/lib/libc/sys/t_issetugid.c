/* $NetBSD: t_issetugid.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

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
__RCSID("$NetBSD: t_issetugid.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <pwd.h>
#include <stdlib.h>
#include <unistd.h>

static bool check(int (*fuid)(uid_t), int (*fgid)(gid_t));

static bool
check(int (*fuid)(uid_t), int (*fgid)(gid_t))
{
	struct passwd *pw;
	pid_t pid;
	int sta;

	pw = getpwnam("nobody");

	if (pw == NULL)
		return false;

	pid = fork();

	if (pid < 0)
		return false;

	if (pid == 0) {

		if (fuid != NULL && (*fuid)(pw->pw_uid) != 0)
			_exit(EXIT_FAILURE);

		if (fgid != NULL && (*fgid)(pw->pw_gid) != 0)
			_exit(EXIT_FAILURE);

		if (issetugid() != 1)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		return false;

	return true;
}

ATF_TC(issetugid_egid);
ATF_TC_HEAD(issetugid_egid, tc)
{
	atf_tc_set_md_var(tc, "descr", "A test of issetugid(2), eff. GID");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(issetugid_egid, tc)
{

	if (check(NULL, setegid) != true)
		atf_tc_fail("issetugid(2) failed with effective GID");
}

ATF_TC(issetugid_euid);
ATF_TC_HEAD(issetugid_euid, tc)
{
	atf_tc_set_md_var(tc, "descr", "A test of issetugid(2), eff. UID");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(issetugid_euid, tc)
{

	if (check(seteuid, NULL) != true)
		atf_tc_fail("issetugid(2) failed with effective UID");
}

ATF_TC(issetugid_rgid);
ATF_TC_HEAD(issetugid_rgid, tc)
{
	atf_tc_set_md_var(tc, "descr", "A test of issetugid(2), real GID");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(issetugid_rgid, tc)
{

	if (check(NULL, setgid) != true)
		atf_tc_fail("issetugid(2) failed with real GID");
}

ATF_TC(issetugid_ruid);
ATF_TC_HEAD(issetugid_ruid, tc)
{
	atf_tc_set_md_var(tc, "descr", "A test of issetugid(2), real UID");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(issetugid_ruid, tc)
{

	if (check(setuid, NULL) != true)
		atf_tc_fail("issetugid(2) failed with real UID");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, issetugid_egid);
	ATF_TP_ADD_TC(tp, issetugid_euid);
	ATF_TP_ADD_TC(tp, issetugid_rgid);
	ATF_TP_ADD_TC(tp, issetugid_ruid);

	return atf_no_error();
}
