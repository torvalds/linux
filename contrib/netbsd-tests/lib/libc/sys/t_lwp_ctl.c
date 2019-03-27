/* $NetBSD: t_lwp_ctl.c,v 1.2 2012/03/18 06:20:51 jruoho Exp $ */

/*-
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
__RCSID("$NetBSD: t_lwp_ctl.c,v 1.2 2012/03/18 06:20:51 jruoho Exp $");

#include <sys/lwpctl.h>

#include <atf-c.h>
#include <lwp.h>
#include <stdio.h>
#include <time.h>

ATF_TC(lwpctl_counter);
ATF_TC_HEAD(lwpctl_counter, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks lwpctl preemption counter");
}

ATF_TC_BODY(lwpctl_counter, tc)
{
	lwpctl_t *lc;
	struct timespec ts;
	int ctr1, ctr2;

	ATF_REQUIRE(_lwp_ctl(LWPCTL_FEATURE_PCTR, &lc) == 0);

	/* Ensure that preemption is reported. */
	ctr1 = lc->lc_pctr;
	(void)printf("pctr = %d\n", ctr1);
	ts.tv_nsec = 10*1000000;
	ts.tv_sec = 0;

	ATF_REQUIRE(nanosleep(&ts, 0) != -1);

	ctr2 = lc->lc_pctr;
	(void)printf("pctr = %d\n", ctr2);

	ATF_REQUIRE_MSG(ctr1 != ctr2, "counters match");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, lwpctl_counter);

	return atf_no_error();
}
