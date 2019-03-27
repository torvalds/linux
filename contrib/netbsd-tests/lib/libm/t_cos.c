/* $NetBSD: t_cos.c,v 1.4 2014/03/03 10:39:08 martin Exp $ */

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
	{ -180, -3.141592653589793, -1.0000000000000000 },
	{ -135, -2.356194490192345, -0.7071067811865476 },
	{  -90, -1.570796326794897,  0.0000000000000000 },
	{  -45, -0.785398163397448,  0.7071067811865476 },
	{    0,  0.000000000000000,  1.0000000000000000 },
	{   30,  0.523598775598299,  0.8660254037844386 },
	{   45,  0.785398163397448,  0.7071067811865476 },
	{   60,  1.047197551196598,  0.5000000000000000 },
	{   90,  1.570796326794897,  0.0000000000000000 },
	{  120,  2.094395102393195, -0.5000000000000000 },
	{  135,  2.356194490192345, -0.7071067811865476 },
	{  150,  2.617993877991494, -0.8660254037844386 },
	{  180,  3.141592653589793, -1.0000000000000000 },
	{  270,  4.712388980384690,  0.0000000000000000 },
	{  360,  6.283185307179586,  1.0000000000000000 }
};

/*
 * cos(3)
 */
ATF_TC(cos_angles);
ATF_TC_HEAD(cos_angles, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some selected angles");
}

ATF_TC_BODY(cos_angles, tc)
{
	const double eps = 1.0e-15;
	size_t i;

	for (i = 0; i < __arraycount(angles); i++) {

		if (fabs(cos(angles[i].x) - angles[i].y) > eps)
			atf_tc_fail_nonfatal("cos(%d deg) != %0.01f",
			    angles[i].angle, angles[i].y);
	}
}

ATF_TC(cos_nan);
ATF_TC_HEAD(cos_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cos(NaN) == NaN");
}

ATF_TC_BODY(cos_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(cos(x)) != 0);
}

ATF_TC(cos_inf_neg);
ATF_TC_HEAD(cos_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cos(-Inf) == NaN");
}

ATF_TC_BODY(cos_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	ATF_CHECK(isnan(cos(x)) != 0);
}

ATF_TC(cos_inf_pos);
ATF_TC_HEAD(cos_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cos(+Inf) == NaN");
}

ATF_TC_BODY(cos_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(isnan(cos(x)) != 0);
}


ATF_TC(cos_zero_neg);
ATF_TC_HEAD(cos_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cos(-0.0) == 1.0");
}

ATF_TC_BODY(cos_zero_neg, tc)
{
	const double x = -0.0L;

	ATF_CHECK(cos(x) == 1.0);
}

ATF_TC(cos_zero_pos);
ATF_TC_HEAD(cos_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cos(+0.0) == 1.0");
}

ATF_TC_BODY(cos_zero_pos, tc)
{
	const double x = 0.0L;

	ATF_CHECK(cos(x) == 1.0);
}

/*
 * cosf(3)
 */
ATF_TC(cosf_angles);
ATF_TC_HEAD(cosf_angles, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some selected angles");
}

ATF_TC_BODY(cosf_angles, tc)
{
	const float eps = 1.0e-7;
	float x, y;
	size_t i;

	for (i = 0; i < __arraycount(angles); i++) {

		x = angles[i].x;
		y = angles[i].y;

		if (fabsf(cosf(x) - y) > eps)
			atf_tc_fail_nonfatal("cosf(%d deg) != %0.01f",
			    angles[i].angle, angles[i].y);
	}
}

ATF_TC(cosf_nan);
ATF_TC_HEAD(cosf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosf(NaN) == NaN");
}

ATF_TC_BODY(cosf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(cosf(x)) != 0);
}

ATF_TC(cosf_inf_neg);
ATF_TC_HEAD(cosf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosf(-Inf) == NaN");
}

ATF_TC_BODY(cosf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	if (isnan(cosf(x)) == 0) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("cosf(-Inf) != NaN");
	}
}

ATF_TC(cosf_inf_pos);
ATF_TC_HEAD(cosf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosf(+Inf) == NaN");
}

ATF_TC_BODY(cosf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	if (isnan(cosf(x)) == 0) {
		atf_tc_expect_fail("PR lib/45362");
		atf_tc_fail("cosf(+Inf) != NaN");
	}
}


ATF_TC(cosf_zero_neg);
ATF_TC_HEAD(cosf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosf(-0.0) == 1.0");
}

ATF_TC_BODY(cosf_zero_neg, tc)
{
	const float x = -0.0L;

	ATF_CHECK(cosf(x) == 1.0);
}

ATF_TC(cosf_zero_pos);
ATF_TC_HEAD(cosf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test cosf(+0.0) == 1.0");
}

ATF_TC_BODY(cosf_zero_pos, tc)
{
	const float x = 0.0L;

	ATF_CHECK(cosf(x) == 1.0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, cos_angles);
	ATF_TP_ADD_TC(tp, cos_nan);
	ATF_TP_ADD_TC(tp, cos_inf_neg);
	ATF_TP_ADD_TC(tp, cos_inf_pos);
	ATF_TP_ADD_TC(tp, cos_zero_neg);
	ATF_TP_ADD_TC(tp, cos_zero_pos);

	ATF_TP_ADD_TC(tp, cosf_angles);
	ATF_TP_ADD_TC(tp, cosf_nan);
	ATF_TP_ADD_TC(tp, cosf_inf_neg);
	ATF_TP_ADD_TC(tp, cosf_inf_pos);
	ATF_TP_ADD_TC(tp, cosf_zero_neg);
	ATF_TP_ADD_TC(tp, cosf_zero_pos);

	return atf_no_error();
}
