/*	$NetBSD: t_signals.c,v 1.3 2017/01/13 21:30:43 christos Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/types.h>
#include <sys/wait.h>

#include <atf-c.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>

#include <rump/rump.h>

#include "../kernspace/kernspace.h"
#include "h_macros.h"

ATF_TC(sigraise);
ATF_TC_HEAD(sigraise, tc)
{

	atf_tc_set_md_var(tc, "descr", "RUMP_SIGMODEL_RAISE");
}

ATF_TC(sigignore);
ATF_TC_HEAD(sigignore, tc)
{

	atf_tc_set_md_var(tc, "descr", "RUMP_SIGMODEL_IGNORE");
}

ATF_TC(sigpanic);
ATF_TC_HEAD(sigpanic, tc)
{

	atf_tc_set_md_var(tc, "descr", "RUMP_SIGMODEL_PANIC");
}

static volatile sig_atomic_t sigcnt;
static void
thehand(int sig)
{

	sigcnt++;
}

ATF_TC_BODY(sigraise, tc)
{

	signal(SIGUSR2, thehand);
	rump_boot_setsigmodel(RUMP_SIGMODEL_RAISE);

	rump_init();
	rump_schedule();
	rumptest_localsig(SIGUSR2);
	rump_unschedule();
	ATF_REQUIRE_EQ(sigcnt, 1);
}

ATF_TC_BODY(sigignore, tc)
{

	rump_boot_setsigmodel(RUMP_SIGMODEL_IGNORE);

	rump_init();
	rump_schedule();
	rumptest_localsig(SIGKILL);
	rump_unschedule();
}

ATF_TC_BODY(sigpanic, tc)
{
	int status;

	rump_boot_setsigmodel(RUMP_SIGMODEL_PANIC);

	switch (fork()) {
	case 0:
		rump_init();
		rump_schedule();
		rumptest_localsig(SIGCONT);
		/* NOTREACHED */
		exit(1);
	default:
		wait(&status);
		ATF_REQUIRE(WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT);
		break;
	case -1:
		atf_tc_fail_errno("fork");
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sigraise);
	ATF_TP_ADD_TC(tp, sigignore);
	ATF_TP_ADD_TC(tp, sigpanic);

	return atf_no_error();
}
