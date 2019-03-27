/* $NetBSD: t_alarm.c,v 1.2 2011/05/10 06:58:17 jruoho Exp $ */

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
__RCSID("$NetBSD: t_alarm.c,v 1.2 2011/05/10 06:58:17 jruoho Exp $");

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

ATF_TC(alarm_basic);
ATF_TC_HEAD(alarm_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of alarm(3)");
}

ATF_TC_BODY(alarm_basic, tc)
{

	ATF_REQUIRE(signal(SIGALRM, handler) == 0);

	fail = true;

	(void)alarm(1);
	(void)sleep(2);

	if (fail != false)
		atf_tc_fail("alarm(3) failed to deliver signal");
}

ATF_TC(alarm_fork);
ATF_TC_HEAD(alarm_fork, tc)
{
	atf_tc_set_md_var(tc, "descr", "Does fork(2) clear a pending alarm?");
}

ATF_TC_BODY(alarm_fork, tc)
{
	unsigned int rv;
	pid_t pid;
	int sta;

	/*
	 * Any pending alarms should be
	 * cleared in the child process.
	 */
	(void)alarm(60);

	pid = fork();
	ATF_REQUIRE(pid >= 0);

	if (pid == 0) {

		rv = alarm(0);

		if (rv != 0)
			_exit(EXIT_FAILURE);

		_exit(EXIT_SUCCESS);
	}

	(void)alarm(0);
	(void)wait(&sta);

	if (WIFEXITED(sta) == 0 || WEXITSTATUS(sta) != EXIT_SUCCESS)
		atf_tc_fail("pending alarm was not cleared for child");
}

ATF_TC(alarm_previous);
ATF_TC_HEAD(alarm_previous, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test return value from alarm(3)");
}

ATF_TC_BODY(alarm_previous, tc)
{
	unsigned int rv;

	/*
	 * See that alarm(3) returns the amount
	 * left on the timer from the previous call.
	*/
	rv = alarm(60);

	if (rv != 0)
		goto fail;

	rv = alarm(0);

	if (rv < 50)
		goto fail;

	(void)alarm(0);

	return;

fail:
	atf_tc_fail("invalid return value from alarm(3)");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, alarm_basic);
	ATF_TP_ADD_TC(tp, alarm_fork);
	ATF_TP_ADD_TC(tp, alarm_previous);

	return atf_no_error();
}
