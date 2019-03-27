/* $NetBSD: t_pow.c,v 1.5 2017/01/20 21:15:56 maya Exp $ */

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
__RCSID("$NetBSD: t_pow.c,v 1.5 2017/01/20 21:15:56 maya Exp $");

#include <atf-c.h>
#include <math.h>

/*
 * pow(3)
 */
ATF_TC(pow_nan_x);
ATF_TC_HEAD(pow_nan_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(NaN, y) == NaN");
}

ATF_TC_BODY(pow_nan_x, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(pow(x, 2.0)) != 0);
}

ATF_TC(pow_nan_y);
ATF_TC_HEAD(pow_nan_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(x, NaN) == NaN");
}

ATF_TC_BODY(pow_nan_y, tc)
{
	const double y = 0.0L / 0.0L;

	ATF_CHECK(isnan(pow(2.0, y)) != 0);
}

ATF_TC(pow_inf_neg_x);
ATF_TC_HEAD(pow_inf_neg_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(-Inf, y) == +-Inf || +-0.0");
}

ATF_TC_BODY(pow_inf_neg_x, tc)
{
	const double x = -1.0L / 0.0L;
	double z;

	/*
	 * If y is odd, y > 0, and x is -Inf, -Inf is returned.
	 * If y is even, y > 0, and x is -Inf, +Inf is returned.
	 */
	z = pow(x, 3.0);

	if (isinf(z) == 0 || signbit(z) == 0)
		atf_tc_fail_nonfatal("pow(-Inf, 3.0) != -Inf");

	z = pow(x, 4.0);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(-Inf, 4.0) != +Inf");

	/*
	 * If y is odd, y < 0, and x is -Inf, -0.0 is returned.
	 * If y is even, y < 0, and x is -Inf, +0.0 is returned.
	 */
	z = pow(x, -3.0);

	if (fabs(z) > 0.0 || signbit(z) == 0)
		atf_tc_fail_nonfatal("pow(-Inf, -3.0) != -0.0");

	z = pow(x, -4.0);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(-Inf -4.0) != +0.0");
}

ATF_TC(pow_inf_neg_y);
ATF_TC_HEAD(pow_inf_neg_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(x, -Inf) == +Inf || +0.0");
}

ATF_TC_BODY(pow_inf_neg_y, tc)
{
	const double y = -1.0L / 0.0L;
	double z;

	/*
	 * If |x| < 1 and y is -Inf, +Inf is returned.
	 * If |x| > 1 and y is -Inf, +0.0 is returned.
	 */
	z = pow(0.1, y);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(0.1, -Inf) != +Inf");

	z = pow(1.1, y);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(1.1, -Inf) != +0.0");
}

ATF_TC(pow_inf_pos_x);
ATF_TC_HEAD(pow_inf_pos_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(+Inf, y) == +Inf || +0.0");
}

ATF_TC_BODY(pow_inf_pos_x, tc)
{
	const double x = 1.0L / 0.0L;
	double z;

	/*
	 * For y < 0, if x is +Inf, +0.0 is returned.
	 * For y > 0, if x is +Inf, +Inf is returned.
	 */
	z = pow(x, -2.0);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(+Inf, -2.0) != +0.0");

	z = pow(x, 2.0);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(+Inf, 2.0) != +Inf");
}

ATF_TC(pow_inf_pos_y);
ATF_TC_HEAD(pow_inf_pos_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(x, +Inf) == +Inf || +0.0");
}

ATF_TC_BODY(pow_inf_pos_y, tc)
{
	const double y = 1.0L / 0.0L;
	double z;

	/*
	 * If |x| < 1 and y is +Inf, +0.0 is returned.
	 * If |x| > 1 and y is +Inf, +Inf is returned.
	 */
	z = pow(0.1, y);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(0.1, +Inf) != +0.0");

	z = pow(1.1, y);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(1.1, +Inf) != +Inf");
}

ATF_TC(pow_one_neg_x);
ATF_TC_HEAD(pow_one_neg_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(-1.0, +-Inf) == 1.0");
}

ATF_TC_BODY(pow_one_neg_x, tc)
{
	const double infp = 1.0L / 0.0L;
	const double infn = -1.0L / 0.0L;

	/*
	 * If x is -1.0, and y is +-Inf, 1.0 shall be returned.
	 */
	ATF_REQUIRE(isinf(infp) != 0);
	ATF_REQUIRE(isinf(infn) != 0);

	if (pow(-1.0, infp) != 1.0) {
		atf_tc_expect_fail("PR lib/45372");
		atf_tc_fail_nonfatal("pow(-1.0, +Inf) != 1.0");
	}

	if (pow(-1.0, infn) != 1.0) {
		atf_tc_expect_fail("PR lib/45372");
		atf_tc_fail_nonfatal("pow(-1.0, -Inf) != 1.0");
	}
}

