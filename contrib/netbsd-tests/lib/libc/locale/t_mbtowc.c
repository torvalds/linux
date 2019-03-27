/* $NetBSD: t_mbtowc.c,v 1.1 2011/04/09 17:45:25 pgoyette Exp $ */

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
 * Copyright (c)2007 Citrus Project,
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
 *
 */

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2011\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_mbtowc.c,v 1.1 2011/04/09 17:45:25 pgoyette Exp $");

#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vis.h>
#include <wchar.h>

#include <atf-c.h>

static void
h_mbtowc(const char *locale, const char *illegal, const char *legal)
{
	char buf[64];
	size_t stateful, ret;
	char *str;

	ATF_REQUIRE_STREQ(setlocale(LC_ALL, "C"), "C");
#ifdef __NetBSD__
	ATF_REQUIRE(setlocale(LC_CTYPE, locale) != NULL);
#else
	if (setlocale(LC_CTYPE, locale) == NULL) {
		fprintf(stderr, "Locale %s not found.\n", locale);
		return;
	}
#endif

	ATF_REQUIRE((str = setlocale(LC_ALL, NULL)) != NULL);
	(void)printf("Using locale: %s\n", str);

	stateful = wctomb(NULL, L'\0');
	(void)printf("Locale is state-%sdependent\n",
		stateful ? "in" : "");

	/* initialize internal state */
	ret = mbtowc(NULL, NULL, 0);
	ATF_REQUIRE(stateful ? ret : !ret);

	(void)strvis(buf, illegal, VIS_WHITE | VIS_OCTAL);
	(void)printf("Checking illegal sequence: \"%s\"\n", buf);

	ret = mbtowc(NULL, illegal, strlen(illegal));
	(void)printf("mbtowc() returned: %zd\n", ret);
	ATF_REQUIRE_EQ(ret, (size_t)-1);
	(void)printf("errno: %s\n", strerror(errno));
	ATF_REQUIRE_EQ(errno, EILSEQ);

	/* if this is stateless encoding, this re-initialization is not required. */
	if (stateful) {
		/* re-initialize internal state */
		ret = mbtowc(NULL, NULL, 0);
		ATF_REQUIRE(stateful ? ret : !ret);
	}

	/* valid multibyte sequence case */
	(void)strvis(buf, legal, VIS_WHITE | VIS_OCTAL);
	(void)printf("Checking legal sequence: \"%s\"\n", buf);

	errno = 0;
	ret = mbtowc(NULL, legal, strlen(legal));
	(void)printf("mbtowc() returned: %zd\n", ret);
	ATF_REQUIRE(ret != (size_t)-1);
	(void)printf("errno: %s\n", strerror(errno));
	ATF_REQUIRE_EQ(errno, 0);

	(void)printf("Ok.\n");
}

ATF_TC(mbtowc);
ATF_TC_HEAD(mbtowc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks mbtowc(3)");
}
ATF_TC_BODY(mbtowc, tc)
{
	h_mbtowc("en_US.UTF-8", "\240", "\302\240");
	h_mbtowc("ja_JP.ISO2022-JP", "\033$B", "\033$B$\"\033(B");
	h_mbtowc("ja_JP.SJIS", "\202", "\202\240");
	h_mbtowc("ja_JP.eucJP", "\244", "\244\242");
	h_mbtowc("zh_CN.GB18030", "\241", "\241\241");
	h_mbtowc("zh_TW.Big5", "\241", "\241@");
	h_mbtowc("zh_TW.eucTW", "\241", "\241\241");
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, mbtowc);

	return atf_no_error();
}
