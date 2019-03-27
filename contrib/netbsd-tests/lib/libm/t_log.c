/* $NetBSD: t_log.c,v 1.13 2015/02/09 19:39:48 martin Exp $ */

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
__RCSID("$NetBSD: t_log.c,v 1.13 2015/02/09 19:39:48 martin Exp $");

#include <atf-c.h>

#include <math.h>
#include <stdio.h>
#include <string.h>

/*
 * log10(3)
 */
ATF_TC(log10_base);
ATF_TC_HEAD(log10_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(10) == 1");
}

ATF_TC_BODY(log10_base, tc)
{
	ATF_CHECK(log10(10.0) == 1.0);
}

ATF_TC(log10_nan);
ATF_TC_HEAD(log10_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(NaN) == NaN");
}

ATF_TC_BODY(log10_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log10(x)) != 0);
}

ATF_TC(log10_inf_neg);
ATF_TC_HEAD(log10_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(-Inf) == NaN");
}

ATF_TC_BODY(log10_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	const double y = log10(x);

	ATF_CHECK(isnan(y) != 0);
}

ATF_TC(log10_inf_pos);
ATF_TC_HEAD(log10_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(+Inf) == +Inf");
}

ATF_TC_BODY(log10_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(log10(x) == x);
}

ATF_TC(log10_one_pos);
ATF_TC_HEAD(log10_one_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(1.0) == +0.0");
}

ATF_TC_BODY(log10_one_pos, tc)
{
	const double x = log10(1.0);
	const double y = 0.0L;

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(log10_zero_neg);
ATF_TC_HEAD(log10_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(-0.0) == -HUGE_VAL");
}

ATF_TC_BODY(log10_zero_neg, tc)
{
	const double x = -0.0L;

	ATF_CHECK(log10(x) == -HUGE_VAL);
}

ATF_TC(log10_zero_pos);
ATF_TC_HEAD(log10_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10(+0.0) == -HUGE_VAL");
}

ATF_TC_BODY(log10_zero_pos, tc)
{
	const double x = 0.0L;

	ATF_CHECK(log10(x) == -HUGE_VAL);
}

/*
 * log10f(3)
 */
ATF_TC(log10f_base);
ATF_TC_HEAD(log10f_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(10) == 1");
}

ATF_TC_BODY(log10f_base, tc)
{
	ATF_CHECK(log10f(10.0) == 1.0);
}

ATF_TC(log10f_nan);
ATF_TC_HEAD(log10f_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(NaN) == NaN");
}

ATF_TC_BODY(log10f_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log10f(x)) != 0);
}

ATF_TC(log10f_inf_neg);
ATF_TC_HEAD(log10f_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(-Inf) == NaN");
}

ATF_TC_BODY(log10f_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	const float y = log10f(x);

	ATF_CHECK(isnan(y) != 0);
}

ATF_TC(log10f_inf_pos);
ATF_TC_HEAD(log10f_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(+Inf) == +Inf");
}

ATF_TC_BODY(log10f_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	ATF_CHECK(log10f(x) == x);
}

ATF_TC(log10f_one_pos);
ATF_TC_HEAD(log10f_one_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(1.0) == +0.0");
}

ATF_TC_BODY(log10f_one_pos, tc)
{
	const float x = log10f(1.0);
	const float y = 0.0L;

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(log10f_zero_neg);
ATF_TC_HEAD(log10f_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(-0.0) == -HUGE_VALF");
}

ATF_TC_BODY(log10f_zero_neg, tc)
{
	const float x = -0.0L;

	ATF_CHECK(log10f(x) == -HUGE_VALF);
}

ATF_TC(log10f_zero_pos);
ATF_TC_HEAD(log10f_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log10f(+0.0) == -HUGE_VALF");
}

ATF_TC_BODY(log10f_zero_pos, tc)
{
	const float x = 0.0L;

	ATF_CHECK(log10f(x) == -HUGE_VALF);
}

/*
 * log1p(3)
 */
ATF_TC(log1p_nan);
ATF_TC_HEAD(log1p_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1p(NaN) == NaN");
}

ATF_TC_BODY(log1p_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log1p(x)) != 0);
}

ATF_TC(log1p_inf_neg);
ATF_TC_HEAD(log1p_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1p(-Inf) == NaN");
}

ATF_TC_BODY(log1p_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	const double y = log1p(x);

	if (isnan(y) == 0) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("log1p(-Inf) != NaN");
	}
}

ATF_TC(log1p_inf_pos);
ATF_TC_HEAD(log1p_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1p(+Inf) == +Inf");
}

