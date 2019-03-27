/* $NetBSD: t_scanf.c,v 1.3 2012/03/18 07:00:51 jruoho Exp $ */

/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
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
#include <stdio.h>
#include <string.h>

#define NUM     -0x1234
#define STRNUM  ___STRING(NUM)

ATF_TC(sscanf_neghex);
ATF_TC_HEAD(sscanf_neghex, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "PR lib/21691: %%i and %%x fail with negative hex numbers");
}

ATF_TC_BODY(sscanf_neghex, tc)
{
        int i;

        sscanf(STRNUM, "%i", &i);
	ATF_REQUIRE(i == NUM);

        sscanf(STRNUM, "%x", &i);
	ATF_REQUIRE(i == NUM);
}

ATF_TC(sscanf_whitespace);
ATF_TC_HEAD(sscanf_whitespace, tc)
{

	atf_tc_set_md_var(tc, "descr", "verify sscanf skips all whitespace");
}

ATF_TC_BODY(sscanf_whitespace, tc)
{
	const char str[] = "\f\n\r\t\v%z";
	char c;

#ifndef __NetBSD__
	atf_tc_expect_fail("fails on FreeBSD and some variants of Linux");
#endif

        /* set of "white space" symbols from isspace(3) */
        c = 0;
        (void)sscanf(str, "%%%c", &c);
	ATF_REQUIRE(c == 'z');
}


ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, sscanf_neghex);
	ATF_TP_ADD_TC(tp, sscanf_whitespace);

	return atf_no_error();
}
