/* $NetBSD: t_tanh.c,v 1.7 2014/03/03 10:39:08 martin Exp $ */

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
__RCSID("$NetBSD: t_tanh.c,v 1.7 2014/03/03 10:39:08 martin Exp $");

#include <atf-c.h>
#include <math.h>

/*
 * tanh(3)
 */
ATF_TC(tanh_nan);
ATF_TC_HEAD(tanh_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanh(NaN) == NaN");
}

ATF_TC_BODY(tanh_nan, tc)
{
	const double x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(tanh(x)) != 0);
}

ATF_TC(tanh_inf_neg);
ATF_TC_HEAD(tanh_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanh(-Inf) == -1.0");
}

ATF_TC_BODY(tanh_inf_neg, tc)
{
	const double x = -1.0L / 0.0L;

	ATF_CHECK(tanh(x) == -1.0);
}

ATF_TC(tanh_inf_pos);
ATF_TC_HEAD(tanh_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanh(+Inf) == +1.0");
}

ATF_TC_BODY(tanh_inf_pos, tc)
{
	const double x = 1.0L / 0.0L;

	ATF_CHECK(tanh(x) == 1.0);
}

ATF_TC(tanh_zero_neg);
ATF_TC_HEAD(tanh_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanh(-0.0) == -0.0");
}

ATF_TC_BODY(tanh_zero_neg, tc)
{
	const double x = -0.0L;
	double y = tanh(x);

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) != 0);

	ATF_REQUIRE_MSG(signbit(y) != 0,
	    "compiler bug, waiting for newer gcc import, see PR lib/44057");
}

ATF_TC(tanh_zero_pos);
ATF_TC_HEAD(tanh_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanh(+0.0) == +0.0");
}

ATF_TC_BODY(tanh_zero_pos, tc)
{
	const double x = 0.0L;
	double y = tanh(x);

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

/*
 * tanhf(3)
 */
ATF_TC(tanhf_nan);
ATF_TC_HEAD(tanhf_nan, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanhf(NaN) == NaN");
}

ATF_TC_BODY(tanhf_nan, tc)
{
	const float x = 0.0L / 0.0L;

	ATF_CHECK(isnan(x) != 0);
	ATF_CHECK(isnan(tanhf(x)) != 0);
}

ATF_TC(tanhf_inf_neg);
ATF_TC_HEAD(tanhf_inf_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanhf(-Inf) == -1.0");
}

ATF_TC_BODY(tanhf_inf_neg, tc)
{
	const float x = -1.0L / 0.0L;

	ATF_CHECK(tanhf(x) == -1.0);
}

ATF_TC(tanhf_inf_pos);
ATF_TC_HEAD(tanhf_inf_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanhf(+Inf) == +1.0");
}

ATF_TC_BODY(tanhf_inf_pos, tc)
{
	const float x = 1.0L / 0.0L;

	ATF_CHECK(tanhf(x) == 1.0);
}

ATF_TC(tanhf_zero_neg);
ATF_TC_HEAD(tanhf_zero_neg, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanhf(-0.0) == -0.0");
}

ATF_TC_BODY(tanhf_zero_neg, tc)
{
	const float x = -0.0L;
	float y = tanh(x);

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) != 0);

	ATF_REQUIRE_MSG(signbit(y) != 0,
	    "compiler bug, waiting for newer gcc import, see PR lib/44057");
}

ATF_TC(tanhf_zero_pos);
ATF_TC_HEAD(tanhf_zero_pos, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test tanhf(+0.0) == +0.0");
}

ATF_TC_BODY(tanhf_zero_pos, tc)
{
	const float x = 0.0L;
	float y = tanhf(x);

	ATF_CHECK(x == y);
	ATF_CHECK(signbit(x) == 0);
	ATF_CHECK(signbit(y) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, tanh_nan);
	ATF_TP_ADD_TC(tp, tanh_inf_neg);
	ATF_TP_ADD_TC(tp, tanh_inf_pos);
	ATF_TP_ADD_TC(tp, tanh_zero_neg);
	ATF_TP_ADD_TC(tp, tanh_zero_pos);

	ATF_TP_ADD_TC(tp, tanhf_nan);
	ATF_TP_ADD_TC(tp, tanhf_inf_neg);
	ATF_TP_ADD_TC(tp, tanhf_inf_pos);
	ATF_TP_ADD_TC(tp, tanhf_zero_neg);
	ATF_TP_ADD_TC(tp, tanhf_zero_pos);

	return atf_no_error();
}
