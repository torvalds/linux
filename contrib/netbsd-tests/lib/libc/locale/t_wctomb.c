/* $NetBSD: t_wctomb.c,v 1.3 2013/03/25 15:31:03 gson Exp $ */

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

/*-
 * Copyright (c)2004 Citrus Project,
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
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2011\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_wctomb.c,v 1.3 2013/03/25 15:31:03 gson Exp $");

#include <stdio.h>
#include <stdlib.h>
#include <locale.h>
#include <vis.h>
#include <wchar.h>
#include <string.h>
#include <limits.h>

#include <atf-c.h>

#define TC_WCTOMB	0
#define TC_WCRTOMB	1
#define TC_WCRTOMB_ST	2

static struct test {
	const char *locale;
	const char *data;
	size_t wclen;
	size_t mblen[16];
} tests[] = {
{
	"ja_JP.ISO2022-JP",
	"\x1b$B"	/* JIS X 0208-1983 */
	"\x46\x7c\x4b\x5c\x38\x6c" /* "nihongo" */
	"\x1b(B"	/* ISO 646 */
	"ABC"
	"\x1b(I"	/* JIS X 0201 katakana */
	"\xb1\xb2\xb3"	/* "aiu" */
	"\x1b(B",	/* ISO 646 */
	3 + 3 + 3,
	{ 3+2, 2, 2, 3+1, 1, 1, 3+1, 1, 1, 3+1 }
}, {
	"C", 
	"ABC",
	3,
	{ 1, 1, 1, 1 }
}, { NULL, NULL, 0, { } }
};

static void
h_wctomb(const struct test *t, char tc)
{
	wchar_t wcs[16 + 2];
	char buf[128];
	char cs[MB_LEN_MAX];
	const char *pcs;
	char *str;
	mbstate_t st;
	mbstate_t *stp = NULL;
	size_t sz, ret, i;

	ATF_REQUIRE_STREQ(setlocale(LC_ALL, "C"), "C");
#ifdef __NetBSD__
	ATF_REQUIRE(setlocale(LC_CTYPE, t->locale) != NULL);
#else
	if (setlocale(LC_CTYPE, t->locale) == NULL) {
		fprintf(stderr, "Locale %s not found.\n", t->locale);
		return;
	}
#endif

	(void)strvis(buf, t->data, VIS_WHITE | VIS_OCTAL);
	(void)printf("Checking sequence: \"%s\"\n", buf);

	ATF_REQUIRE((str = setlocale(LC_ALL, NULL)) != NULL);
	(void)printf("Using locale: %s\n", str);

	if (tc == TC_WCRTOMB_ST) {
		(void)memset(&st, 0, sizeof(st));
		stp = &st;
	}

	wcs[t->wclen] = L'X'; /* poison */
	pcs = t->data;
	sz = mbsrtowcs(wcs, &pcs, t->wclen + 2, NULL);
	ATF_REQUIRE_EQ_MSG(sz, t->wclen, "mbsrtowcs() returned: "
		"%zu, expected: %zu", sz, t->wclen);
	ATF_REQUIRE_EQ(wcs[t->wclen], 0);

	for (i = 0; i < t->wclen + 1; i++) {
		if (tc == TC_WCTOMB)
			ret = wctomb(cs, wcs[i]);
		else
			ret = wcrtomb(cs, wcs[i], stp);

		if (ret == t->mblen[i])
			continue;

		(void)printf("At position %zd:\n", i);
		(void)printf("  expected: %zd\n", t->mblen[i]);
		(void)printf("  got     : %zd\n", ret);
		atf_tc_fail("Test failed");
		/* NOTREACHED */
	}

	(void)printf("Ok.\n");
}

ATF_TC(wctomb);
ATF_TC_HEAD(wctomb, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks wctomb(3)");
}
ATF_TC_BODY(wctomb, tc)
{
	struct test *t;

	(void)printf("Checking wctomb()\n");

	for (t = &tests[0]; t->data != NULL; ++t)
		h_wctomb(t, TC_WCTOMB);
}

ATF_TC(wcrtomb_state);
ATF_TC_HEAD(wcrtomb_state, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks wcrtomb(3) (using state object)");
}
ATF_TC_BODY(wcrtomb_state, tc)
{
	struct test *t;

	(void)printf("Checking wcrtomb() (with state object)\n");

	for (t = &tests[0]; t->data != NULL; ++t)
		h_wctomb(t, TC_WCRTOMB_ST);
}

ATF_TC(wcrtomb);
ATF_TC_HEAD(wcrtomb, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks wcrtomb(3) (using internal state)");
}
ATF_TC_BODY(wcrtomb, tc)
{
	struct test *t;

	(void)printf("Checking wcrtomb() (using internal state)\n");

	for (t = &tests[0]; t->data != NULL; ++t)
		h_wctomb(t, TC_WCRTOMB);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, wctomb);
	ATF_TP_ADD_TC(tp, wcrtomb);
	ATF_TP_ADD_TC(tp, wcrtomb_state);

	return atf_no_error();
}
