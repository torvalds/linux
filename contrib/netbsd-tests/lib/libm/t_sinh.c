/* $NetBSD: t_sinh.c,v 1.6 2014/03/03 10:39:08 martin Exp $ */

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
__RCSID("$NetBSD: t_sinh.c,v 1.6 2014/03/03 10:39:08 martin Exp $");

#include <atf-c.h>
#include <math.h>
#include <stdio.h>

static const struct {
	double x;
	double y;
	double e;
} values[] = {
	{ -10,   -11013.23287470339, 1e4, },
	{ -2,    -3.626860407847019, 1,   },
	{ -1,    -1.175201193643801, 1,   },
	{ -0.05, -0.050020835937655, 1,   },
	{ -0.001,-0.001000000166667, 1,   },
	{  0.001, 0.001000000166667, 1,   },
	{  0.05,  0.050020835937655, 1,   },
	{  1,     1.175201193643801, 1,   },
	{  2,     3.626860407847019, 1,   },
	{  10,    11013.23287470339, 1e4, },
};

/*
 * sinh(3)
 */
ATF_TC(sinh_inrange);
ATF_TC_HEAD(sinh_inrange, tc)
{
	atf_tc_set_md_var(tc, "descr", "sinh(x) for some values");
}

ATF_TC_BODY(sinh_inrange, tc)
{
	double eps;
	double x;
	double y;
	size_t i;

	for (i = 0; i < __arraycount(values); i++) {
		x = values[i].x;
		y = values[i].y;
		eps = 1e-15 * values[i].e;

		if (fabs(sinh(x) - y) > eps)
			atf_tc_fail_nonfatal("sinh(%g) != %g\n", x, y);
	}
}

ATF_TC(sinh_nan);
ATF_TC_HEAD(sinh_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinh(NaN) == NaN");
}

ATF_TC_BODY(sinh_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(sinh(x)) != 0);
}

ATF_TC(sinh_inf_neg);
ATF_TC_HEAD(sinh_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinh(-Inf) == -Inf");
}

ATF_TC_BODY(sinh_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	double y = sinh(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) != 0);
}

ATF_TC(sinh_inf_pos);
ATF_TC_HEAD(sinh_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinh(+Inf) == +Inf");
}

ATF_TC_BODY(sinh_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = sinh(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(sinh_zero_neg);
ATF_TC_HEAD(sinh_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinh(-0.0) == -0.0");
}

ATF_TC_BODY(sinh_zero_neg, tc)
{
	const double x = -0.0L;
	double y = sinh(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("sinh(-0.0) != -0.0");
}

ATF_TC(sinh_zero_pos);
ATF_TC_HEAD(sinh_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinh(+0.0) == +0.0");
}

ATF_TC_BODY(sinh_zero_pos, tc)
{
	const double x = 0.0L;
	double y = sinh(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("sinh(+0.0) != +0.0");
}

/*
 * sinhf(3)
 */
ATF_TC(sinhf_inrange);
ATF_TC_HEAD(sinhf_inrange, tc)
{
	atf_tc_set_md_var(tc, "descr", "sinhf(x) for some values");
}

ATF_TC_BODY(sinhf_inrange, tc)
{
	float eps;
	float x;
	float y;
	size_t i;

	for (i = 0; i < __arraycount(values); i++) {
		x = values[i].x;
		y = values[i].y;
		eps = 1e-6 * values[i].e;

		if (fabsf(sinhf(x) - y) > eps)
			atf_tc_fail_nonfatal("sinhf(%g) != %g\n", x, y);
	}
}

ATF_TC(sinhf_nan);
ATF_TC_HEAD(sinhf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinhf(NaN) == NaN");
}

ATF_TC_BODY(sinhf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(sinhf(x)) != 0);
}

ATF_TC(sinhf_inf_neg);
ATF_TC_HEAD(sinhf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinhf(-Inf) == -Inf");
}

ATF_TC_BODY(sinhf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	float y = sinhf(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) != 0);
}

ATF_TC(sinhf_inf_pos);
ATF_TC_HEAD(sinhf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinhf(+Inf) == +Inf");
}

ATF_TC_BODY(sinhf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = sinhf(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(sinhf_zero_neg);
ATF_TC_HEAD(sinhf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinhf(-0.0) == -0.0");
}

ATF_TC_BODY(sinhf_zero_neg, tc)
{
	const float x = -0.0L;
	float y = sinhf(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("sinhf(-0.0) != -0.0");
}

ATF_TC(sinhf_zero_pos);
ATF_TC_HEAD(sinhf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test sinhf(+0.0) == +0.0");
}

ATF_TC_BODY(sinhf_zero_pos, tc)
{
	const float x = 0.0L;
	float y = sinhf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("sinhf(+0.0) != +0.0");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sinh_inrange);
	ATF_TP_ADD_TC(tp, sinh_nan);
	ATF_TP_ADD_TC(tp, sinh_inf_neg);
	ATF_TP_ADD_TC(tp, sinh_inf_pos);
	ATF_TP_ADD_TC(tp, sinh_zero_neg);
	ATF_TP_ADD_TC(tp, sinh_zero_pos);

	ATF_TP_ADD_TC(tp, sinhf_inrange);
	ATF_TP_ADD_TC(tp, sinhf_nan);
	ATF_TP_ADD_TC(tp, sinhf_inf_neg);
	ATF_TP_ADD_TC(tp, sinhf_inf_pos);
	ATF_TP_ADD_TC(tp, sinhf_zero_neg);
	ATF_TP_ADD_TC(tp, sinhf_zero_pos);

	return atf_no_error();
}
