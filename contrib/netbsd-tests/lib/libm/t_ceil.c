/* $NetBSD: t_ceil.c,v 1.10 2014/03/03 10:39:08 martin Exp $ */

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
__RCSID("$NetBSD: t_ceil.c,v 1.10 2014/03/03 10:39:08 martin Exp $");

#include <atf-c.h>
#include <math.h>
#include <limits.h>
#include <stdio.h>

#ifdef __vax__
#define SMALL_NUM	1.0e-38
#else
#define SMALL_NUM	1.0e-40
#endif

/*
 * ceil(3)
 */
ATF_TC(ceil_basic);
ATF_TC_HEAD(ceil_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of ceil(3)");
}

ATF_TC_BODY(ceil_basic, tc)
{
	const double x = 0.999999999999999;
	const double y = 0.000000000000001;

	ATF_CHECK(fabs(ceil(x) - 1) < SMALL_NUM);
	ATF_CHECK(fabs(ceil(y) - 1) < SMALL_NUM);
}

ATF_TC(ceil_nan);
ATF_TC_HEAD(ceil_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceil(NaN) == NaN");
}

ATF_TC_BODY(ceil_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(ceil(x)) != 0);
}

ATF_TC(ceil_inf_neg);
ATF_TC_HEAD(ceil_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceil(-Inf) == -Inf");
}

