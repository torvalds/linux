/* $NetBSD: t_tan.c,v 1.5 2014/03/03 10:39:08 martin Exp $ */

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
	int		angle;
	double		x;
	double		y;
} angles[] = {
	{ -180, -3.141592653589793,  0.0000000000000000 },
	{ -135, -2.356194490192345,  1.0000000000000000 },
	{  -45, -0.785398163397448, -1.0000000000000000 },
	{    0,  0.000000000000000,  0.0000000000000000 },
	{   30,  0.523598775598299,  0.5773502691896258 },
	{   45,  0.785398163397448,  1.0000000000000000 },
	{   60,  1.047197551196598,  1.7320508075688773 },
	{  120,  2.094395102393195, -1.7320508075688773 },
	{  135,  2.356194490192345, -1.0000000000000000 },
	{  150,  2.617993877991494, -0.5773502691896258 },
	{  180,  3.141592653589793,  0.0000000000000000 },
	{  360,  6.283185307179586,  0.0000000000000000 }
};

/*
 * tan(3)
 */
ATF_TC(tan_angles);
ATF_TC_HEAD(tan_angles, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some selected angles");
}

ATF_TC_BODY(tan_angles, tc)
{
	const double eps = 1.0e-14;
	size_t i;

	for (i = 0; i < __arraycount(angles); i++) {

		if (fabs(tan(angles[i].x) - angles[i].y) > eps)
			atf_tc_fail_nonfatal("tan(%d deg) != %0.01f",
			    angles[i].angle, angles[i].y);
	}
}

ATF_TC(tan_nan);
ATF_TC_HEAD(tan_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tan(NaN) == NaN");
}

ATF_TC_BODY(tan_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(tan(x)) != 0);
}

ATF_TC(tan_inf_neg);
ATF_TC_HEAD(tan_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tan(-Inf) == NaN");
}

ATF_TC_BODY(tan_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	ATF_CHECK(isnan(tan(x)) != 0);
}

ATF_TC(tan_inf_pos);
ATF_TC_HEAD(tan_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tan(+Inf) == NaN");
}

ATF_TC_BODY(tan_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(isnan(tan(x)) != 0);
}


ATF_TC(tan_zero_neg);
ATF_TC_HEAD(tan_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tan(-0.0) == -0.0");
}

ATF_TC_BODY(tan_zero_neg, tc)
{
	const double x = -0.0L;

	ATF_CHECK(tan(x) == x);
}

ATF_TC(tan_zero_pos);
ATF_TC_HEAD(tan_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tan(+0.0) == +0.0");
}

ATF_TC_BODY(tan_zero_pos, tc)
{
	const double x = 0.0L;

	ATF_CHECK(tan(x) == x);
}

/*
 * tanf(3)
 */
ATF_TC(tanf_angles);
ATF_TC_HEAD(tanf_angles, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some selected angles");
}

ATF_TC_BODY(tanf_angles, tc)
{
	const float eps = 1.0e-6;
	float x, y;
	size_t i;

	for (i = 0; i < __arraycount(angles); i++) {

		x = angles[i].x;
		y = angles[i].y;

		if (fabsf(tanf(x) - y) > eps)
			atf_tc_fail_nonfatal("tanf(%d deg) != %0.01f",
			    angles[i].angle, angles[i].y);
	}
}

ATF_TC(tanf_nan);
ATF_TC_HEAD(tanf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanf(NaN) == NaN");
}

ATF_TC_BODY(tanf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(tanf(x)) != 0);
}

ATF_TC(tanf_inf_neg);
ATF_TC_HEAD(tanf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanf(-Inf) == NaN");
}

ATF_TC_BODY(tanf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	if (isnan(tanf(x)) == 0) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("tanf(-Inf) != NaN");
	}
}

ATF_TC(tanf_inf_pos);
ATF_TC_HEAD(tanf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanf(+Inf) == NaN");
}

ATF_TC_BODY(tanf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	if (isnan(tanf(x)) == 0) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("tanf(+Inf) != NaN");
	}
}


ATF_TC(tanf_zero_neg);
ATF_TC_HEAD(tanf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanf(-0.0) == -0.0");
}

ATF_TC_BODY(tanf_zero_neg, tc)
{
	const float x = -0.0L;

	ATF_CHECK(tanf(x) == x);
}

ATF_TC(tanf_zero_pos);
ATF_TC_HEAD(tanf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanf(+0.0) == +0.0");
}

ATF_TC_BODY(tanf_zero_pos, tc)
{
	const float x = 0.0L;

	ATF_CHECK(tanf(x) == x);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, tan_angles);
	ATF_TP_ADD_TC(tp, tan_nan);
	ATF_TP_ADD_TC(tp, tan_inf_neg);
	ATF_TP_ADD_TC(tp, tan_inf_pos);
	ATF_TP_ADD_TC(tp, tan_zero_neg);
	ATF_TP_ADD_TC(tp, tan_zero_pos);

	ATF_TP_ADD_TC(tp, tanf_angles);
	ATF_TP_ADD_TC(tp, tanf_nan);
	ATF_TP_ADD_TC(tp, tanf_inf_neg);
	ATF_TP_ADD_TC(tp, tanf_inf_pos);
	ATF_TP_ADD_TC(tp, tanf_zero_neg);
	ATF_TP_ADD_TC(tp, tanf_zero_pos);

	return atf_no_error();
}