ATF_TC_BODY(log1p_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(log1p(x) == x);
}

ATF_TC(log1p_one_neg);
ATF_TC_HEAD(log1p_one_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1p(-1.0) == -HUGE_VAL");
}

ATF_TC_BODY(log1p_one_neg, tc)
{
	const double x = log1p(-1.0);

	if (x != -HUGE_VAL) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("log1p(-1.0) != -HUGE_VAL");
	}
}

ATF_TC(log1p_zero_neg);
ATF_TC_HEAD(log1p_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1p(-0.0) == -0.0");
}

ATF_TC_BODY(log1p_zero_neg, tc)
{
	const double x = -0.0L;

	ATF_CHECK(log1p(x) == x);
}

ATF_TC(log1p_zero_pos);
ATF_TC_HEAD(log1p_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1p(+0.0) == +0.0");
}

ATF_TC_BODY(log1p_zero_pos, tc)
{
	const double x = 0.0L;

	ATF_CHECK(log1p(x) == x);
}

/*
 * log1pf(3)
 */
ATF_TC(log1pf_nan);
ATF_TC_HEAD(log1pf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1pf(NaN) == NaN");
}

ATF_TC_BODY(log1pf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log1pf(x)) != 0);
}

ATF_TC(log1pf_inf_neg);
ATF_TC_HEAD(log1pf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1pf(-Inf) == NaN");
}

ATF_TC_BODY(log1pf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	const float y = log1pf(x);

	if (isnan(y) == 0) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("log1pf(-Inf) != NaN");
	}
}

ATF_TC(log1pf_inf_pos);
ATF_TC_HEAD(log1pf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1pf(+Inf) == +Inf");
}

ATF_TC_BODY(log1pf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	ATF_CHECK(log1pf(x) == x);
}

ATF_TC(log1pf_one_neg);
ATF_TC_HEAD(log1pf_one_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1pf(-1.0) == -HUGE_VALF");
}

ATF_TC_BODY(log1pf_one_neg, tc)
{
	const float x = log1pf(-1.0);

	if (x != -HUGE_VALF) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("log1pf(-1.0) != -HUGE_VALF");
	}
}

ATF_TC(log1pf_zero_neg);
ATF_TC_HEAD(log1pf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1pf(-0.0) == -0.0");
}

ATF_TC_BODY(log1pf_zero_neg, tc)
{
	const float x = -0.0L;

	ATF_CHECK(log1pf(x) == x);
}

ATF_TC(log1pf_zero_pos);
ATF_TC_HEAD(log1pf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log1pf(+0.0) == +0.0");
}

ATF_TC_BODY(log1pf_zero_pos, tc)
{
	const float x = 0.0L;

	ATF_CHECK(log1pf(x) == x);
}

/*
 * log2(3)
 */
ATF_TC(log2_base);
ATF_TC_HEAD(log2_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(2) == 1");
}

ATF_TC_BODY(log2_base, tc)
{
	ATF_CHECK(log2(2.0) == 1.0);
}

ATF_TC(log2_nan);
ATF_TC_HEAD(log2_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(NaN) == NaN");
}

ATF_TC_BODY(log2_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log2(x)) != 0);
}

ATF_TC(log2_inf_neg);
ATF_TC_HEAD(log2_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(-Inf) == NaN");
}

ATF_TC_BODY(log2_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	const double y = log2(x);

	ATF_CHECK(isnan(y) != 0);
}

ATF_TC(log2_inf_pos);
ATF_TC_HEAD(log2_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(+Inf) == +Inf");
}

ATF_TC_BODY(log2_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(log2(x) == x);
}

ATF_TC(log2_one_pos);
ATF_TC_HEAD(log2_one_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(1.0) == +0.0");
}

ATF_TC_BODY(log2_one_pos, tc)
{
	const double x = log2(1.0);
	const double y = 0.0L;

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(log2_zero_neg);
ATF_TC_HEAD(log2_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(-0.0) == -HUGE_VAL");
}

ATF_TC_BODY(log2_zero_neg, tc)
{
	const double x = -0.0L;

	ATF_CHECK(log2(x) == -HUGE_VAL);
}

ATF_TC(log2_zero_pos);
ATF_TC_HEAD(log2_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2(+0.0) == -HUGE_VAL");
}

ATF_TC_BODY(log2_zero_pos, tc)
{
	const double x = 0.0L;

	ATF_CHECK(log2(x) == -HUGE_VAL);
}

/*
 * log2f(3)
 */
ATF_TC(log2f_base);
ATF_TC_HEAD(log2f_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(2) == 1");
}

ATF_TC_BODY(log2f_base, tc)
{
	ATF_CHECK(log2f(2.0) == 1.0);
}

ATF_TC(log2f_nan);
ATF_TC_HEAD(log2f_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(NaN) == NaN");
}

ATF_TC_BODY(log2f_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log2f(x)) != 0);
}

