/* $NetBSD: t_siglongjmp.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $ */

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
__RCSID("$NetBSD: t_siglongjmp.c,v 1.1 2010/07/16 15:42:53 jmmv Exp $");

/*
 * Regression test for siglongjmp out of a signal handler back into
 * its thread.
 *
 * Written by Christian Limpach <cl@NetBSD.org>, December 2003.
 * Public domain.
 */

#include <sys/resource.h>

#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <unistd.h>

#include <atf-c.h>

#include "h_common.h"

static sigjmp_buf env;

static void *
thread(void *arg)
{
	return NULL;
}

static void
handler(int sig, siginfo_t *info, void *ctx)
{
	siglongjmp(env, 1);
}

ATF_TC(siglongjmp1);
ATF_TC_HEAD(siglongjmp1, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks siglongjmp() out of a signal handler back into its thread");
}
ATF_TC_BODY(siglongjmp1, tc)
{
	pthread_t t;
	sigset_t nset;
	struct rlimit rlim;
	struct sigaction act;

	rlim.rlim_cur = rlim.rlim_max = 0;
	(void)setrlimit(RLIMIT_CORE, &rlim);

	PTHREAD_REQUIRE(pthread_create(&t, NULL, thread, NULL));

	sigemptyset(&nset);
	sigaddset(&nset, SIGUSR1);
	PTHREAD_REQUIRE(pthread_sigmask(SIG_SETMASK, &nset, NULL));

	act.sa_sigaction = handler;
	sigemptyset(&act.sa_mask);
	sigaddset(&act.sa_mask, SIGUSR2);
	act.sa_flags = 0;
	sigaction(SIGSEGV, &act, NULL);

	ATF_REQUIRE_EQ(sigsetjmp(env, 1), 0);

	PTHREAD_REQUIRE(pthread_sigmask(0, NULL, &nset));

	ATF_REQUIRE_EQ_MSG(sigismember(&nset, SIGSEGV), 0, "SIGSEGV set");
	ATF_REQUIRE_EQ_MSG(sigismember(&nset, SIGUSR2), 0, "SIGUSR2 set");
	ATF_REQUIRE_MSG(sigismember(&nset, SIGUSR1), "SIGUSR1 not set");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, siglongjmp1);

	return atf_no_error();
}
