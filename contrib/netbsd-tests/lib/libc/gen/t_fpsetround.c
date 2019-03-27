/* $NetBSD: t_fpsetround.c,v 1.6 2011/10/01 17:46:10 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
__RCSID("$NetBSD: t_fpsetround.c,v 1.6 2011/10/01 17:46:10 christos Exp $");

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <atf-c.h>

ATF_TC(fpsetround_basic);
ATF_TC_HEAD(fpsetround_basic, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "Minimal testing of fpgetround(3) and fpsetround(3)");
}

#ifdef _FLOAT_IEEE754
#include <ieeefp.h>

static const struct {
	const char *n;
	int rm;
	int rf;
} rnd[] = {
	{ "RN", FP_RN, 1 },
	{ "RP", FP_RP, 2 },
	{ "RM", FP_RM, 3 },
	{ "RZ", FP_RZ, 0 },

};

static const struct {
	const char *n;
	int v[4];
} tst[] = {	/*  RN  RP  RM  RZ */
	{  "1.1", {  1,  1,  2,  1 } },
	{  "1.5", {  1,  2,  2,  1 } },
	{  "1.9", {  1,  2,  2,  1 } },
	{  "2.1", {  2,  2,  3,  2 } },
	{  "2.5", {  2,  2,  3,  2 } },
	{  "2.9", {  2,  3,  3,  2 } },
	{ "-1.1", { -1, -1, -1, -2 } },
	{ "-1.5", { -1, -2, -1, -2 } },
	{ "-1.9", { -1, -2, -1, -2 } },
	{ "-2.1", { -2, -2, -2, -3 } },
	{ "-2.5", { -2, -2, -2, -3 } },
	{ "-2.9", { -2, -3, -2, -3 } },
};

static const char *
getname(int r)
{
	for (size_t i = 0; i < __arraycount(rnd); i++)
		if (rnd[i].rm == r)
			return rnd[i].n;
	return "*unknown*";
}

static void
test(int r)
{
	int did = 0;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		double d = strtod(tst[i].n, NULL);
		int g = (int)rint(d);
		int e = tst[i].v[r];
		ATF_CHECK_EQ(g, e);
		if (g != e) {
			if (!did) {
				fprintf(stderr, "Mode Value Result Expected\n");
				did = 1;
			}
			fprintf(stderr, "%4.4s %-5.5s %6d %8d\n", rnd[r].n,
			    tst[i].n, (int)rint(d), tst[i].v[r]);
		}
	}
}
#endif


ATF_TC_BODY(fpsetround_basic, tc)
{

#ifndef _FLOAT_IEEE754
	atf_tc_skip("Test not applicable on this architecture.");
#else
	int r;

	ATF_CHECK_EQ(r = fpgetround(), FP_RN);
	if (FP_RN != r)
		fprintf(stderr, "default expected=%s got=%s\n", getname(FP_RN),
		    getname(r));
	ATF_CHECK_EQ(FLT_ROUNDS, 1);

	for (size_t i = 0; i < __arraycount(rnd); i++) {
		const size_t j = (i + 1) & 3;
		const int o = rnd[i].rm;
		const int n = rnd[j].rm;

		ATF_CHECK_EQ(r = fpsetround(n), o);
		if (o != r)
			fprintf(stderr, "set %s expected=%s got=%s\n",
			    getname(n), getname(o), getname(r));
		ATF_CHECK_EQ(r = fpgetround(), n);
		if (n != r)
			fprintf(stderr, "get expected=%s got=%s\n", getname(n),
			    getname(r));
		ATF_CHECK_EQ(r = FLT_ROUNDS, rnd[j].rf);
		if (r != rnd[j].rf)
			fprintf(stderr, "rounds expected=%x got=%x\n",
			    rnd[j].rf, r);
		test(r);
	}
#endif /* _FLOAT_IEEE754 */
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fpsetround_basic);

	return atf_no_error();
}