ATF_TC_BODY(ceil_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	double y = ceil(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("ceil(-Inf) != -Inf");
}

ATF_TC(ceil_inf_pos);
ATF_TC_HEAD(ceil_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceil(+Inf) == +Inf");
}

ATF_TC_BODY(ceil_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = ceil(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("ceil(+Inf) != +Inf");
}

ATF_TC(ceil_zero_neg);
ATF_TC_HEAD(ceil_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceil(-0.0) == -0.0");
}

ATF_TC_BODY(ceil_zero_neg, tc)
{
	const double x = -0.0L;
	double y = ceil(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("ceil(-0.0) != -0.0");
}

ATF_TC(ceil_zero_pos);
ATF_TC_HEAD(ceil_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceil(+0.0) == +0.0");
}

ATF_TC_BODY(ceil_zero_pos, tc)
{
	const double x = 0.0L;
	double y = ceil(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("ceil(+0.0) != +0.0");
}

/*
 * ceilf(3)
 */
ATF_TC(ceilf_basic);
ATF_TC_HEAD(ceilf_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of ceilf(3)");
}

ATF_TC_BODY(ceilf_basic, tc)
{
	const float x = 0.9999999;
	const float y = 0.0000001;

	ATF_CHECK(fabsf(ceilf(x) - 1) < SMALL_NUM);
	ATF_CHECK(fabsf(ceilf(y) - 1) < SMALL_NUM);
}

ATF_TC(ceilf_nan);
ATF_TC_HEAD(ceilf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceilf(NaN) == NaN");
}

ATF_TC_BODY(ceilf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(ceilf(x)) != 0);
}

ATF_TC(ceilf_inf_neg);
ATF_TC_HEAD(ceilf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceilf(-Inf) == -Inf");
}

ATF_TC_BODY(ceilf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	float y = ceilf(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("ceilf(-Inf) != -Inf");
}

ATF_TC(ceilf_inf_pos);
ATF_TC_HEAD(ceilf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceilf(+Inf) == +Inf");
}

ATF_TC_BODY(ceilf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = ceilf(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("ceilf(+Inf) != +Inf");
}

ATF_TC(ceilf_zero_neg);
ATF_TC_HEAD(ceilf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceilf(-0.0) == -0.0");
}

ATF_TC_BODY(ceilf_zero_neg, tc)
{
	const float x = -0.0L;
	float y = ceilf(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("ceilf(-0.0) != -0.0");
}

ATF_TC(ceilf_zero_pos);
ATF_TC_HEAD(ceilf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceilf(+0.0) == +0.0");
}

ATF_TC_BODY(ceilf_zero_pos, tc)
{
	const float x = 0.0L;
	float y = ceilf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("ceilf(+0.0) != +0.0");
}

/*
 * ceill(3)
 */
ATF_TC(ceill_basic);
ATF_TC_HEAD(ceill_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of ceill(3)");
}

ATF_TC_BODY(ceill_basic, tc)
{
	const long double x = 0.9999999;
	const long double y = 0.0000001;

	ATF_CHECK(fabsl(ceill(x) - 1) < SMALL_NUM);
	ATF_CHECK(fabsl(ceill(y) - 1) < SMALL_NUM);
}

ATF_TC(ceill_nan);
ATF_TC_HEAD(ceill_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceill(NaN) == NaN");
}

ATF_TC_BODY(ceill_nan, tc)
{
	const long double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(ceill(x)) != 0);
}

ATF_TC(ceill_inf_neg);
ATF_TC_HEAD(ceill_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceill(-Inf) == -Inf");
}

ATF_TC_BODY(ceill_inf_neg, tc)
{
	const long double x = -1.0L / 0.0L;
	long double y = ceill(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("ceill(-Inf) != -Inf");
}

ATF_TC(ceill_inf_pos);
ATF_TC_HEAD(ceill_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceill(+Inf) == +Inf");
}

ATF_TC_BODY(ceill_inf_pos, tc)
{
	const long double x = 1.0L / 0.0L;
	long double y = ceill(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("ceill(+Inf) != +Inf");
}

ATF_TC(ceill_zero_neg);
ATF_TC_HEAD(ceill_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceill(-0.0) == -0.0");
}

ATF_TC_BODY(ceill_zero_neg, tc)
{
	const long double x = -0.0L;
	long double y = ceill(x);

	if (fabsl(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("ceill(-0.0) != -0.0");
}

ATF_TC(ceill_zero_pos);
ATF_TC_HEAD(ceill_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ceill(+0.0) == +0.0");
}

ATF_TC_BODY(ceill_zero_pos, tc)
{
	const long double x = 0.0L;
	long double y = ceill(x);

	if (fabsl(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("ceill(+0.0) != +0.0");
}

/*
 * floor(3)
 */
ATF_TC(floor_basic);
ATF_TC_HEAD(floor_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of floor(3)");
}

ATF_TC_BODY(floor_basic, tc)
{
	const double x = 0.999999999999999;
	const double y = 0.000000000000001;

	ATF_CHECK(floor(x) < SMALL_NUM);
	ATF_CHECK(floor(y) < SMALL_NUM);
}

ATF_TC(floor_nan);
ATF_TC_HEAD(floor_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floor(NaN) == NaN");
}

ATF_TC_BODY(floor_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(floor(x)) != 0);
}

ATF_TC(floor_inf_neg);
ATF_TC_HEAD(floor_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floor(-Inf) == -Inf");
}

ATF_TC_BODY(floor_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	double y = floor(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("floor(-Inf) != -Inf");
}

ATF_TC(floor_inf_pos);
ATF_TC_HEAD(floor_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floor(+Inf) == +Inf");
}

ATF_TC_BODY(floor_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = floor(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("floor(+Inf) != +Inf");
}

ATF_TC(floor_zero_neg);
ATF_TC_HEAD(floor_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floor(-0.0) == -0.0");
}

ATF_TC_BODY(floor_zero_neg, tc)
{
	const double x = -0.0L;
	double y = floor(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("floor(-0.0) != -0.0");
}

ATF_TC(floor_zero_pos);
ATF_TC_HEAD(floor_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floor(+0.0) == +0.0");
}

ATF_TC_BODY(floor_zero_pos, tc)
{
	const double x = 0.0L;
	double y = floor(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("floor(+0.0) != +0.0");
}

/*
 * floorf(3)
 */
ATF_TC(floorf_basic);
ATF_TC_HEAD(floorf_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of floorf(3)");
}

ATF_TC_BODY(floorf_basic, tc)
{
	const float x = 0.9999999;
	const float y = 0.0000001;

	ATF_CHECK(floorf(x) < SMALL_NUM);
	ATF_CHECK(floorf(y) < SMALL_NUM);
}

ATF_TC(floorf_nan);
ATF_TC_HEAD(floorf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorf(NaN) == NaN");
}

ATF_TC_BODY(floorf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(floorf(x)) != 0);
}

ATF_TC(floorf_inf_neg);
ATF_TC_HEAD(floorf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorf(-Inf) == -Inf");
}

ATF_TC_BODY(floorf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	float y = floorf(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("floorf(-Inf) != -Inf");
}

ATF_TC(floorf_inf_pos);
ATF_TC_HEAD(floorf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorf(+Inf) == +Inf");
}

ATF_TC_BODY(floorf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = floorf(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("floorf(+Inf) != +Inf");
}

ATF_TC(floorf_zero_neg);
ATF_TC_HEAD(floorf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorf(-0.0) == -0.0");
}

ATF_TC_BODY(floorf_zero_neg, tc)
{
	const float x = -0.0L;
	float y = floorf(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("floorf(-0.0) != -0.0");
}

ATF_TC(floorf_zero_pos);
ATF_TC_HEAD(floorf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorf(+0.0) == +0.0");
}

ATF_TC_BODY(floorf_zero_pos, tc)
{
	const float x = 0.0L;
	float y = floorf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("floorf(+0.0) != +0.0");
}

/*
 * floorl(3)
 */
ATF_TC(floorl_basic);
ATF_TC_HEAD(floorl_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of floorl(3)");
}

ATF_TC_BODY(floorl_basic, tc)
{
	const long double x = 0.9999999;
	const long double y = 0.0000001;

	ATF_CHECK(floorl(x) < SMALL_NUM);
	ATF_CHECK(floorl(y) < SMALL_NUM);
}

ATF_TC(floorl_nan);
ATF_TC_HEAD(floorl_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorl(NaN) == NaN");
}

ATF_TC_BODY(floorl_nan, tc)
{
	const long double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(floorl(x)) != 0);
}

ATF_TC(floorl_inf_neg);
ATF_TC_HEAD(floorl_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorl(-Inf) == -Inf");
}

ATF_TC_BODY(floorl_inf_neg, tc)
{
	const long double x = -1.0L / 0.0L;
	long double y = floorl(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("floorl(-Inf) != -Inf");
}

ATF_TC(floorl_inf_pos);
ATF_TC_HEAD(floorl_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorl(+Inf) == +Inf");
}

ATF_TC_BODY(floorl_inf_pos, tc)
{
	const long double x = 1.0L / 0.0L;
	long double y = floorl(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("floorl(+Inf) != +Inf");
}

ATF_TC(floorl_zero_neg);
ATF_TC_HEAD(floorl_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorl(-0.0) == -0.0");
}

ATF_TC_BODY(floorl_zero_neg, tc)
{
	const long double x = -0.0L;
	long double y = floorl(x);

	if (fabsl(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("floorl(-0.0) != -0.0");
}

ATF_TC(floorl_zero_pos);
ATF_TC_HEAD(floorl_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test floorl(+0.0) == +0.0");
}

ATF_TC_BODY(floorl_zero_pos, tc)
{
	const long double x = 0.0L;
	long double y = floorl(x);

	if (fabsl(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("floorl(+0.0) != +0.0");
}

/*
 * trunc(3)
 */
ATF_TC(trunc_basic);
ATF_TC_HEAD(trunc_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of trunc(3)");
}

ATF_TC_BODY(trunc_basic, tc)
{
	const double x = 0.999999999999999;
	const double y = 0.000000000000001;

	ATF_CHECK(trunc(x) < SMALL_NUM);
	ATF_CHECK(trunc(y) < SMALL_NUM);
}

ATF_TC(trunc_nan);
ATF_TC_HEAD(trunc_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test trunc(NaN) == NaN");
}

ATF_TC_BODY(trunc_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(trunc(x)) != 0);
}

ATF_TC(trunc_inf_neg);
ATF_TC_HEAD(trunc_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test trunc(-Inf) == -Inf");
}

ATF_TC_BODY(trunc_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	double y = trunc(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("trunc(-Inf) != -Inf");
}

ATF_TC(trunc_inf_pos);
ATF_TC_HEAD(trunc_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test trunc(+Inf) == +Inf");
}

ATF_TC_BODY(trunc_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = trunc(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("trunc(+Inf) != +Inf");
}

ATF_TC(trunc_zero_neg);
ATF_TC_HEAD(trunc_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test trunc(-0.0) == -0.0");
}

ATF_TC_BODY(trunc_zero_neg, tc)
{
	const double x = -0.0L;
	double y = trunc(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("trunc(-0.0) != -0.0");
}

ATF_TC(trunc_zero_pos);
ATF_TC_HEAD(trunc_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test trunc(+0.0) == +0.0");
}

ATF_TC_BODY(trunc_zero_pos, tc)
{
	const double x = 0.0L;
	double y = trunc(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("trunc(+0.0) != +0.0");
}

/*
 * truncf(3)
 */
ATF_TC(truncf_basic);
ATF_TC_HEAD(truncf_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of truncf(3)");
}

ATF_TC_BODY(truncf_basic, tc)
{
	const float x = 0.9999999;
	const float y = 0.0000001;

	ATF_CHECK(truncf(x) < SMALL_NUM);
	ATF_CHECK(truncf(y) < SMALL_NUM);
}

ATF_TC(truncf_nan);
ATF_TC_HEAD(truncf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncf(NaN) == NaN");
}

ATF_TC_BODY(truncf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(truncf(x)) != 0);
}

ATF_TC(truncf_inf_neg);
ATF_TC_HEAD(truncf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncf(-Inf) == -Inf");
}

ATF_TC_BODY(truncf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	float y = truncf(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("truncf(-Inf) != -Inf");
}

ATF_TC(truncf_inf_pos);
ATF_TC_HEAD(truncf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncf(+Inf) == +Inf");
}

ATF_TC_BODY(truncf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = truncf(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("truncf(+Inf) != +Inf");
}

ATF_TC(truncf_zero_neg);
ATF_TC_HEAD(truncf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncf(-0.0) == -0.0");
}

ATF_TC_BODY(truncf_zero_neg, tc)
{
	const float x = -0.0L;
	float y = truncf(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("truncf(-0.0) != -0.0");
}

ATF_TC(truncf_zero_pos);
ATF_TC_HEAD(truncf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncf(+0.0) == +0.0");
}

ATF_TC_BODY(truncf_zero_pos, tc)
{
	const float x = 0.0L;
	float y = truncf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("truncf(+0.0) != +0.0");
}

/*
 * truncl(3)
 */
ATF_TC(truncl_basic);
ATF_TC_HEAD(truncl_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of truncl(3)");
}

ATF_TC_BODY(truncl_basic, tc)
{
	const long double x = 0.9999999;
	const long double y = 0.0000001;

	ATF_CHECK(truncl(x) < SMALL_NUM);
	ATF_CHECK(truncl(y) < SMALL_NUM);
}

ATF_TC(truncl_nan);
ATF_TC_HEAD(truncl_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncl(NaN) == NaN");
}

ATF_TC_BODY(truncl_nan, tc)
{
	const long double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(truncl(x)) != 0);
}

ATF_TC(truncl_inf_neg);
ATF_TC_HEAD(truncl_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncl(-Inf) == -Inf");
}

ATF_TC_BODY(truncl_inf_neg, tc)
{
	const long double x = -1.0L / 0.0L;
	long double y = truncl(x);

	if (isinf(y) == 0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("truncl(-Inf) != -Inf");
}

ATF_TC(truncl_inf_pos);
ATF_TC_HEAD(truncl_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncl(+Inf) == +Inf");
}

ATF_TC_BODY(truncl_inf_pos, tc)
{
	const long double x = 1.0L / 0.0L;
	long double y = truncl(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("truncl(+Inf) != +Inf");
}

ATF_TC(truncl_zero_neg);
ATF_TC_HEAD(truncl_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncl(-0.0) == -0.0");
}

ATF_TC_BODY(truncl_zero_neg, tc)
{
	const long double x = -0.0L;
	long double y = truncl(x);

	if (fabsl(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("truncl(-0.0) != -0.0");
}

ATF_TC(truncl_zero_pos);
ATF_TC_HEAD(truncl_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test truncl(+0.0) == +0.0");
}

ATF_TC_BODY(truncl_zero_pos, tc)
{
	const long double x = 0.0L;
	long double y = truncl(x);

	if (fabsl(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("truncl(+0.0) != +0.0");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, ceil_basic);
	ATF_TP_ADD_TC(tp, ceil_nan);
	ATF_TP_ADD_TC(tp, ceil_inf_neg);
	ATF_TP_ADD_TC(tp, ceil_inf_pos);
	ATF_TP_ADD_TC(tp, ceil_zero_neg);
	ATF_TP_ADD_TC(tp, ceil_zero_pos);

	ATF_TP_ADD_TC(tp, ceilf_basic);
	ATF_TP_ADD_TC(tp, ceilf_nan);
	ATF_TP_ADD_TC(tp, ceilf_inf_neg);
	ATF_TP_ADD_TC(tp, ceilf_inf_pos);
	ATF_TP_ADD_TC(tp, ceilf_zero_neg);
	ATF_TP_ADD_TC(tp, ceilf_zero_pos);

	ATF_TP_ADD_TC(tp, ceill_basic);
	ATF_TP_ADD_TC(tp, ceill_nan);
	ATF_TP_ADD_TC(tp, ceill_inf_neg);
	ATF_TP_ADD_TC(tp, ceill_inf_pos);
	ATF_TP_ADD_TC(tp, ceill_zero_neg);
	ATF_TP_ADD_TC(tp, ceill_zero_pos);

	ATF_TP_ADD_TC(tp, floor_basic);
	ATF_TP_ADD_TC(tp, floor_nan);
	ATF_TP_ADD_TC(tp, floor_inf_neg);
	ATF_TP_ADD_TC(tp, floor_inf_pos);
	ATF_TP_ADD_TC(tp, floor_zero_neg);
	ATF_TP_ADD_TC(tp, floor_zero_pos);

	ATF_TP_ADD_TC(tp, floorf_basic);
	ATF_TP_ADD_TC(tp, floorf_nan);
	ATF_TP_ADD_TC(tp, floorf_inf_neg);
	ATF_TP_ADD_TC(tp, floorf_inf_pos);
	ATF_TP_ADD_TC(tp, floorf_zero_neg);
	ATF_TP_ADD_TC(tp, floorf_zero_pos);

	ATF_TP_ADD_TC(tp, floorl_basic);
	ATF_TP_ADD_TC(tp, floorl_nan);
	ATF_TP_ADD_TC(tp, floorl_inf_neg);
	ATF_TP_ADD_TC(tp, floorl_inf_pos);
	ATF_TP_ADD_TC(tp, floorl_zero_neg);
	ATF_TP_ADD_TC(tp, floorl_zero_pos);

	ATF_TP_ADD_TC(tp, trunc_basic);
	ATF_TP_ADD_TC(tp, trunc_nan);
	ATF_TP_ADD_TC(tp, trunc_inf_neg);
	ATF_TP_ADD_TC(tp, trunc_inf_pos);
	ATF_TP_ADD_TC(tp, trunc_zero_neg);
	ATF_TP_ADD_TC(tp, trunc_zero_pos);

	ATF_TP_ADD_TC(tp, truncf_basic);
	ATF_TP_ADD_TC(tp, truncf_nan);
	ATF_TP_ADD_TC(tp, truncf_inf_neg);
	ATF_TP_ADD_TC(tp, truncf_inf_pos);
	ATF_TP_ADD_TC(tp, truncf_zero_neg);
	ATF_TP_ADD_TC(tp, truncf_zero_pos);

	ATF_TP_ADD_TC(tp, truncl_basic);
	ATF_TP_ADD_TC(tp, truncl_nan);
	ATF_TP_ADD_TC(tp, truncl_inf_neg);
	ATF_TP_ADD_TC(tp, truncl_inf_pos);
	ATF_TP_ADD_TC(tp, truncl_zero_neg);
	ATF_TP_ADD_TC(tp, truncl_zero_pos);

	return atf_no_error();
}
