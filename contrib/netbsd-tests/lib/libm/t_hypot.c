/* $NetBSD: t_hypot.c,v 1.1 2016/01/24 20:26:47 gson Exp $ */

/*-
 * Copyright (c) 2016 The NetBSD Foundation, Inc.
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
#include <math.h>

ATF_TC(hypot_integer);
ATF_TC_HEAD(hypot_integer, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test hypot with integer args");
}

ATF_TC_BODY(hypot_integer, tc)
{
	/* volatile so hypotf() won't be evaluated at compile time */
	volatile double a = 5;
	volatile double b = 12;
	ATF_CHECK(hypot(a, b) == 13.0);
}

ATF_TC(hypotf_integer);
ATF_TC_HEAD(hypotf_integer, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test hypotf with integer args");
}

ATF_TC_BODY(hypotf_integer, tc)
{
	volatile float a = 5;
	volatile float b = 12;
	ATF_CHECK(hypotf(a, b) == 13.0f);
}

ATF_TC(pr50698);
ATF_TC_HEAD(pr50698, tc)
{
	atf_tc_set_md_var(tc, "descr", "Check for the bug of PR 50698");
}

ATF_TC_BODY(pr50698, tc)
{
	volatile float a = 1e-18f;
	float val = hypotf(a, a);
	ATF_CHECK(!isinf(val));
	ATF_CHECK(!isnan(val));
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, hypot_integer);
	ATF_TP_ADD_TC(tp, hypotf_integer);
	ATF_TP_ADD_TC(tp, pr50698);

	return atf_no_error();
}
