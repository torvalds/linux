/* $NetBSD: t_sigsuspend.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_sigsuspend.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

/*
 * Regression test for sigsuspend in libpthread when pthread lib isn't
 * initialized.
 *
 * Written by Love  FIXME strand <lha@NetBSD.org>, March 2003.
 * Public domain.
 */

#include <stdio.h>
#include <signal.h>
#include <unistd.h>

#include <atf-c.h>

int alarm_set;

static void
alarm_handler(int signo)
{
	alarm_set = 1;
}

ATF_TC(sigsuspend);
ATF_TC_HEAD(sigsuspend, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks sigsuspend in libpthread when pthread lib isn't initialized");
}
ATF_TC_BODY(sigsuspend, tc)
{
	struct sigaction sa;
	sigset_t set;

	sa.sa_flags = 0;
	sa.sa_handler = alarm_handler;
	sigemptyset(&sa.sa_mask);

	sigaction(SIGALRM, &sa, NULL);

	alarm(1);

	sigemptyset(&set);
	sigaddset(&set, SIGUSR1);

	sigsuspend(&set);

	alarm(0);

	ATF_REQUIRE(alarm_set);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, sigsuspend);

	return atf_no_error();
}
