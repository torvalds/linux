/* $Id: t_bm.c,v 1.1 2014/06/23 10:53:20 shm Exp $ */
/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Mateusz Kocielski.
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
__RCSID("$Id: t_bm.c,v 1.1 2014/06/23 10:53:20 shm Exp $");

#include <atf-c.h>
#include <stdio.h>
#include <sys/types.h>
#include <bm.h>
#include <string.h>
#include <stdlib.h>

ATF_TC(bm);
ATF_TC_HEAD(bm, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test bm(3)");
}

typedef struct {
	const char *pattern;
	const char *text;
	const char *freq;
	ssize_t match;
} t_testcase;

const t_testcase testcases[] = {
	{"test", "test", NULL, 0},
	{"test", "ttest", NULL, 1},
	{"test", "tes", NULL, -1},
	{"test", "testtesttest", NULL, 0},
	{"test", "testtesttesttesttesttest", NULL, 0},
	{"test", "------------------------", NULL, -1},
	{"a", "a", NULL, 0},
	{"a", "ba", NULL, 1},
	{"a", "bba", NULL, 2},
	{"bla", "bl", NULL, -1},
	{"a", "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb", NULL, -1},
	{"test", "qfwiofjqeiwofjioqewfjeiqwjfiqewjfioqewfjioewqjfioewqjfioewqjoi",
	  NULL, -1},
	{"needle", "haystack", NULL, -1},
	{"netbsd", "freebsd netbsd openbsd", NULL, 8},
};

ATF_TC_BODY(bm, tc)
{
	size_t ts;
	u_char *off;
	char *text;
	bm_pat *pattern;
	
	for (ts = 0; ts < sizeof(testcases)/sizeof(t_testcase); ts++) {
		ATF_CHECK(pattern = bm_comp((const u_char *)testcases[ts].pattern,
		  strlen(testcases[ts].pattern), (const u_char *)testcases[ts].freq));

		ATF_REQUIRE(text = strdup(testcases[ts].text));
		off = bm_exec(pattern, (u_char *)text, strlen(text));

		if (testcases[ts].match == -1)
			ATF_CHECK_EQ(off, NULL);
		else
			ATF_CHECK_EQ(testcases[ts].match,
			  (off-(u_char *)text));

		bm_free(pattern);
		free(text);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bm);
	return atf_no_error();
}
