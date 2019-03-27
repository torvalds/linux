/* $NetBSD: t_sched.c,v 1.5 2012/03/25 04:11:42 christos Exp $ */

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
__RCSID("$NetBSD: t_sched.c,v 1.5 2012/03/25 04:11:42 christos Exp $");

#include <sched.h>
#include <limits.h>
#include <unistd.h>

#include <atf-c.h>

static void	 sched_priority_set(int, int);

ATF_TC(sched_getparam);
ATF_TC_HEAD(sched_getparam, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of sched_getparam(3)");
}

ATF_TC_BODY(sched_getparam, tc)
{
	struct sched_param s1, s2;
	pid_t p = getpid();

	/*
	 * IEEE Std 1003.1-2008: if the supplied pid is zero,
	 * the parameters for the calling process are returned.
	 */
	ATF_REQUIRE(sched_getparam(0, &s1) == 0);
	ATF_REQUIRE(sched_getparam(p, &s2) == 0);

	ATF_CHECK_EQ(s1.sched_priority, s2.sched_priority);

	/*
	 * The behavior is undefined but should error
	 * out in case the supplied PID is negative.
	 */
	ATF_REQUIRE(sched_getparam(-1, &s1) != 0);
}

ATF_TC(sched_priority);
ATF_TC_HEAD(sched_priority, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sched(3) priority ranges");
}

ATF_TC_BODY(sched_priority, tc)
{
	static const int pol[3] = { SCHED_OTHER, SCHED_FIFO, SCHED_RR };
	int pmax, pmin;
	size_t i;

	/*
	 * Test that bogus values error out.
	 */
	if (INT_MAX > SCHED_RR)
		ATF_REQUIRE(sched_get_priority_max(INT_MAX) != 0);

	if (-INT_MAX < SCHED_OTHER)
		ATF_REQUIRE(sched_get_priority_max(-INT_MAX) != 0);

	/*
	 * Test that we have a valid range.
	 */
	for (i = 0; i < __arraycount(pol); i++) {

		pmax = sched_get_priority_max(pol[i]);
		pmin = sched_get_priority_min(pol[i]);

		ATF_REQUIRE(pmax != -1);
		ATF_REQUIRE(pmin != -1);
		ATF_REQUIRE(pmax > pmin);
	}
}

static void
sched_priority_set(int pri, int pol)
{
	struct sched_param sched;

	sched.sched_priority = pri;

	ATF_REQUIRE(pri >= 0);
	ATF_REQUIRE(sched_setscheduler(0, pol, &sched) == 0);

	/*
	 * Test that the policy was changed.
	 */
	ATF_CHECK_EQ(sched_getscheduler(0), pol);

	/*
	 * And that sched_getparam(3) returns the new priority.
	 */
	sched.sched_priority = -1;

	ATF_REQUIRE(sched_getparam(0, &sched) == 0);
	ATF_CHECK_EQ(sched.sched_priority, pri);
}

ATF_TC(sched_setscheduler_1);
ATF_TC_HEAD(sched_setscheduler_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "sched_setscheduler(3), max, RR");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sched_setscheduler_1, tc)
{
	int pri;

	pri = sched_get_priority_max(SCHED_RR);
	sched_priority_set(pri, SCHED_RR);
}

ATF_TC(sched_setscheduler_2);
ATF_TC_HEAD(sched_setscheduler_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "sched_setscheduler(3), min, RR");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sched_setscheduler_2, tc)
{
	int pri;

	pri = sched_get_priority_min(SCHED_RR);
	sched_priority_set(pri, SCHED_RR);
}

ATF_TC(sched_setscheduler_3);
ATF_TC_HEAD(sched_setscheduler_3, tc)
{
	atf_tc_set_md_var(tc, "descr", "sched_setscheduler(3), max, FIFO");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sched_setscheduler_3, tc)
{
	int pri;

	pri = sched_get_priority_max(SCHED_FIFO);
	sched_priority_set(pri, SCHED_FIFO);
}

ATF_TC(sched_setscheduler_4);
ATF_TC_HEAD(sched_setscheduler_4, tc)
{
	atf_tc_set_md_var(tc, "descr", "sched_setscheduler(3), min, FIFO");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sched_setscheduler_4, tc)
{
	int pri;

	pri = sched_get_priority_min(SCHED_FIFO);
	sched_priority_set(pri, SCHED_FIFO);
}

ATF_TC(sched_rr_get_interval_1);
ATF_TC_HEAD(sched_rr_get_interval_1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sched_rr_get_interval(3), #1"
	    " (PR lib/44768)");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sched_rr_get_interval_1, tc)
{
	struct timespec tv;
	int pri;

	pri = sched_get_priority_min(SCHED_RR);
	sched_priority_set(pri, SCHED_RR);

	/*
	 * This should fail with ESRCH for invalid PID.
	 */
	ATF_REQUIRE(sched_rr_get_interval(-INT_MAX, &tv) != 0);
}

ATF_TC(sched_rr_get_interval_2);
ATF_TC_HEAD(sched_rr_get_interval_2, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sched_rr_get_interval(3), #2");
	atf_tc_set_md_var(tc, "require.user", "root");
}

ATF_TC_BODY(sched_rr_get_interval_2, tc)
{
	struct timespec tv1, tv2;
	int pri;

	pri = sched_get_priority_min(SCHED_RR);
	sched_priority_set(pri, SCHED_RR);

	tv1.tv_sec = tv2.tv_sec = -1;
	tv1.tv_nsec = tv2.tv_nsec = -1;

	ATF_REQUIRE(sched_rr_get_interval(0, &tv1) == 0);
	ATF_REQUIRE(sched_rr_get_interval(getpid(), &tv2) == 0);

	ATF_REQUIRE(tv1.tv_sec != -1);
	ATF_REQUIRE(tv2.tv_sec != -1);

	ATF_REQUIRE(tv1.tv_nsec != -1);
	ATF_REQUIRE(tv2.tv_nsec != -1);

	ATF_REQUIRE(tv1.tv_sec == tv2.tv_sec);
	ATF_REQUIRE(tv1.tv_nsec == tv2.tv_nsec);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sched_getparam);
	ATF_TP_ADD_TC(tp, sched_priority);

	ATF_TP_ADD_TC(tp, sched_setscheduler_1);
	ATF_TP_ADD_TC(tp, sched_setscheduler_2);
	ATF_TP_ADD_TC(tp, sched_setscheduler_3);
	ATF_TP_ADD_TC(tp, sched_setscheduler_4);

	ATF_TP_ADD_TC(tp, sched_rr_get_interval_1);
	ATF_TP_ADD_TC(tp, sched_rr_get_interval_2);

	return atf_no_error();
}
