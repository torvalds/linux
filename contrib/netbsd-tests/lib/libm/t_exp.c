/* $NetBSD: t_exp.c,v 1.8 2014/10/07 16:53:44 gson Exp $ */

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
#include "t_libm.h"

/* y = exp(x) */
static const struct {
	double x;
	double y;
	double e;
} exp_values[] = {
	{  -10, 0.4539992976248485e-4, 1e-4, },
	{   -5, 0.6737946999085467e-2, 1e-2, },
	{   -1, 0.3678794411714423,    1e-1, },
	{ -0.1, 0.9048374180359595,    1e-1, },
	{    0, 1.0000000000000000,    1,    },
	{  0.1, 1.1051709180756477,    1,    },
	{    1, 2.7182818284590452,    1,    },
	{    5, 148.41315910257660,    1e2, },
	{   10, 22026.465794806718,    1e4, },
};

/*
 * exp2/exp2f(3)
 */
ATF_LIBM_TEST(exp2_is_nan, "Test exp2(x) == NaN")
{
#ifdef T_LIBM_NAN
	T_LIBM_CHECK_NAN(0, exp2, T_LIBM_NAN);
	T_LIBM_CHECK_NAN(0, exp2f, T_LIBM_NAN);
#else
	atf_tc_skip("no NaN on this machine");
#endif
}

ATF_LIBM_TEST(exp2_is_plus_zero, "Test exp2(x) == +0.0")
{
#ifdef T_LIBM_MINUS_INF
	T_LIBM_CHECK_PLUS_ZERO(0, exp2, T_LIBM_MINUS_INF);
	T_LIBM_CHECK_PLUS_ZERO(0, exp2f, T_LIBM_MINUS_INF);
#else
	atf_tc_skip("no +/-Inf on this machine");
#endif
}

