/* $NetBSD: t_acos.c,v 1.10 2014/03/05 20:14:46 dsl Exp $ */

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

/*
 * acos(3) and acosf(3)
 */

ATF_LIBM_TEST(acos_is_nan, "Test acos/acosf(x) == NaN, x = NaN, +/-Inf, ![-1..1]")
{
	static const double x[] = {
	    -1.000000001, 1.000000001,
	    -1.0000001, 1.0000001,
	    -1.1, 1.1,
#ifndef __vax__
	    T_LIBM_NAN,
	    T_LIBM_MINUS_INF, T_LIBM_PLUS_INF,
#endif
	};
	unsigned int i;

	for (i = 0; i < __arraycount(x); i++) {
		T_LIBM_CHECK_NAN(i, acos, x[i]);
		if (i < 2)
			/* Values are too small for float */
			continue;
		T_LIBM_CHECK_NAN(i, acosf, x[i]);
	}
}

ATF_LIBM_TEST(acos_inrange, "Test acos/acosf(x) for some valid values")
{
	static const struct {
		double x;
		double y;
	} values[] = {
		{ -1,    M_PI,              },
		{ -0.99, 3.000053180265366, },
		{ -0.5,  2.094395102393195, },
		{ -0.1,  1.670963747956456, },
		{  0,    M_PI / 2,          },
		{  0.1,  1.470628905633337, },
		{  0.5,  1.047197551196598, },
		{  0.99, 0.141539473324427, },
	};
	unsigned int i;

	/*
	 * Note that acos(x) might be calculated as atan2(sqrt(1-x*x),x).
	 * This means that acos(-1) is atan2(+0,-1), if the sign is wrong
	 * the value will be -M_PI (atan2(-0,-1)) not M_PI.
	 */

	for (i = 0; i < __arraycount(values); i++) {
		T_LIBM_CHECK(i, acos, values[i].x, values[i].y, 1.0e-15);
		T_LIBM_CHECK(i, acosf, values[i].x, values[i].y, 1.0e-5);
	}
}

ATF_LIBM_TEST(acos_is_plus_zero, "Test acosf(1.0) == +0.0")
{
	T_LIBM_CHECK_PLUS_ZERO(0, acos, 1.0);
	T_LIBM_CHECK_PLUS_ZERO(0, acosf, 1.0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, acos_inrange);
	ATF_TP_ADD_TC(tp, acos_is_nan);
	ATF_TP_ADD_TC(tp, acos_is_plus_zero);

	return atf_no_error();
}
