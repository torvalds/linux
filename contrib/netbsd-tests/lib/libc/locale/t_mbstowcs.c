/* $NetBSD: t_mbstowcs.c,v 1.1 2011/07/15 07:35:21 jruoho Exp $ */

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
__RCSID("$NetBSD: t_mbstowcs.c,v 1.1 2011/07/15 07:35:21 jruoho Exp $");

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>
#include <wchar.h>

#include <atf-c.h>

#define REQUIRE_ERRNO(x, v) \
	ATF_REQUIRE_MSG((x) != (v), "%s: %s", #x, strerror(errno))

#define SIZE 256

static struct test {
	const char *locale;
	const char *data;
	wchar_t wchars[64];
	int widths[64];
	int width;
} tests[] = {
{
	"en_US.UTF-8",
	"[\001\177][\302\200\337\277][\340\240\200\357\277\277][\360\220\200"
	"\200\364\217\277\277]",
	{
		0x5B, 0x01, 0x7F, 0x5D, 0x5B, 0x80, 0x07FF, 0x5D, 0x5B, 0x0800,
		0xFFFF, 0x5D, 0x5B, 0x10000, 0x10FFFF, 0x5D, 0x0A
	},
#ifdef __FreeBSD__
	{	 1, -1, -1,  1,  1, -1,  1,  1,  1,  1, -1,  1,  1,  1, -1,
#else
	{	 1, -1, -1,  1,  1, -1, -1,  1,  1, -1, -1,  1,  1, -1, -1,
#endif
		 1,  1, -1, -1,  1,  1, -1, -1,  1, -1
	}, 
	-1
}, {
	"ja_JP.ISO2022-JP",
	"\033$B#J#I#S$G$9!#\033(Baaaa\033$B$\"$$$&$($*\033(B",
	{
		0x4200234A, 0x42002349, 0x42002353, 0x42002447, 0x42002439,
		0x42002123, 0x61, 0x61, 0x61, 0x61, 0x42002422, 0x42002424,
		0x42002426, 0x42002428, 0x4200242A, 0x0A
	},
	{ 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 2, -1 },
	26
}, {
	"ja_JP.SJIS",
	"\202r\202i\202h\202r\202\305\202\267\201Baaaa\202\240\202\242"
	"\202\244\202\246\202\250",
	{
		0x8272, 0x8269, 0x8268, 0x8272, 0x82C5, 0x82B7, 0x8142, 0x61,
		0x61, 0x61, 0x61, 0x82A0, 0x82A2, 0x82A4, 0x82A6, 0x82A8, 0x0A
	},
	{ 2, 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 2, -1 },
	28
}, {
	"ja_JP.eucJP",
	"\243\305\243\325\243\303\244\307\244\271\241\243aaaa\244\242\244"
	"\244\244\246\244\250\244\252",
	{
		0xA3C5, 0xA3D5, 0xA3C3, 0xA4C7, 0xA4B9, 0xA1A3, 0x61, 0x61, 0x61,
		0x61, 0xA4A2, 0xA4A4, 0xA4A6, 0xA4A8, 0xA4AA, 0x0A
	},
	{ 2, 2, 2, 2, 2, 2, 1, 1, 1, 1, 2, 2, 2, 2, 2, -1 },
	26
}, {
	NULL,
	NULL,
	{},
	{},
	0
}
};

ATF_TC(mbstowcs_basic);
ATF_TC_HEAD(mbstowcs_basic, tc)
{
	atf_tc_set_md_var(tc, "descr",
		"Checks wide character functions with different locales");
}
ATF_TC_BODY(mbstowcs_basic, tc)
{
	struct test *t;

	for (t = &tests[0]; t->data != NULL; ++t) {
		wchar_t	wbuf[SIZE];
		char buf[SIZE];
		char visbuf[SIZE];
		char *str;
		int i;

		ATF_REQUIRE_STREQ(setlocale(LC_ALL, "C"), "C");
#ifdef __NetBSD__
		ATF_REQUIRE(setlocale(LC_CTYPE, t->locale) != NULL);
#else
		if (setlocale(LC_CTYPE, t->locale) == NULL) {
			fprintf(stderr, "Locale %s not found.\n", t->locale);
			continue;
		}
#endif

		(void)strvis(visbuf, t->data, VIS_WHITE | VIS_OCTAL);
		(void)printf("Checking string: \"%s\"\n", visbuf);

		ATF_REQUIRE((str = setlocale(LC_ALL, NULL)) != NULL);
		(void)printf("Using locale: %s\n", str);

		REQUIRE_ERRNO((ssize_t)mbstowcs(wbuf, t->data, SIZE-1), -1);
		REQUIRE_ERRNO((ssize_t)wcstombs(buf, wbuf, SIZE-1), -1);

		if (strcmp(buf, t->data) != 0) {
			(void)strvis(visbuf, buf, VIS_WHITE | VIS_OCTAL);
			(void)printf("Conversion to wcs and back failed: "
				"\"%s\"\n", visbuf);
			atf_tc_fail("Test failed");
		}

		/* The output here is implementation-dependent. */

		for (i = 0; wbuf[i] != 0; ++i) {
			if (wbuf[i] == t->wchars[i] &&
			    wcwidth(wbuf[i]) == t->widths[i])
				continue;

			(void)printf("At position %d:\n", i);
			(void)printf("  expected: 0x%04X (%d)\n",
				t->wchars[i], t->widths[i]);
			(void)printf("  got     : 0x%04X (%d)\n", wbuf[i],
				wcwidth(wbuf[i]));
			atf_tc_fail("Test failed");
		}

		if (wcswidth(wbuf, SIZE-1) != t->width) {
			(void)printf("Incorrect wcswidth:\n");
			(void)printf("  expected: %d\n", t->width);
			(void)printf("  got     : %d\n", wcswidth(wbuf, SIZE-1));
			atf_tc_fail("Test failed");
		}

		(void)printf("Ok.\n");
	}
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, mbstowcs_basic);

	return atf_no_error();
}
