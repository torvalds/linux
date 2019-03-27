/* $NetBSD: t_cosh.c,v 1.6 2014/03/03 10:39:08 martin Exp $ */

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
__RCSID("$NetBSD: t_cosh.c,v 1.6 2014/03/03 10:39:08 martin Exp $");

#include <atf-c.h>
#include <math.h>
#include <stdio.h>

static const struct {
	double x;
	double y;
	double e;
} values[] = {
	{ -10,    11013.23292010332, 1e4, },
	{ -2,     3.762195691083631, 1,   },
	{ -1,     1.543080634815244, 1,   },
	{ -0.05,  1.001250260438369, 1,   },
	{ -0.001, 1.000000500000042, 1,   },
	{  0,     1,                 1,   },
	{  0.001, 1.000000500000042, 1,   },
	{  0.05,  1.001250260438369, 1,   },
	{  1,     1.543080634815244, 1,   },
	{  2,     3.762195691083631, 1,   },
	{  10,    11013.23292010332, 1e4, },
};

/*
 * cosh(3)
 */
ATF_TC(cosh_inrange);
ATF_TC_HEAD(cosh_inrange, tc)
{
	atf_tc_set_md_var(tc, "descr", "cosh(x) for some values");
}

ATF_TC_BODY(cosh_inrange, tc)
{
	double eps;
	double x;
	double y;
	size_t i;

	for (i = 0; i < __arraycount(values); i++) {
		x = values[i].x;
		y = values[i].y;
		eps = 1e-15 * values[i].e;

		if (fabs(cosh(x) - y) > eps)
			atf_tc_fail_nonfatal("cosh(%g) != %g\n", x, y);
	}
}

ATF_TC(cosh_nan);
ATF_TC_HEAD(cosh_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosh(NaN) == NaN");
}

ATF_TC_BODY(cosh_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(cosh(x)) != 0);
}

ATF_TC(cosh_inf_neg);
ATF_TC_HEAD(cosh_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosh(-Inf) == +Inf");
}

ATF_TC_BODY(cosh_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	double y = cosh(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(cosh_inf_pos);
ATF_TC_HEAD(cosh_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosh(+Inf) == +Inf");
}

ATF_TC_BODY(cosh_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = cosh(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(cosh_zero_neg);
ATF_TC_HEAD(cosh_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosh(-0.0) == 1.0");
}

ATF_TC_BODY(cosh_zero_neg, tc)
{
	const double x = -0.0L;

	if (cosh(x) != 1.0)
		atf_tc_fail_nonfatal("cosh(-0.0) != 1.0");
}

ATF_TC(cosh_zero_pos);
ATF_TC_HEAD(cosh_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosh(+0.0) == 1.0");
}

ATF_TC_BODY(cosh_zero_pos, tc)
{
	const double x = 0.0L;

	if (cosh(x) != 1.0)
		atf_tc_fail_nonfatal("cosh(+0.0) != 1.0");
}

/*
 * coshf(3)
 */
ATF_TC(coshf_inrange);
ATF_TC_HEAD(coshf_inrange, tc)
{
	atf_tc_set_md_var(tc, "descr", "coshf(x) for some values");
}

ATF_TC_BODY(coshf_inrange, tc)
{
	float eps;
	float x;
	float y;
	size_t i;

	for (i = 0; i < __arraycount(values); i++) {
		x = values[i].x;
		y = values[i].y;
		eps = 1e-6 * values[i].e;

		if (fabsf(coshf(x) - y) > eps)
			atf_tc_fail_nonfatal("coshf(%g) != %g\n", x, y);
	}
}

ATF_TC(coshf_nan);
ATF_TC_HEAD(coshf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coshf(NaN) == NaN");
}

ATF_TC_BODY(coshf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(coshf(x)) != 0);
}

ATF_TC(coshf_inf_neg);
ATF_TC_HEAD(coshf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coshf(-Inf) == +Inf");
}

ATF_TC_BODY(coshf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	float y = coshf(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(coshf_inf_pos);
ATF_TC_HEAD(coshf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coshf(+Inf) == +Inf");
}

ATF_TC_BODY(coshf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = coshf(x);

	ATF_CHECK(isinf(y) != 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TC(coshf_zero_neg);
ATF_TC_HEAD(coshf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coshf(-0.0) == 1.0");
}

ATF_TC_BODY(coshf_zero_neg, tc)
{
	const float x = -0.0L;

	if (coshf(x) != 1.0)
		atf_tc_fail_nonfatal("coshf(-0.0) != 1.0");
}

ATF_TC(coshf_zero_pos);
ATF_TC_HEAD(coshf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test coshf(+0.0) == 1.0");
}

ATF_TC_BODY(coshf_zero_pos, tc)
{
	const float x = 0.0L;

	if (coshf(x) != 1.0)
		atf_tc_fail_nonfatal("coshf(+0.0) != 1.0");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cosh_inrange);
	ATF_TP_ADD_TC(tp, cosh_nan);
	ATF_TP_ADD_TC(tp, cosh_inf_neg);
	ATF_TP_ADD_TC(tp, cosh_inf_pos);
	ATF_TP_ADD_TC(tp, cosh_zero_neg);
	ATF_TP_ADD_TC(tp, cosh_zero_pos);

	ATF_TP_ADD_TC(tp, coshf_inrange);
	ATF_TP_ADD_TC(tp, coshf_nan);
	ATF_TP_ADD_TC(tp, coshf_inf_neg);
	ATF_TP_ADD_TC(tp, coshf_inf_pos);
	ATF_TP_ADD_TC(tp, coshf_zero_neg);
	ATF_TP_ADD_TC(tp, coshf_zero_pos);

	return atf_no_error();
}
