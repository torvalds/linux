/* $NetBSD: t_atan.c,v 1.15 2014/03/17 11:08:11 martin Exp $ */

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

static const struct {
	double x;
	double y;
} values[] = {
#ifndef __vax__
	/* vax has no +/- INF */
	{ T_LIBM_MINUS_INF, -M_PI / 2 },
	{ T_LIBM_PLUS_INF,   M_PI / 2 },
#endif
	{ -100, -1.560796660108231, },
	{  -10, -1.471127674303735, },
	{   -1, -M_PI / 4, },
	{ -0.1, -0.09966865249116204, },
	{  0.1,  0.09966865249116204, },
	{    1,  M_PI / 4, },
	{   10,  1.471127674303735, },
	{  100,  1.560796660108231, },
};

/*
 * atan(3)
 */
ATF_LIBM_TEST(atan_nan, "Test atan/atanf(NaN) == NaN")
{
#ifdef T_LIBM_NAN
	T_LIBM_CHECK_NAN(0, atan, T_LIBM_NAN);
	T_LIBM_CHECK_NAN(0, atanf, T_LIBM_NAN);
#else
	atf_tc_skip("no NaN on this machine");
#endif
}

ATF_LIBM_TEST(atan_inrange, "Test atan/atanf(x) for some values")
{
	unsigned int i;

	for (i = 0; i < __arraycount(values); i++) {
		T_LIBM_CHECK(i, atan, values[i].x, values[i].y, 1.0e-15);
		T_LIBM_CHECK(i, atanf, values[i].x, values[i].y, 1.0e-7);
	}
}

ATF_LIBM_TEST(atan_zero_neg, "Test atan/atanf(-0.0) == -0.0")
{

	T_LIBM_CHECK_MINUS_ZERO(0, atan, -0.0);
	T_LIBM_CHECK_MINUS_ZERO(0, atanf, -0.0);
}

ATF_LIBM_TEST(atan_zero_pos, "Test atan/atanf(+0.0) == +0.0")
{

	T_LIBM_CHECK_PLUS_ZERO(0, atan, +0.0);
	T_LIBM_CHECK_PLUS_ZERO(0, atanf, +0.0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, atan_nan);
	ATF_TP_ADD_TC(tp, atan_inrange);
	ATF_TP_ADD_TC(tp, atan_zero_neg);
	ATF_TP_ADD_TC(tp, atan_zero_pos);

	return atf_no_error();
}
