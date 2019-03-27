/* $NetBSD: t_asin.c,v 1.3 2014/03/03 10:39:08 martin Exp $ */

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

#include <atf-c.h>
#include <math.h>

static const struct {
	double x;
	double y;
} values[] = {
	{ -1.0, -M_PI / 2, },
	{ -0.9, -1.119769514998634, },
	{ -0.5, -M_PI / 6, },
	{ -0.1, -0.1001674211615598, },
	{  0.1,  0.1001674211615598, },
	{  0.5,  M_PI / 6, },
	{  0.9,  1.119769514998634, },
	{  1.0,  M_PI / 2, },
};

/*
 * asin(3)
 */
ATF_TC(asin_nan);
ATF_TC_HEAD(asin_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(NaN) == NaN");
}

ATF_TC_BODY(asin_nan, tc)
{
	const double x = 0.0L / 0.0L;

	if (isnan(asin(x)) == 0)
		atf_tc_fail_nonfatal("asin(NaN) != NaN");
}

ATF_TC(asin_inf_neg);
ATF_TC_HEAD(asin_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(-Inf) == NaN");
}

ATF_TC_BODY(asin_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	if (isnan(asin(x)) == 0)
		atf_tc_fail_nonfatal("asin(-Inf) != NaN");
}

ATF_TC(asin_inf_pos);
ATF_TC_HEAD(asin_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(+Inf) == NaN");
}

ATF_TC_BODY(asin_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	if (isnan(asin(x)) == 0)
		atf_tc_fail_nonfatal("asin(+Inf) != NaN");
}

ATF_TC(asin_range);
ATF_TC_HEAD(asin_range, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(x) == NaN, x < -1, x > 1");
}

ATF_TC_BODY(asin_range, tc)
{
	const double x[] = { -1.1, -1.000000001, 1.1, 1.000000001 };
	size_t i;

	for (i = 0; i < __arraycount(x); i++) {

		if (isnan(asin(x[i])) == 0)
			atf_tc_fail_nonfatal("asin(%f) != NaN", x[i]);
	}
}

ATF_TC(asin_inrange);
ATF_TC_HEAD(asin_inrange, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(x) for some values");
}

ATF_TC_BODY(asin_inrange, tc)
{
	const double eps = 1.0e-15;
	double y;
	size_t i;

	for (i = 0; i < __arraycount(values); i++) {
		y = asin(values[i].x);
		if (fabs(y - values[i].y) > eps)
			atf_tc_fail_nonfatal("asin(%g) != %g",
				values[i].x, values[i].y);
	}
}

ATF_TC(asin_zero_neg);
ATF_TC_HEAD(asin_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(-0.0) == -0.0");
}

ATF_TC_BODY(asin_zero_neg, tc)
{
	const double x = -0.0L;
	double y = asin(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("asin(-0.0) != -0.0");
}

ATF_TC(asin_zero_pos);
ATF_TC_HEAD(asin_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asin(+0.0) == +0.0");
}

ATF_TC_BODY(asin_zero_pos, tc)
{
	const double x = 0.0L;
	double y = asin(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("asin(+0.0) != +0.0");
}

/*
 * asinf(3)
 */
ATF_TC(asinf_nan);
ATF_TC_HEAD(asinf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(NaN) == NaN");
}

ATF_TC_BODY(asinf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	if (isnan(asinf(x)) == 0)
		atf_tc_fail_nonfatal("asinf(NaN) != NaN");
}

ATF_TC(asinf_inf_neg);
ATF_TC_HEAD(asinf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(-Inf) == NaN");
}

ATF_TC_BODY(asinf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	if (isnan(asinf(x)) == 0)
		atf_tc_fail_nonfatal("asinf(-Inf) != NaN");
}

ATF_TC(asinf_inf_pos);
ATF_TC_HEAD(asinf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(+Inf) == NaN");
}

ATF_TC_BODY(asinf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	if (isnan(asinf(x)) == 0)
		atf_tc_fail_nonfatal("asinf(+Inf) != NaN");
}

ATF_TC(asinf_range);
ATF_TC_HEAD(asinf_range, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(x) == NaN, x < -1, x > 1");
}

ATF_TC_BODY(asinf_range, tc)
{
	const float x[] = { -1.1, -1.0000001, 1.1, 1.0000001 };
	size_t i;

	for (i = 0; i < __arraycount(x); i++) {

		if (isnan(asinf(x[i])) == 0)
			atf_tc_fail_nonfatal("asinf(%f) != NaN", x[i]);
	}
}

ATF_TC(asinf_inrange);
ATF_TC_HEAD(asinf_inrange, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(x) for some values");
}

ATF_TC_BODY(asinf_inrange, tc)
{
	const float eps = 1.0e-6;
	float x;
	float y;
	size_t i;

	for (i = 0; i < __arraycount(values); i++) {
		x = values[i].x;
		y = values[i].y;
		if (fabs(asinf(x) - y) > eps)
			atf_tc_fail_nonfatal("asinf(%g) != %g", x, y);
	}
}

ATF_TC(asinf_zero_neg);
ATF_TC_HEAD(asinf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(-0.0) == -0.0");
}

ATF_TC_BODY(asinf_zero_neg, tc)
{
	const float x = -0.0L;
	float y = asinf(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("asinf(-0.0) != -0.0");
}

ATF_TC(asinf_zero_pos);
ATF_TC_HEAD(asinf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test asinf(+0.0) == +0.0");
}

ATF_TC_BODY(asinf_zero_pos, tc)
{
	const float x = 0.0L;
	float y = asinf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("asinf(+0.0) != +0.0");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, asin_nan);
	ATF_TP_ADD_TC(tp, asin_inf_neg);
	ATF_TP_ADD_TC(tp, asin_inf_pos);
	ATF_TP_ADD_TC(tp, asin_range);
	ATF_TP_ADD_TC(tp, asin_inrange);
	ATF_TP_ADD_TC(tp, asin_zero_neg);
	ATF_TP_ADD_TC(tp, asin_zero_pos);

	ATF_TP_ADD_TC(tp, asinf_nan);
	ATF_TP_ADD_TC(tp, asinf_inf_neg);
	ATF_TP_ADD_TC(tp, asinf_inf_pos);
	ATF_TP_ADD_TC(tp, asinf_range);
	ATF_TP_ADD_TC(tp, asinf_inrange);
	ATF_TP_ADD_TC(tp, asinf_zero_neg);
	ATF_TP_ADD_TC(tp, asinf_zero_pos);

	return atf_no_error();
}
