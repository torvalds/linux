/* $NetBSD: t_round.c,v 1.4 2013/11/11 23:57:34 joerg Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
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
#include <float.h>
#include <math.h>

/*
 * This tests for a bug in the initial implementation where
 * precision was lost in an internal substraction, leading to
 * rounding into the wrong direction.
 */

/* 0.5 - EPSILON */
#define VAL	0x0.7ffffffffffffcp0
#define VALF	0x0.7fffff8p0
#define VALL	(0.5 - LDBL_EPSILON)

#ifdef __vax__
#define SMALL_NUM	1.0e-38
#else
#define SMALL_NUM	1.0e-40
#endif

ATF_TC(round_dir);
ATF_TC_HEAD(round_dir, tc)
{
	atf_tc_set_md_var(tc, "descr","Check for rounding in wrong direction");
}

ATF_TC_BODY(round_dir, tc)
{
	double a = VAL, b, c;
	float af = VALF, bf, cf;
	long double al = VALL, bl, cl;

	b = round(a);
	bf = roundf(af);
	bl = roundl(al);

	ATF_CHECK(fabs(b) < SMALL_NUM);
	ATF_CHECK(fabsf(bf) < SMALL_NUM);
	ATF_CHECK(fabsl(bl) < SMALL_NUM);

	c = round(-a);
	cf = roundf(-af);
	cl = roundl(-al);

	ATF_CHECK(fabs(c) < SMALL_NUM);
	ATF_CHECK(fabsf(cf) < SMALL_NUM);
	ATF_CHECK(fabsl(cl) < SMALL_NUM);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, round_dir);

	return atf_no_error();
}