ATF_TC(log2f_inf_neg);
ATF_TC_HEAD(log2f_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(-Inf) == NaN");
}

ATF_TC_BODY(log2f_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	const float y = log2f(x);

	ATF_CHECK(isnan(y) != 0);
}

ATF_TC(log2f_inf_pos);
ATF_TC_HEAD(log2f_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(+Inf) == +Inf");
}

ATF_TC_BODY(log2f_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	ATF_CHECK(log2f(x) == x);
}

ATF_TC(log2f_one_pos);
ATF_TC_HEAD(log2f_one_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(1.0) == +0.0");
}

ATF_TC_BODY(log2f_one_pos, tc)
{
	const float x = log2f(1.0);
	const float y = 0.0L;

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(log2f_zero_neg);
ATF_TC_HEAD(log2f_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(-0.0) == -HUGE_VALF");
}

ATF_TC_BODY(log2f_zero_neg, tc)
{
	const float x = -0.0L;

	ATF_CHECK(log2f(x) == -HUGE_VALF);
}

ATF_TC(log2f_zero_pos);
ATF_TC_HEAD(log2f_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log2f(+0.0) == -HUGE_VALF");
}

ATF_TC_BODY(log2f_zero_pos, tc)
{
	const float x = 0.0L;

	ATF_CHECK(log2f(x) == -HUGE_VALF);
}

/*
 * log(3)
 */
ATF_TC(log_base);
ATF_TC_HEAD(log_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(e) == 1");
}

ATF_TC_BODY(log_base, tc)
{
	const double eps = 1.0e-38;

	if (fabs(log(M_E) - 1.0) > eps)
		atf_tc_fail_nonfatal("log(e) != 1");
}

ATF_TC(log_nan);
ATF_TC_HEAD(log_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(NaN) == NaN");
}

ATF_TC_BODY(log_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(log(x)) != 0);
}

ATF_TC(log_inf_neg);
ATF_TC_HEAD(log_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(-Inf) == NaN");
}

ATF_TC_BODY(log_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	const double y = log(x);

	ATF_CHECK(isnan(y) != 0);
}

ATF_TC(log_inf_pos);
ATF_TC_HEAD(log_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(+Inf) == +Inf");
}

ATF_TC_BODY(log_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(log(x) == x);
}

ATF_TC(log_one_pos);
ATF_TC_HEAD(log_one_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(1.0) == +0.0");
}

ATF_TC_BODY(log_one_pos, tc)
{
	const double x = log(1.0);
	const double y = 0.0L;

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(log_zero_neg);
ATF_TC_HEAD(log_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(-0.0) == -HUGE_VAL");
}

ATF_TC_BODY(log_zero_neg, tc)
{
	const double x = -0.0L;

	ATF_CHECK(log(x) == -HUGE_VAL);
}

ATF_TC(log_zero_pos);
ATF_TC_HEAD(log_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test log(+0.0) == -HUGE_VAL");
}

ATF_TC_BODY(log_zero_pos, tc)
{
	const double x = 0.0L;

	ATF_CHECK(log(x) == -HUGE_VAL);
}

/*
 * logf(3)
 */
ATF_TC(logf_base);
ATF_TC_HEAD(logf_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(e) == 1");
}

ATF_TC_BODY(logf_base, tc)
{
	const float eps = 1.0e-7;

	if (fabsf(logf(M_E) - 1.0f) > eps)
		atf_tc_fail_nonfatal("logf(e) != 1");
}

ATF_TC(logf_nan);
ATF_TC_HEAD(logf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(NaN) == NaN");
}

ATF_TC_BODY(logf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(logf(x)) != 0);
}

ATF_TC(logf_inf_neg);
ATF_TC_HEAD(logf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(-Inf) == NaN");
}

ATF_TC_BODY(logf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	const float y = logf(x);

	ATF_CHECK(isnan(y) != 0);
}

ATF_TC(logf_inf_pos);
ATF_TC_HEAD(logf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(+Inf) == +Inf");
}

ATF_TC_BODY(logf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	ATF_CHECK(logf(x) == x);
}

