/* $NetBSD: t_sigaction.c,v 1.5 2017/01/13 21:30:41 christos Exp $ */

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sigaction.c,v 1.5 2017/01/13 21:30:41 christos Exp $");

#include <sys/wait.h>

#include <signal.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_macros.h"

static bool handler_called = false;

static void
handler(int signo __unused)
{
    handler_called = true;
}

static void
sa_resethand_child(const int flags)
{
    struct sigaction sa;

    sa.sa_flags = flags;
    sa.sa_handler = &handler;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGUSR1, &sa, NULL);
    kill(getpid(), SIGUSR1);
    exit(handler_called ? EXIT_SUCCESS : EXIT_FAILURE);
}

static void
wait_and_check_child(const pid_t pid, const char *fail_message)
{
	int status;

	(void)waitpid(pid, &status, 0);

	if (WIFEXITED(status))
		ATF_CHECK_EQ(EXIT_SUCCESS, WEXITSTATUS(status));
	else
		atf_tc_fail("%s; raw exit status was %d", fail_message, status);
}

static void
catch(int sig __unused)
{
	return;
}

ATF_TC(sigaction_basic);
ATF_TC_HEAD(sigaction_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Checks for correct I&D cache"
	    "synchronization after copying out the trampoline code.");
}

ATF_TC_BODY(sigaction_basic, tc)
{
	static struct sigaction sa;

	sa.sa_handler = catch;

	sigaction(SIGUSR1, &sa, 0);
	kill(getpid(), SIGUSR1);
	atf_tc_pass();
}

ATF_TC(sigaction_noflags);
ATF_TC_HEAD(sigaction_noflags, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks programming a signal with "
	    "sigaction(2) but without any flags");
}

ATF_TC_BODY(sigaction_noflags, tc)
{
	const pid_t pid = fork();
	if (pid == -1)
		atf_tc_fail_errno("fork(2) failed");
	else if (pid == 0)
		sa_resethand_child(0);
	else
		wait_and_check_child(pid, "Child process did not exit cleanly;"
		    " it failed to process the signal");
}

ATF_TC(sigaction_resethand);
ATF_TC_HEAD(sigaction_resethand, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks that SA_RESETHAND works");
}

ATF_TC_BODY(sigaction_resethand, tc)
{
	const pid_t pid = fork();
	if (pid == -1)
		atf_tc_fail_errno("fork(2) failed");
	else if (pid == 0)
		sa_resethand_child(SA_RESETHAND);
	else {
		wait_and_check_child(pid, "Child process did not exit cleanly;"
		    " it either failed to process the signal or SA_RESETHAND"
		    " is broken");
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sigaction_basic);
	ATF_TP_ADD_TC(tp, sigaction_noflags);
	ATF_TP_ADD_TC(tp, sigaction_resethand);

	return atf_no_error();
}
