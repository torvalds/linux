/* $NetBSD: t_mbrtowc.c,v 1.1 2011/07/15 07:35:21 jruoho Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by YAMAMOTO Takashi
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
 * Copyright (c)2003 Citrus Project,
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
__RCSID("$NetBSD: t_mbrtowc.c,v 1.1 2011/07/15 07:35:21 jruoho Exp $");

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>
#include <wchar.h>

#include <atf-c.h>

#define SIZE 256

static struct test {
	const char *locale;
	const char *data;
	const wchar_t wchars[64];
	const wchar_t widths[64];
	size_t length;
} tests[] = {
{
	"C",
	"ABCD01234_\\",
	{ 0x41, 0x42, 0x43, 0x44, 0x30, 0x31, 0x32, 0x33, 0x34, 0x5F, 0x5C },
	{ 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 },
	11
}, {
	"en_US.UTF-8",
	"[\001\177][\302\200\337\277][\340\240\200\357\277\277][\360\220\200"
	"\200\364\217\277\277]",
	{ 0x5b, 0x01, 0x7f, 0x5d, 0x5b, 0x80, 0x7ff, 0x5d, 0x5b, 0x800, 0xffff,
	  0x5d, 0x5b, 0x10000, 0x10ffff, 0x5d },
	{ 1, 1, 1, 1, 1, 2, 2, 1, 1, 3, 3, 1, 1, 4, 4, 1 },
	16
}, {
	"ja_JP.ISO2022-JP2",
	"\033$BF|K\1348l\033(BA\033$B$\"\033(BB\033$B$$\033(B",
	{ 0x4200467c, 0x42004b5c, 0x4200386c, 0x41, 0x42002422, 0x42, 0x42002424 },
	{ 5, 2, 2, 4, 5, 4, 5 },
	7
}, {
	"ja_JP.SJIS",
	"\223\372\226{\214\352A\202\240B\202\242",
	{ 0x93fa, 0x967b, 0x8cea, 0x41, 0x82a0, 0x42, 0x82a2 },
	{ 2, 2, 2, 1, 2, 1, 2 },
	7
}, {
	"ja_JP.eucJP",
	"\306\374\313\334\270\354A\244\242B\244\244",
	{ 0xc6fc, 0xcbdc, 0xb8ec, 0x41, 0xa4a2, 0x42, 0xa4a4 },
	{ 2, 2, 2, 1, 2, 1, 2 },
	7
}, {
	NULL,
	NULL,
	{ },
	{ },
	0
}
};

static void
h_ctype2(const struct test *t, bool use_mbstate)
{
	mbstate_t *stp;
	mbstate_t st;
	char buf[SIZE];
	char *str;
	size_t n;

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
	(void)printf("Checking string: \"%s\"\n", buf);

	ATF_REQUIRE((str = setlocale(LC_ALL, NULL)) != NULL);
	(void)printf("Using locale: %s\n", str);

	(void)printf("Using mbstate: %s\n", use_mbstate ? "yes" : "no");

	(void)memset(&st, 0, sizeof(st));
//	mbrtowc(0, 0, 0, &st); /* XXX for ISO2022-JP */
	stp = use_mbstate ? &st : 0;

	for (n = 9; n > 0; n--) {
		const char *src = t->data;
		wchar_t dst;
		size_t nchar = 0;
		int width = 0;

		ATF_REQUIRE(mbsinit(stp) != 0);

		for (;;) {
			size_t rv = mbrtowc(&dst, src, n, stp);

			if (rv == 0)
				break;

			if (rv == (size_t)-2) {
				src += n;
				width += n;

				continue;
			}
			if (rv == (size_t)-1) {
				ATF_REQUIRE_EQ(errno, EILSEQ);
				atf_tc_fail("Invalid sequence");
				/* NOTREACHED */
			}

			width += rv;
			src += rv;

			if (dst != t->wchars[nchar] ||
			    width != t->widths[nchar]) {
				(void)printf("At position %zd:\n", nchar);
				(void)printf("  expected: 0x%04X (%u)\n",
					t->wchars[nchar], t->widths[nchar]);
				(void)printf("  got     : 0x%04X (%u)\n",
					dst, width);
				atf_tc_fail("Test failed");
			}

			nchar++;
			width = 0;
		}

		ATF_REQUIRE_EQ_MSG(dst, 0, "Incorrect terminating character: "
			"0x%04X (expected: 0x00)", dst);

		ATF_REQUIRE_EQ_MSG(nchar, t->length, "Incorrect length: "
			"%zd (expected: %zd)", nchar, t->length);
	}

	{
		wchar_t wbuf[SIZE];
		size_t rv;
		char const *src = t->data;
		int i;

		(void)memset(wbuf, 0xFF, sizeof(wbuf));

		rv = mbsrtowcs(wbuf, &src, SIZE, stp);

		ATF_REQUIRE_EQ_MSG(rv, t->length, "Incorrect length: %zd "
			"(expected: %zd)", rv, t->length);
		ATF_REQUIRE_EQ(src, NULL);

		for (i = 0; wbuf[i] != 0; ++i) {
			if (wbuf[i] == t->wchars[i])
				continue;

			(void)printf("At position %d:\n", i);
			(void)printf("  expected: 0x%04X\n", t->wchars[i]);
			(void)printf("  got     : 0x%04X\n", wbuf[i]);
			atf_tc_fail("Test failed");
		}

		ATF_REQUIRE_EQ_MSG((size_t)i, t->length, "Incorrect length: "
			"%d (expected: %zd)", i, t->length);
	}

	(void)printf("Ok.\n");
}

ATF_TC(mbrtowc_internal);
ATF_TC_HEAD(mbrtowc_internal, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks mbrtowc(3) and mbsrtowcs(3) (using internal "
		"state) with different locales");
}
ATF_TC_BODY(mbrtowc_internal, tc)
{
	struct test *t;

	for (t = &tests[0]; t->data != NULL; ++t)
		h_ctype2(t, false);
}

ATF_TC(mbrtowc_object);
ATF_TC_HEAD(mbrtowc_object, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks mbrtowc(3) and mbsrtowcs(3) (using state "
		"object) with different locales");
}
ATF_TC_BODY(mbrtowc_object, tc)
{
	struct test *t;

	for (t = &tests[0]; t->data != NULL; ++t)
		h_ctype2(t, true);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, mbrtowc_internal);
	ATF_TP_ADD_TC(tp, mbrtowc_object);

	return atf_no_error();
}