ATF_TC(logf_one_pos);
ATF_TC_HEAD(logf_one_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(1.0) == +0.0");
}

ATF_TC_BODY(logf_one_pos, tc)
{
	const float x = logf(1.0);
	const float y = 0.0L;

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(logf_zero_neg);
ATF_TC_HEAD(logf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(-0.0) == -HUGE_VALF");
}

ATF_TC_BODY(logf_zero_neg, tc)
{
	const float x = -0.0L;

	ATF_CHECK(logf(x) == -HUGE_VALF);
}

ATF_TC(logf_zero_pos);
ATF_TC_HEAD(logf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test logf(+0.0) == -HUGE_VALF");
}

ATF_TC_BODY(logf_zero_pos, tc)
{
	const float x = 0.0L;

	ATF_CHECK(logf(x) == -HUGE_VALF);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, log10_base);
	ATF_TP_ADD_TC(tp, log10_nan);
	ATF_TP_ADD_TC(tp, log10_inf_neg);
	ATF_TP_ADD_TC(tp, log10_inf_pos);
	ATF_TP_ADD_TC(tp, log10_one_pos);
	ATF_TP_ADD_TC(tp, log10_zero_neg);
	ATF_TP_ADD_TC(tp, log10_zero_pos);

	ATF_TP_ADD_TC(tp, log10f_base);
	ATF_TP_ADD_TC(tp, log10f_nan);
	ATF_TP_ADD_TC(tp, log10f_inf_neg);
	ATF_TP_ADD_TC(tp, log10f_inf_pos);
	ATF_TP_ADD_TC(tp, log10f_one_pos);
	ATF_TP_ADD_TC(tp, log10f_zero_neg);
	ATF_TP_ADD_TC(tp, log10f_zero_pos);

	ATF_TP_ADD_TC(tp, log1p_nan);
	ATF_TP_ADD_TC(tp, log1p_inf_neg);
	ATF_TP_ADD_TC(tp, log1p_inf_pos);
	ATF_TP_ADD_TC(tp, log1p_one_neg);
	ATF_TP_ADD_TC(tp, log1p_zero_neg);
	ATF_TP_ADD_TC(tp, log1p_zero_pos);

	ATF_TP_ADD_TC(tp, log1pf_nan);
	ATF_TP_ADD_TC(tp, log1pf_inf_neg);
	ATF_TP_ADD_TC(tp, log1pf_inf_pos);
	ATF_TP_ADD_TC(tp, log1pf_one_neg);
	ATF_TP_ADD_TC(tp, log1pf_zero_neg);
	ATF_TP_ADD_TC(tp, log1pf_zero_pos);

	ATF_TP_ADD_TC(tp, log2_base);
	ATF_TP_ADD_TC(tp, log2_nan);
	ATF_TP_ADD_TC(tp, log2_inf_neg);
	ATF_TP_ADD_TC(tp, log2_inf_pos);
	ATF_TP_ADD_TC(tp, log2_one_pos);
	ATF_TP_ADD_TC(tp, log2_zero_neg);
	ATF_TP_ADD_TC(tp, log2_zero_pos);

	ATF_TP_ADD_TC(tp, log2f_base);
	ATF_TP_ADD_TC(tp, log2f_nan);
	ATF_TP_ADD_TC(tp, log2f_inf_neg);
	ATF_TP_ADD_TC(tp, log2f_inf_pos);
	ATF_TP_ADD_TC(tp, log2f_one_pos);
	ATF_TP_ADD_TC(tp, log2f_zero_neg);
	ATF_TP_ADD_TC(tp, log2f_zero_pos);

	ATF_TP_ADD_TC(tp, log_base);
	ATF_TP_ADD_TC(tp, log_nan);
	ATF_TP_ADD_TC(tp, log_inf_neg);
	ATF_TP_ADD_TC(tp, log_inf_pos);
	ATF_TP_ADD_TC(tp, log_one_pos);
	ATF_TP_ADD_TC(tp, log_zero_neg);
	ATF_TP_ADD_TC(tp, log_zero_pos);

	ATF_TP_ADD_TC(tp, logf_base);
	ATF_TP_ADD_TC(tp, logf_nan);
	ATF_TP_ADD_TC(tp, logf_inf_neg);
	ATF_TP_ADD_TC(tp, logf_inf_pos);
	ATF_TP_ADD_TC(tp, logf_one_pos);
	ATF_TP_ADD_TC(tp, logf_zero_neg);
	ATF_TP_ADD_TC(tp, logf_zero_pos);

	return atf_no_error();
}