ATF_TC(pow_one_pos_x);
ATF_TC_HEAD(pow_one_pos_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(1.0, y) == 1.0");
}

ATF_TC_BODY(pow_one_pos_x, tc)
{
	const double y[] = { 0.0, 0.1, 2.0, -3.0, 99.0, 99.99, 9999999.9 };
	const double z = 0.0L / 0.0L;
	size_t i;

	/*
	 * For any value of y (including NaN),
	 * if x is 1.0, 1.0 shall be returned.
	 */
	if (pow(1.0, z) != 1.0)
		atf_tc_fail_nonfatal("pow(1.0, NaN) != 1.0");

	for (i = 0; i < __arraycount(y); i++) {

		if (pow(1.0, y[i]) != 1.0)
			atf_tc_fail_nonfatal("pow(1.0, %0.01f) != 1.0", y[i]);
	}
}

ATF_TC(pow_zero_x);
ATF_TC_HEAD(pow_zero_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(+-0.0, y) == +-0.0 || HUGE");
}

ATF_TC_BODY(pow_zero_x, tc)
{
	double z;

	/*
	 * If x is +0.0 or -0.0, y > 0, and y
	 * is an odd integer, x is returned.
	 */
	z = pow(+0.0, 3.0);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(+0.0, 3.0) != +0.0");

	z = pow(-0.0, 3.0);

	if (fabs(z) > 0.0 || signbit(z) == 0)
		atf_tc_fail_nonfatal("pow(-0.0, 3.0) != -0.0");

	/*
	 * If y > 0 and not an odd integer,
	 * if x is +0.0 or -0.0, +0.0 is returned.
	 */
	z = pow(+0.0, 4.0);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(+0.0, 4.0) != +0.0");

	z = pow(-0.0, 4.0);

	if (fabs(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("pow(-0.0, 4.0) != +0.0");

	/*
	 * If y < 0 and x is +0.0 or -0.0, either +-HUGE_VAL,
	 * +-HUGE_VALF, or +-HUGE_VALL shall be returned.
	 */
	z = pow(+0.0, -4.0);

	if (z != HUGE_VAL) {
		atf_tc_fail_nonfatal("pow(+0.0, -4.0) != HUGE_VAL");
	}

	z = pow(-0.0, -4.0);

	if (z != HUGE_VAL) {
		atf_tc_fail_nonfatal("pow(-0.0, -4.0) != HUGE_VAL");
	}

	z = pow(+0.0, -5.0);

	if (z != HUGE_VAL) {
		atf_tc_fail_nonfatal("pow(+0.0, -5.0) != HUGE_VAL");
	}

	z = pow(-0.0, -5.0);

	if (z != -HUGE_VAL)
		atf_tc_fail_nonfatal("pow(-0.0, -5.0) != -HUGE_VAL");
}

ATF_TC(pow_zero_y);
ATF_TC_HEAD(pow_zero_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test pow(x, +-0.0) == 1.0");
}

ATF_TC_BODY(pow_zero_y, tc)
{
	const double x[] =  { 0.1, -3.0, 77.0, 99.99, 101.0000001 };
	const double z = 0.0L / 0.0L;
	size_t i;

	/*
	 * For any value of x (including NaN),
	 * if y is +0.0 or -0.0, 1.0 is returned.
	 */
	if (pow(z, +0.0) != 1.0)
		atf_tc_fail_nonfatal("pow(NaN, +0.0) != 1.0");

	if (pow(z, -0.0) != 1.0)
		atf_tc_fail_nonfatal("pow(NaN, -0.0) != 1.0");

	for (i = 0; i < __arraycount(x); i++) {

		if (pow(x[i], +0.0) != 1.0)
			atf_tc_fail_nonfatal("pow(%0.01f, +0.0) != 1.0", x[i]);

		if (pow(x[i], -0.0) != 1.0)
			atf_tc_fail_nonfatal("pow(%0.01f, -0.0) != 1.0", x[i]);
	}
}

/*
 * powf(3)
 */
ATF_TC(powf_nan_x);
ATF_TC_HEAD(powf_nan_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(NaN, y) == NaN");
}

ATF_TC_BODY(powf_nan_x, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnanf(powf(x, 2.0)) != 0);
}

ATF_TC(powf_nan_y);
ATF_TC_HEAD(powf_nan_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(x, NaN) == NaN");
}

ATF_TC_BODY(powf_nan_y, tc)
{
	const float y = 0.0L / 0.0L;

	ATF_CHECK(isnanf(powf(2.0, y)) != 0);
}

ATF_TC(powf_inf_neg_x);
ATF_TC_HEAD(powf_inf_neg_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(-Inf, y) == +-Inf || +-0.0");
}

ATF_TC_BODY(powf_inf_neg_x, tc)
{
	const float x = -1.0L / 0.0L;
	float z;

	/*
	 * If y is odd, y > 0, and x is -Inf, -Inf is returned.
	 * If y is even, y > 0, and x is -Inf, +Inf is returned.
	 */
	z = powf(x, 3.0);

	if (isinf(z) == 0 || signbit(z) == 0)
		atf_tc_fail_nonfatal("powf(-Inf, 3.0) != -Inf");

	z = powf(x, 4.0);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(-Inf, 4.0) != +Inf");

	/*
	 * If y is odd, y < 0, and x is -Inf, -0.0 is returned.
	 * If y is even, y < 0, and x is -Inf, +0.0 is returned.
	 */
	z = powf(x, -3.0);

	if (fabsf(z) > 0.0 || signbit(z) == 0) {
		atf_tc_expect_fail("PR lib/45372");
		atf_tc_fail_nonfatal("powf(-Inf, -3.0) != -0.0");
	}

	z = powf(x, -4.0);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(-Inf -4.0) != +0.0");
}

ATF_TC(powf_inf_neg_y);
ATF_TC_HEAD(powf_inf_neg_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(x, -Inf) == +Inf || +0.0");
}

ATF_TC_BODY(powf_inf_neg_y, tc)
{
	const float y = -1.0L / 0.0L;
	float z;

	/*
	 * If |x| < 1 and y is -Inf, +Inf is returned.
	 * If |x| > 1 and y is -Inf, +0.0 is returned.
	 */
	z = powf(0.1, y);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(0.1, -Inf) != +Inf");

	z = powf(1.1, y);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(1.1, -Inf) != +0.0");
}

ATF_TC(powf_inf_pos_x);
ATF_TC_HEAD(powf_inf_pos_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(+Inf, y) == +Inf || +0.0");
}

ATF_TC_BODY(powf_inf_pos_x, tc)
{
	const float x = 1.0L / 0.0L;
	float z;

	/*
	 * For y < 0, if x is +Inf, +0.0 is returned.
	 * For y > 0, if x is +Inf, +Inf is returned.
	 */
	z = powf(x, -2.0);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(+Inf, -2.0) != +0.0");

	z = powf(x, 2.0);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(+Inf, 2.0) != +Inf");
}

ATF_TC(powf_inf_pos_y);
ATF_TC_HEAD(powf_inf_pos_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(x, +Inf) == +Inf || +0.0");
}

ATF_TC_BODY(powf_inf_pos_y, tc)
{
	const float y = 1.0L / 0.0L;
	float z;

	/*
	 * If |x| < 1 and y is +Inf, +0.0 is returned.
	 * If |x| > 1 and y is +Inf, +Inf is returned.
	 */
	z = powf(0.1, y);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(0.1, +Inf) != +0.0");

	z = powf(1.1, y);

	if (isinf(z) == 0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(1.1, +Inf) != +Inf");
}

ATF_TC(powf_one_neg_x);
ATF_TC_HEAD(powf_one_neg_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(-1.0, +-Inf) == 1.0");
}

ATF_TC_BODY(powf_one_neg_x, tc)
{
	const float infp = 1.0L / 0.0L;
	const float infn = -1.0L / 0.0L;

	/*
	 * If x is -1.0, and y is +-Inf, 1.0 shall be returned.
	 */
	ATF_REQUIRE(isinf(infp) != 0);
	ATF_REQUIRE(isinf(infn) != 0);

	if (powf(-1.0, infp) != 1.0) {
		atf_tc_expect_fail("PR lib/45372");
		atf_tc_fail_nonfatal("powf(-1.0, +Inf) != 1.0");
	}

	if (powf(-1.0, infn) != 1.0) {
		atf_tc_expect_fail("PR lib/45372");
		atf_tc_fail_nonfatal("powf(-1.0, -Inf) != 1.0");
	}
}

ATF_TC(powf_one_pos_x);
ATF_TC_HEAD(powf_one_pos_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(1.0, y) == 1.0");
}

ATF_TC_BODY(powf_one_pos_x, tc)
{
	const float y[] = { 0.0, 0.1, 2.0, -3.0, 99.0, 99.99, 9999999.9 };
	const float z = 0.0L / 0.0L;
	size_t i;

	/*
	 * For any value of y (including NaN),
	 * if x is 1.0, 1.0 shall be returned.
	 */
	if (powf(1.0, z) != 1.0)
		atf_tc_fail_nonfatal("powf(1.0, NaN) != 1.0");

	for (i = 0; i < __arraycount(y); i++) {

		if (powf(1.0, y[i]) != 1.0)
			atf_tc_fail_nonfatal("powf(1.0, %0.01f) != 1.0", y[i]);
	}
}

ATF_TC(powf_zero_x);
ATF_TC_HEAD(powf_zero_x, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(+-0.0, y) == +-0.0 || HUGE");
}

ATF_TC_BODY(powf_zero_x, tc)
{
	float z;

	/*
	 * If x is +0.0 or -0.0, y > 0, and y
	 * is an odd integer, x is returned.
	 */
	z = powf(+0.0, 3.0);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(+0.0, 3.0) != +0.0");

	z = powf(-0.0, 3.0);

	if (fabsf(z) > 0.0 || signbit(z) == 0)
		atf_tc_fail_nonfatal("powf(-0.0, 3.0) != -0.0");

	/*
	 * If y > 0 and not an odd integer,
	 * if x is +0.0 or -0.0, +0.0 is returned.
	 */
	z = powf(+0.0, 4.0);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(+0.0, 4.0) != +0.0");

	z = powf(-0.0, 4.0);

	if (fabsf(z) > 0.0 || signbit(z) != 0)
		atf_tc_fail_nonfatal("powf(-0.0, 4.0) != +0.0");

	/*
	 * If y < 0 and x is +0.0 or -0.0, either +-HUGE_VAL,
	 * +-HUGE_VALF, or +-HUGE_VALL shall be returned.
	 */
	z = powf(+0.0, -4.0);

	if (z != HUGE_VALF) {
		atf_tc_expect_fail("PR port-amd64/45391");
		atf_tc_fail_nonfatal("powf(+0.0, -4.0) != HUGE_VALF");
	}

	z = powf(-0.0, -4.0);

	if (z != HUGE_VALF) {
		atf_tc_expect_fail("PR port-amd64/45391");
		atf_tc_fail_nonfatal("powf(-0.0, -4.0) != HUGE_VALF");
	}

	z = powf(+0.0, -5.0);

	if (z != HUGE_VALF) {
		atf_tc_expect_fail("PR port-amd64/45391");
		atf_tc_fail_nonfatal("powf(+0.0, -5.0) != HUGE_VALF");
	}

	z = powf(-0.0, -5.0);

	if (z != -HUGE_VALF)
		atf_tc_fail_nonfatal("powf(-0.0, -5.0) != -HUGE_VALF");
}

ATF_TC(powf_zero_y);
ATF_TC_HEAD(powf_zero_y, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test powf(x, +-0.0) == 1.0");
}

ATF_TC_BODY(powf_zero_y, tc)
{
	const float x[] =  { 0.1, -3.0, 77.0, 99.99, 101.0000001 };
	const float z = 0.0L / 0.0L;
	size_t i;

	/*
	 * For any value of x (including NaN),
	 * if y is +0.0 or -0.0, 1.0 is returned.
	 */
	if (powf(z, +0.0) != 1.0)
		atf_tc_fail_nonfatal("powf(NaN, +0.0) != 1.0");

	if (powf(z, -0.0) != 1.0)
		atf_tc_fail_nonfatal("powf(NaN, -0.0) != 1.0");

	for (i = 0; i < __arraycount(x); i++) {

		if (powf(x[i], +0.0) != 1.0)
			atf_tc_fail_nonfatal("powf(%0.01f, +0.0) != 1.0",x[i]);

		if (powf(x[i], -0.0) != 1.0)
			atf_tc_fail_nonfatal("powf(%0.01f, -0.0) != 1.0",x[i]);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, pow_nan_x);
	ATF_TP_ADD_TC(tp, pow_nan_y);
	ATF_TP_ADD_TC(tp, pow_inf_neg_x);
	ATF_TP_ADD_TC(tp, pow_inf_neg_y);
	ATF_TP_ADD_TC(tp, pow_inf_pos_x);
	ATF_TP_ADD_TC(tp, pow_inf_pos_y);
	ATF_TP_ADD_TC(tp, pow_one_neg_x);
	ATF_TP_ADD_TC(tp, pow_one_pos_x);
	ATF_TP_ADD_TC(tp, pow_zero_x);
	ATF_TP_ADD_TC(tp, pow_zero_y);

	ATF_TP_ADD_TC(tp, powf_nan_x);
	ATF_TP_ADD_TC(tp, powf_nan_y);
	ATF_TP_ADD_TC(tp, powf_inf_neg_x);
	ATF_TP_ADD_TC(tp, powf_inf_neg_y);
	ATF_TP_ADD_TC(tp, powf_inf_pos_x);
	ATF_TP_ADD_TC(tp, powf_inf_pos_y);
	ATF_TP_ADD_TC(tp, powf_one_neg_x);
	ATF_TP_ADD_TC(tp, powf_one_pos_x);
	ATF_TP_ADD_TC(tp, powf_zero_x);
	ATF_TP_ADD_TC(tp, powf_zero_y);

	return atf_no_error();
}
