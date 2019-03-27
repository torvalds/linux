/* $NetBSD: t_getsid.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $ */

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
__RCSID("$NetBSD: t_getsid.c,v 1.1 2011/07/07 06:57:53 jruoho Exp $");

#include <sys/wait.h>

#include <errno.h>
#include <stdlib.h>
#include <unistd.h>

#include <atf-c.h>

ATF_TC(getsid_current);
ATF_TC_HEAD(getsid_current, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test getsid(0)");
}

ATF_TC_BODY(getsid_current, tc)
{
	pid_t sid;

	sid = getsid(0);
	ATF_REQUIRE(sid != -1);

	if (sid != getsid(getpid()))
		atf_tc_fail("getsid(0) did not match the calling process");
}

ATF_TC(getsid_err);
ATF_TC_HEAD(getsid_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test error conditions in getsid(2)");
}

ATF_TC_BODY(getsid_err, tc)
{

	errno = 0;

	ATF_REQUIRE(getsid(-1) == -1);
	ATF_REQUIRE(errno == ESRCH);
}

ATF_TC(getsid_process);
ATF_TC_HEAD(getsid_process, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test getsid(2) with processes");
}

ATF_TC_BODY(getsid_process, tc)
{
	pid_t csid, pid, ppid, sid;
	int sta;

	sid = getsid(0);
	pid = fork();

	ATF_REQUIRE(pid >= 0);
	ATF_REQUIRE(sid != -1);

	if (pid == 0) {

		csid = getsid(0);
		ppid = getppid();

		if (sid != csid)
			_exit(EXIT_FAILURE);

		if (getsid(ppid) != csid)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("invalid session ID");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, getsid_current);
	ATF_TP_ADD_TC(tp, getsid_err);
	ATF_TP_ADD_TC(tp, getsid_process);

	return atf_no_error();
}
