/* $NetBSD: t_mbsnrtowcs.c,v 1.2 2014/05/06 00:41:26 yamt Exp $ */

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
__RCSID("$NetBSD: t_mbsnrtowcs.c,v 1.2 2014/05/06 00:41:26 yamt Exp $");

#include <locale.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>

static const struct test {
	const char *locale;
	const char *data;
	size_t limit;
	const wchar_t output1[64];
	size_t output1_len;
	const wchar_t output2[64];
	size_t output2_len;
} tests[] = {
	{ "C", "ABCD0123", 4, { 0x41, 0x42, 0x43, 0x44 }, 4,
			    { 0x30, 0x31, 0x32, 0x33, 0x0 }, 5 },
	{ "en_US.UTF-8", "ABCD0123", 4, { 0x41, 0x42, 0x43, 0x44 }, 4,
			    { 0x30, 0x31, 0x32, 0x33, 0x0 }, 5 },
	{ "en_US.UTF-8", "ABC\303\2440123", 4, { 0x41, 0x42, 0x43, }, 3,
			    { 0xe4, 0x30, 0x31, 0x32, 0x33, 0x0 }, 6 },
};

ATF_TC(mbsnrtowcs);
ATF_TC_HEAD(mbsnrtowcs, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks mbsnrtowc(3) with different locales");
}
ATF_TC_BODY(mbsnrtowcs, tc)
{
	size_t i;
	const struct test *t;
	mbstate_t state;
	wchar_t buf[64];
	const char *src;
	size_t len;

	for (i = 0; i < __arraycount(tests); ++i) {
		t = &tests[i];
		ATF_REQUIRE_STREQ(setlocale(LC_ALL, "C"), "C");
		ATF_REQUIRE(setlocale(LC_CTYPE, t->locale) != NULL);
		memset(&state, 0, sizeof(state));
		src = t->data;
		len = mbsnrtowcs(buf, &src, t->limit,
		    __arraycount(buf), &state);
		ATF_REQUIRE_EQ(src, t->data + t->limit);
		ATF_REQUIRE_EQ(len, t->output1_len);
		ATF_REQUIRE(wmemcmp(t->output1, buf, len) == 0);
		len = mbsnrtowcs(buf, &src, strlen(src) + 1,
		    __arraycount(buf), &state);
		ATF_REQUIRE_EQ(len, strlen(t->data) - t->limit);
		ATF_REQUIRE(wmemcmp(t->output2, buf, len + 1) == 0);
		ATF_REQUIRE_EQ(src, NULL);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mbsnrtowcs);

	return atf_no_error();
}
