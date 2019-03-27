/* $NetBSD: t_memcpy.c,v 1.6 2017/01/11 18:05:54 christos Exp $ */

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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <md5.h>

#include <sys/types.h>

#define	ALIGNMENTS 16
#define	LENGTHS	    4
#define BLOCKTYPES 4

MD5_CTX mc[1];

typedef	unsigned char testBlock_t[ALIGNMENTS * LENGTHS];

testBlock_t bss1, bss2;

unsigned char *start[BLOCKTYPES] = {
		bss1, bss2
};

char result[100];
#ifdef __NetBSD__
const char goodResult[] = "7b405d24bc03195474c70ddae9e1f8fb";
#else
const char goodResult[] = "5ab4443f0e3e058d94087d9f2a11ef5e";
#endif

static void
runTest(unsigned char *b1, unsigned char *b2)
{
	int	i, j, k, m;
	size_t	n;

	for (i = 0; i < ALIGNMENTS; ++i) {
		for (j = 0; j < ALIGNMENTS; ++j) {
			k = sizeof(testBlock_t) - (i > j ? i : j);
			for (m = 0; m < k; ++m) {
				for (n = 0; n < sizeof(testBlock_t); ++n) {
					b1[n] = (unsigned char)random();
					b2[n] = (unsigned char)random();
				}
				memcpy(b1 + i, b2 + j, m);
				MD5Update(mc, b1, sizeof(testBlock_t));
				MD5Update(mc, b2, sizeof(testBlock_t));
			}
		}
	}
}

ATF_TC(memcpy_basic);
ATF_TC_HEAD(memcpy_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memcpy results");
}

ATF_TC_BODY(memcpy_basic, tc)
{
	int i, j;
	testBlock_t auto1, auto2;

	start[2] = auto1;
	start[3] = auto2;

#ifdef __NetBSD__
	srandom(0L);
#else
	/*
	 * random() shall produce by default a sequence of numbers that can be
	 * duplicated by calling srandom() with 1 as the seed.
	 */
	srandom(1);
#endif
	MD5Init(mc);
	for (i = 0; i < BLOCKTYPES; ++i)
		for (j = 0; j < BLOCKTYPES; ++j)
			if (i != j)
				runTest(start[i], start[j]);
	MD5End(mc, result);
	ATF_REQUIRE_EQ_MSG(strcmp(result, goodResult), 0, "%s != %s",
	    result, goodResult);
}

ATF_TC(memccpy_simple);
ATF_TC_HEAD(memccpy_simple, tc)
{
        atf_tc_set_md_var(tc, "descr", "Test memccpy(3) results");
}

ATF_TC_BODY(memccpy_simple, tc)
{
	char buf[100];
	char c = ' ';

	(void)memset(buf, c, sizeof(buf));

	ATF_CHECK(memccpy(buf, "foo bar", c, sizeof(buf)) != NULL);
	ATF_CHECK(buf[4] == c);

	ATF_CHECK(memccpy(buf, "foo bar", '\0', sizeof(buf) - 1) != NULL);
	ATF_CHECK(buf[8] == c);

	ATF_CHECK(memccpy(buf, "foo bar", 'x', 7) == NULL);
	ATF_CHECK(strncmp(buf, "foo bar", 7) == 0);

	ATF_CHECK(memccpy(buf, "xxxxxxx", 'r', 7) == NULL);
	ATF_CHECK(strncmp(buf, "xxxxxxx", 7) == 0);
}

ATF_TC(memcpy_return);
ATF_TC_HEAD(memcpy_return, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test memcpy(3) return value");
}

ATF_TC_BODY(memcpy_return, tc)
{
	char *b = (char *)0x1;
	char c[2];
	ATF_REQUIRE_EQ(memcpy(b, b, 0), b);
	ATF_REQUIRE_EQ(memcpy(c, "ab", sizeof(c)), c);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, memcpy_basic);
	ATF_TP_ADD_TC(tp, memcpy_return);
	ATF_TP_ADD_TC(tp, memccpy_simple);

	return atf_no_error();
}
