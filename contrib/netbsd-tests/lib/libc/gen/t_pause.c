/* $NetBSD: t_pause.c,v 1.1 2011/05/10 13:03:06 jruoho Exp $ */

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
__RCSID("$NetBSD: t_pause.c,v 1.1 2011/05/10 13:03:06 jruoho Exp $");

#include <sys/wait.h>

#include <atf-c.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>

static bool	fail;
static void	handler(int);

static void
handler(int signo)
{

	if (signo == SIGALRM)
		fail = false;
}

ATF_TC(pause_basic);
ATF_TC_HEAD(pause_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of pause(3), #1");
}

ATF_TC_BODY(pause_basic, tc)
{

	fail = true;

	ATF_REQUIRE(signal(SIGALRM, handler) == 0);

	(void)alarm(1);

	if (pause() != -1 || fail != false)
		atf_tc_fail("pause(3) did not cancel out from a signal");
}

ATF_TC(pause_kill);
ATF_TC_HEAD(pause_kill, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of pause(3), #2");
}

ATF_TC_BODY(pause_kill, tc)
{
	pid_t pid;
	int sta;

	fail = true;

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		(void)pause();

		_exit(EXIT_SUCCESS);
	}

	(void)sleep(1);

	if (fail != true)
		atf_tc_fail("child terminated before signal");

	(void)kill(pid, SIGKILL);
	(void)sleep(1);
	(void)wait(&sta);

	if (WIFSIGNALED(sta) == 0 || WTERMSIG(sta) != SIGKILL)
		atf_tc_fail("pause(3) did not cancel from SIGKILL");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, pause_basic);
	ATF_TP_ADD_TC(tp, pause_kill);

	return atf_no_error();
}
