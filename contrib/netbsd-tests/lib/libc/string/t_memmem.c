/*	$NetBSD: t_memmem.c,v 1.3 2017/01/11 18:07:37 christos Exp $ */

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Perry E. Metzger of Metzger, Dowdeswell & Co. LLC.
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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

char p0[] = "";
int lp0 = 0;
char p1[] = "0123";
int lp1 = 4;
char p2[] = "456";
int lp2 = 3;
char p3[] = "789";
int lp3 = 3;
char p4[] = "abc";
int lp4 = 3;
char p5[] = "0";
int lp5 = 1;
char p6[] = "9";
int lp6 = 1;
char p7[] = "654";
int lp7 = 3;
char p8[] = "89abc";
int lp8 = 5;

char b0[] = "";
int lb0 = 0;
char b1[] = "0";
int lb1 = 1;
char b2[] = "0123456789";
int lb2 = 10;

#define expect(b)							\
	if (!(b)) {							\
		fprintf(stderr, "failed on line %d\n", __LINE__);	\
		atf_tc_fail("Check stderr for test id/line");		\
	}

ATF_TC(memmem_basic);
ATF_TC_HEAD(memmem_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test memmem results");
}

ATF_TC_BODY(memmem_basic, tc)
{

#if defined(__darwin__)
	expect(memmem(b2, lb2, p0, lp0) == NULL);
	expect(memmem(b0, lb0, p0, lp0) == NULL);
#else
	expect(memmem(b2, lb2, p0, lp0) == b2);
	expect(memmem(b0, lb0, p0, lp0) == b0);
#endif
	expect(memmem(b0, lb0, p1, lp1) == NULL);
	expect(memmem(b1, lb1, p1, lp1) == NULL);

	expect(memmem(b2, lb2, p1, lp1) == b2);
	expect(memmem(b2, lb2, p2, lp2) == (b2 + 4));
	expect(memmem(b2, lb2, p3, lp3) == (b2 + 7));

	expect(memmem(b2, lb2, p5, lp5) == b2);
	expect(memmem(b2, lb2, p6, lp6) == (b2 + 9));

	expect(memmem(b2, lb2, p4, lp4) == NULL);
	expect(memmem(b2, lb2, p7, lp7) == NULL);
	expect(memmem(b2, lb2, p8, lp8) == NULL);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, memmem_basic);

	return atf_no_error();
}