ATF_LIBM_TEST(exp2_powers, "Test exp2(x) is correct for some integer x")
{
	static const struct {
		double	x;
		double	d_y;
		double	f_y;
	} v[] = {
	    { +0.0,	1.0,	1.0 },
	    { -0.0,	1.0,	1.0 },
	    {    1,	0x1p1,	0x1p1 },
	    {    2,	0x1p2,	0x1p2 },
	    {  100,	0x1p100,	0x1p100 },
	    {  125,	0x1p125,	0x1p125 },
	    {  126,	0x1p126,	0x1p126 },
#if __DBL_MAX_EXP__ > 129
	    {  127,	0x1p127,	0x1p127 },
#endif
#ifdef T_LIBM_PLUS_INF
	    {  128,	0x1p128,	T_LIBM_PLUS_INF },
	    {  129,	0x1p129,	T_LIBM_PLUS_INF },
	    { 1000,	0x1p1000,	T_LIBM_PLUS_INF },
	    { 1020,	0x1p1020,	T_LIBM_PLUS_INF },
	    { 1023,	0x1p1023,	T_LIBM_PLUS_INF },
	    { 1024,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
	    { 1030,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
	    { 1050,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
	    { 2000,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
	    { 16383,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
	    { 16384,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
	    { 16385,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF },
#endif
	    {   -1,	0x1p-1,	0x1p-1 },
	    {   -2,	0x1p-2,	0x1p-2 },
	    { -100,	0x1p-100,	0x1p-100 },
	    { -127,	0x1p-127,	0x1p-127 },
	    { -128,	0x1p-128,	0x1p-128 },
#if __LDBL_MIN_EXP__ < -129
	    { -300,	0x1p-300,	0.0},
	    { -400,	0x1p-400,	0.0},
	    {-1000,	0x1p-1000,	0.0},
	    {-1022,	0x1p-1022,	0.0},
	    /* These should be denormal numbers */
	    {-1023,	0x1p-1023,	0.0},
	    {-1024,	0x1p-1024,	0.0},
	    {-1040,	0x1p-1040,	0.0},
	    {-1060,	0x1p-1060,	0.0},
	    /* This is the smallest result gcc will allow */
	    {-1074,	0x1p-1074,	0.0},
#endif
	    {-1075,	0x0,	0.0},
	    {-1080,	0x0,	0.0},
	    {-2000,	0x0,	0.0},
	    {-16382,	0x0,	0.0},
	    {-16383,	0x0,	0.0},
	    {-16384,	0x0,	0.0},
	};
	unsigned int i;

#if defined(__FreeBSD__) && defined(__i386__)
	atf_tc_expect_fail("a number of the assertions fail on i386");
#endif

	for (i = 0; i < __arraycount(v); i++) {
		T_LIBM_CHECK(i, exp2, v[i].x, v[i].d_y, 0.0);
		T_LIBM_CHECK(i, exp2f, v[i].x, v[i].f_y, 0.0);
	}
}

ATF_LIBM_TEST(exp2_values, "Test exp2(x) is correct for some x")
{
	static const struct {
		double	x;
		double	d_y;
		float   f_y;
		double	d_eps;
		double	f_eps;
	} v[] = {
#if __DBL_MAX_EXP__ > 128
	    /* The largest double constant */
	    { 0x1.fffffffffffffp9,	0x1.ffffffffffd3ap1023,	0.00,
		0x1p969,	0.0 },
	    /* The largest float constant */
	    { 0x1.fffffep6,	0x1.ffff4ep+127,	0x1.ffff4ep+127,	6e30,	0.0 },
#endif
#ifdef T_LIBM_PLUS_INF
	    { T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF,	T_LIBM_PLUS_INF,	0.0,	0.0 },
#endif

	    /* The few values from the old tests */
	    /* Results from i386/amd64, d_eps needed on i386 */
	    /* f_y values calculated using py-mpmath */
	    {  1.1,	0x1.125fbee250664p+1,	0x1.125fc0p+1,	0x1p-52,	0x1.8p-22 },
	    {  2.2,	0x1.2611186bae675p+2,	0x1.26111ap+2,	0x1p-51,	0x1.8p-21 },
	    {  3.3,	0x1.3b2c47bff8328p+3,	0x1.3b2c48p+3,	0x1p-50,	0x1.8p-20 },
	    {  4.4,	0x1.51cb453b9536ep+4,	0x1.51cb46p+4,	0x1p-49,	0x1.8p-19 },
	    {  5.5,	0x1.6a09e667f3bcdp+5,	0x1.6a09e6p+5,	0x1p-48,	0x1.8p-18 },
	    {  6.6,	0x1.8406003b2ae5bp+6,	0x1.8405fep+6,	0x1p-47,	0x1.8p-17 },
	    {  7.7,	0x1.9fdf8bcce533ep+7,	0x1.9fdf88p+7,	0x1p-46,	0x1.8p-16 },
	    {  8.8,	0x1.bdb8cdadbe124p+8,	0x1.bdb8d2p+8,	0x1p-45,	0x1.8p-15 },
	};
	unsigned int i;

	for (i = 0; i < __arraycount(v); i++) {
		T_LIBM_CHECK(i, exp2, v[i].x, v[i].d_y, v[i].d_eps);
		if (i > 1)
			T_LIBM_CHECK(i, exp2f, v[i].x, v[i].f_y, v[i].f_eps);
	}
}


/*
 * exp(3)
 */
ATF_TC(exp_nan);
ATF_TC_HEAD(exp_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test exp(NaN) == NaN");
}

ATF_TC_BODY(exp_nan, tc)
{
	const double x = 0.0L / 0.0L;

	if (isnan(exp(x)) == 0)
		atf_tc_fail_nonfatal("exp(NaN) != NaN");
}

ATF_TC(exp_inf_neg);
ATF_TC_HEAD(exp_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test exp(-Inf) == +0.0");
}

ATF_TC_BODY(exp_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;
	double y = exp(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("exp(-Inf) != +0.0");
}

ATF_TC(exp_inf_pos);
ATF_TC_HEAD(exp_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test exp(+Inf) == +Inf");
}

ATF_TC_BODY(exp_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = exp(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("exp(+Inf) != +Inf");
}

ATF_TC(exp_product);
ATF_TC_HEAD(exp_product, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some selected exp(x)");
}

ATF_TC_BODY(exp_product, tc)
{
	double eps;
	double x;
	double y;
	size_t i;

	for (i = 0; i < __arraycount(exp_values); i++) {
		x = exp_values[i].x;
		y = exp_values[i].y;
		eps = 1e-15 * exp_values[i].e;

		if (fabs(exp(x) - y) > eps)
			atf_tc_fail_nonfatal("exp(%0.01f) != %18.18e", x, y);
	}
}

ATF_TC(exp_zero_neg);
ATF_TC_HEAD(exp_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test exp(-0.0) == 1.0");
}

ATF_TC_BODY(exp_zero_neg, tc)
{
	const double x = -0.0L;

	if (fabs(exp(x) - 1.0) > 0.0)
		atf_tc_fail_nonfatal("exp(-0.0) != 1.0");
}

ATF_TC(exp_zero_pos);
ATF_TC_HEAD(exp_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test exp(+0.0) == 1.0");
}

ATF_TC_BODY(exp_zero_pos, tc)
{
	const double x = 0.0L;

	if (fabs(exp(x) - 1.0) > 0.0)
		atf_tc_fail_nonfatal("exp(+0.0) != 1.0");
}

/*
 * expf(3)
 */
ATF_TC(expf_nan);
ATF_TC_HEAD(expf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expf(NaN) == NaN");
}

ATF_TC_BODY(expf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	if (isnan(expf(x)) == 0)
		atf_tc_fail_nonfatal("expf(NaN) != NaN");
}

ATF_TC(expf_inf_neg);
ATF_TC_HEAD(expf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expf(-Inf) == +0.0");
}

ATF_TC_BODY(expf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;
	float y = expf(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("expf(-Inf) != +0.0");
}

ATF_TC(expf_inf_pos);
ATF_TC_HEAD(expf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expf(+Inf) == +Inf");
}

ATF_TC_BODY(expf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = expf(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("expf(+Inf) != +Inf");
}

ATF_TC(expf_product);
ATF_TC_HEAD(expf_product, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test some selected expf(x)");
}

ATF_TC_BODY(expf_product, tc)
{
	float eps;
	float x;
	float y;
	size_t i;

	for (i = 0; i < __arraycount(exp_values); i++) {
		x = exp_values[i].x;
		y = exp_values[i].y;
		eps = 1e-6 * exp_values[i].e;

		if (fabsf(expf(x) - y) > eps)
			atf_tc_fail_nonfatal("expf(%0.01f) != %18.18e", x, y);
	}
}

ATF_TC(expf_zero_neg);
ATF_TC_HEAD(expf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expf(-0.0) == 1.0");
}

ATF_TC_BODY(expf_zero_neg, tc)
{
	const float x = -0.0L;

	if (fabsf(expf(x) - 1.0f) > 0.0)
		atf_tc_fail_nonfatal("expf(-0.0) != 1.0");
}

ATF_TC(expf_zero_pos);
ATF_TC_HEAD(expf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expf(+0.0) == 1.0");
}

ATF_TC_BODY(expf_zero_pos, tc)
{
	const float x = 0.0L;

	if (fabsf(expf(x) - 1.0f) > 0.0)
		atf_tc_fail_nonfatal("expf(+0.0) != 1.0");
}

/*
 * expm1(3)
 */
ATF_TC(expm1_nan);
ATF_TC_HEAD(expm1_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1(NaN) == NaN");
}

ATF_TC_BODY(expm1_nan, tc)
{
	const double x = 0.0L / 0.0L;

	if (isnan(expm1(x)) == 0)
		atf_tc_fail_nonfatal("expm1(NaN) != NaN");
}

ATF_TC(expm1_inf_neg);
ATF_TC_HEAD(expm1_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1(-Inf) == -1");
}

ATF_TC_BODY(expm1_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	if (expm1(x) != -1.0)
		atf_tc_fail_nonfatal("expm1(-Inf) != -1.0");
}

ATF_TC(expm1_inf_pos);
ATF_TC_HEAD(expm1_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1(+Inf) == +Inf");
}

ATF_TC_BODY(expm1_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;
	double y = expm1(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("expm1(+Inf) != +Inf");
}

ATF_TC(expm1_zero_neg);
ATF_TC_HEAD(expm1_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1(-0.0) == -0.0");
}

ATF_TC_BODY(expm1_zero_neg, tc)
{
	const double x = -0.0L;
	double y = expm1(x);

	if (fabs(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("expm1(-0.0) != -0.0");
}

ATF_TC(expm1_zero_pos);
ATF_TC_HEAD(expm1_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1(+0.0) == 1.0");
}

ATF_TC_BODY(expm1_zero_pos, tc)
{
	const double x = 0.0L;
	double y = expm1(x);

	if (fabs(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("expm1(+0.0) != +0.0");
}

/*
 * expm1f(3)
 */
ATF_TC(expm1f_nan);
ATF_TC_HEAD(expm1f_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1f(NaN) == NaN");
}

ATF_TC_BODY(expm1f_nan, tc)
{
	const float x = 0.0L / 0.0L;

	if (isnan(expm1f(x)) == 0)
		atf_tc_fail_nonfatal("expm1f(NaN) != NaN");
}

ATF_TC(expm1f_inf_neg);
ATF_TC_HEAD(expm1f_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1f(-Inf) == -1");
}

ATF_TC_BODY(expm1f_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	if (expm1f(x) != -1.0)
		atf_tc_fail_nonfatal("expm1f(-Inf) != -1.0");
}

ATF_TC(expm1f_inf_pos);
ATF_TC_HEAD(expm1f_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1f(+Inf) == +Inf");
}

ATF_TC_BODY(expm1f_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;
	float y = expm1f(x);

	if (isinf(y) == 0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("expm1f(+Inf) != +Inf");
}

ATF_TC(expm1f_zero_neg);
ATF_TC_HEAD(expm1f_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1f(-0.0) == -0.0");
}

ATF_TC_BODY(expm1f_zero_neg, tc)
{
	const float x = -0.0L;
	float y = expm1f(x);

	if (fabsf(y) > 0.0 || signbit(y) == 0)
		atf_tc_fail_nonfatal("expm1f(-0.0) != -0.0");
}

ATF_TC(expm1f_zero_pos);
ATF_TC_HEAD(expm1f_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test expm1f(+0.0) == 1.0");
}

ATF_TC_BODY(expm1f_zero_pos, tc)
{
	const float x = 0.0L;
	float y = expm1f(x);

	if (fabsf(y) > 0.0 || signbit(y) != 0)
		atf_tc_fail_nonfatal("expm1f(+0.0) != +0.0");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, exp2_is_nan);
	ATF_TP_ADD_TC(tp, exp2_is_plus_zero);
	ATF_TP_ADD_TC(tp, exp2_values);
	ATF_TP_ADD_TC(tp, exp2_powers);

	ATF_TP_ADD_TC(tp, exp_nan);
	ATF_TP_ADD_TC(tp, exp_inf_neg);
	ATF_TP_ADD_TC(tp, exp_inf_pos);
	ATF_TP_ADD_TC(tp, exp_product);
	ATF_TP_ADD_TC(tp, exp_zero_neg);
	ATF_TP_ADD_TC(tp, exp_zero_pos);

	ATF_TP_ADD_TC(tp, expf_nan);
	ATF_TP_ADD_TC(tp, expf_inf_neg);
	ATF_TP_ADD_TC(tp, expf_inf_pos);
	ATF_TP_ADD_TC(tp, expf_product);
	ATF_TP_ADD_TC(tp, expf_zero_neg);
	ATF_TP_ADD_TC(tp, expf_zero_pos);

	ATF_TP_ADD_TC(tp, expm1_nan);
	ATF_TP_ADD_TC(tp, expm1_inf_neg);
	ATF_TP_ADD_TC(tp, expm1_inf_pos);
	ATF_TP_ADD_TC(tp, expm1_zero_neg);
	ATF_TP_ADD_TC(tp, expm1_zero_pos);

	ATF_TP_ADD_TC(tp, expm1f_nan);
	ATF_TP_ADD_TC(tp, expm1f_inf_neg);
	ATF_TP_ADD_TC(tp, expm1f_inf_pos);
	ATF_TP_ADD_TC(tp, expm1f_zero_neg);
	ATF_TP_ADD_TC(tp, expm1f_zero_pos);

	return atf_no_error();
}
