/* $NetBSD: t_precision.c,v 1.3 2016/08/27 10:07:05 christos Exp $ */

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Joerg Sonnenberger.
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
__RCSID("$NetBSD: t_precision.c,v 1.3 2016/08/27 10:07:05 christos Exp $");

#include <atf-c.h>

#include <float.h>
#include <stdlib.h>

ATF_TC(t_precision);

ATF_TC_HEAD(t_precision, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Basic precision test for double and long double");
}

volatile double x = 1;
#if __HAVE_LONG_DOUBLE
volatile long double y = 1;
#endif

ATF_TC_BODY(t_precision, tc)
{
#ifdef	__FreeBSD__
#ifdef	__i386__
	atf_tc_expect_fail("the __HAVE_LONG_DOUBLE checks fail on i386");
#endif
#endif
	x += DBL_EPSILON;
	ATF_CHECK(x != 1.0);
	x -= 1;
	ATF_CHECK(x == DBL_EPSILON);

	x = 2;
	x += DBL_EPSILON;
	ATF_CHECK(x == 2.0);

#if __HAVE_LONG_DOUBLE
	y += LDBL_EPSILON;
	ATF_CHECK(y != 1.0L);
	y -= 1;
	ATF_CHECK(y == LDBL_EPSILON);
	y = 2;
	y += LDBL_EPSILON;
	ATF_CHECK(y == 2.0L);
#endif
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_precision);

	return atf_no_error();
}
