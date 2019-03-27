/*	$NetBSD: t_convfp.c,v 1.7 2011/06/14 11:58:22 njoly Exp $	*/

/*-
 * Copyright (c) 2003 The NetBSD Foundation, Inc.
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

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

/*
 * This value is representable as an unsigned int, but not as an int.
 * According to ISO C it must survive the convsion back from a double
 * to an unsigned int (everything > -1 and < UINT_MAX+1 has to)
 */ 
#define	UINT_TESTVALUE	(INT_MAX+42U)

/* The same for unsigned long */
#define ULONG_TESTVALUE	(LONG_MAX+42UL)


ATF_TC(conv_uint);
ATF_TC_HEAD(conv_uint, tc)
{

	atf_tc_set_md_var(tc, "descr", "test conversions to unsigned int");
}

ATF_TC_BODY(conv_uint, tc)
{
	unsigned int ui;
	double d;

	/* unsigned int test */
	d = UINT_TESTVALUE;
	ui = (unsigned int)d;

	if (ui != UINT_TESTVALUE)
		atf_tc_fail("FAILED: unsigned int %u (0x%x) != %u (0x%x)",
		    ui, ui, UINT_TESTVALUE, UINT_TESTVALUE);
}

ATF_TC(conv_ulong);

ATF_TC_HEAD(conv_ulong, tc)
{

	atf_tc_set_md_var(tc, "descr", "test conversions to unsigned long");
}

ATF_TC_BODY(conv_ulong, tc)
{
	unsigned long ul;
	long double dt;
	double d;

	/* unsigned long vs. {long} double test */
	if (sizeof(d) > sizeof(ul)) {
		d = ULONG_TESTVALUE;
		ul = (unsigned long)d;
		printf("testing double vs. long\n");
	} else if (sizeof(dt) > sizeof(ul)) {
		dt = ULONG_TESTVALUE;
		ul = (unsigned long)dt;
		printf("testing long double vs. long\n");
	} else {
		printf("sizeof(long) = %zu, sizeof(double) = %zu, "
		    "sizeof(long double) = %zu\n", 
		    sizeof(ul), sizeof(d), sizeof(dt));
		atf_tc_skip("no suitable {long} double type found");
	}

	if (ul != ULONG_TESTVALUE)
		atf_tc_fail("unsigned long %lu (0x%lx) != %lu (0x%lx)",
		    ul, ul, ULONG_TESTVALUE, ULONG_TESTVALUE);
}

ATF_TC(cast_ulong);

ATF_TC_HEAD(cast_ulong, tc)
{

	atf_tc_set_md_var(tc, "descr", "test double to unsigned long cast");
}

ATF_TC_BODY(cast_ulong, tc)
{
	double nv;
	unsigned long uv;

	nv = 5.6;
	uv = (unsigned long)nv;

	ATF_CHECK_EQ_MSG(uv, 5,
	    "%.3f casted to unsigned long is %lu", nv, uv);
}

ATF_TC(cast_ulong2);

ATF_TC_HEAD(cast_ulong2, tc)
{

	atf_tc_set_md_var(tc, "descr",
	    "test double/long double casts to unsigned long");
}

ATF_TC_BODY(cast_ulong2, tc)
{
	double dv = 1.9;
	long double ldv = dv;
	unsigned long l1 = dv;
	unsigned long l2 = ldv;

	ATF_CHECK_EQ_MSG(l1, 1,
	    "double 1.9 casted to unsigned long should be 1, but is %lu", l1);

	ATF_CHECK_EQ_MSG(l2, 1,
	    "long double 1.9 casted to unsigned long should be 1, but is %lu",
	    l2);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, conv_uint);
	ATF_TP_ADD_TC(tp, conv_ulong);
	ATF_TP_ADD_TC(tp, cast_ulong);
	ATF_TP_ADD_TC(tp, cast_ulong2);

	return atf_no_error();
}
