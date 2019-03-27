/* $NetBSD: t_io.c,v 1.4 2014/01/21 00:32:16 yamt Exp $ */

/*-
 * Copyright (c) 2013 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas.
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
__COPYRIGHT("@(#) Copyright (c) 2011\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_io.c,v 1.4 2014/01/21 00:32:16 yamt Exp $");

#include <sys/param.h>
#include <errno.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include <atf-c.h>


ATF_TC(bad_big5_wprintf);
ATF_TC_HEAD(bad_big5_wprintf, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test bad big5 wchar wprintf");
}

ATF_TC_BODY(bad_big5_wprintf, tc)
{
	/* XXX implementation detail knowledge (wchar_t encoding) */
	wchar_t ibuf[] = { 0xcf10, 0 };
	setlocale(LC_CTYPE, "zh_TW.Big5");
	ATF_REQUIRE_ERRNO(EILSEQ, wprintf(L"%ls\n", ibuf) < 0);
	ATF_REQUIRE(ferror(stdout));
}

ATF_TC(bad_big5_swprintf);
ATF_TC_HEAD(bad_big5_swprintf, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test bad big5 wchar swprintf");
}

ATF_TC_BODY(bad_big5_swprintf, tc)
{
	/* XXX implementation detail knowledge (wchar_t encoding) */
	wchar_t ibuf[] = { 0xcf10, 0 };
	wchar_t obuf[20];
	setlocale(LC_CTYPE, "zh_TW.Big5");
	ATF_REQUIRE_ERRNO(EILSEQ,
			  swprintf(obuf, sizeof(obuf), L"%ls\n", ibuf) < 0);
}

ATF_TC(good_big5_wprintf);
ATF_TC_HEAD(good_big5_wprintf, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test good big5 wchar wprintf");
}

ATF_TC_BODY(good_big5_wprintf, tc)
{
	/* XXX implementation detail knowledge (wchar_t encoding) */
	wchar_t ibuf[] = { 0xcf40, 0 };
	setlocale(LC_CTYPE, "zh_TW.Big5");
	ATF_REQUIRE_EQ(wprintf(L"%ls\n", ibuf), 2);
}

ATF_TC(good_big5_swprintf);
ATF_TC_HEAD(good_big5_swprintf, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test good big5 wchar swprintf");
}

ATF_TC_BODY(good_big5_swprintf, tc)
{
	/* XXX implementation detail knowledge (wchar_t encoding) */
	wchar_t ibuf[] = { 0xcf40, 0 };
	wchar_t obuf[20];
	setlocale(LC_CTYPE, "zh_TW.Big5");
	ATF_REQUIRE_EQ(swprintf(obuf, sizeof(obuf), L"%ls\n", ibuf), 2);
}

struct ibuf {
	off_t off;
	size_t buflen;
	const char *buf;
};

static int
readfn(void *vp, char *buf, int len)
{
	struct ibuf *ib = vp;
	size_t todo = MIN((size_t)len, ib->buflen - ib->off);

	memcpy(buf, ib->buf + ib->off, todo);
	ib->off += todo;
	return todo;
}

ATF_TC(good_big5_getwc);
ATF_TC_HEAD(good_big5_getwc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test good big5 wchar getwc");
}

ATF_TC_BODY(good_big5_getwc, tc)
{
	const char buf[] = { 0xcf, 0x40 };
	struct ibuf ib = {
		.buf = buf,
		.buflen = sizeof(buf),
	};
	FILE *fp = funopen(&ib, readfn, NULL, NULL, NULL);

	ATF_REQUIRE(fp != NULL);
	setlocale(LC_CTYPE, "zh_TW.Big5");
	/* XXX implementation detail knowledge (wchar_t encoding) */
	ATF_REQUIRE_EQ(getwc(fp), 0xcf40);
	fclose(fp);
}

ATF_TC(bad_big5_getwc);
ATF_TC_HEAD(bad_big5_getwc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test bad big5 wchar getwc");
}

ATF_TC_BODY(bad_big5_getwc, tc)
{
	const char buf[] = { 0xcf, 0x20 };
	struct ibuf ib = {
		.buf = buf,
		.buflen = sizeof(buf),
	};
	FILE *fp = funopen(&ib, readfn, NULL, NULL, NULL);

	ATF_REQUIRE(fp != NULL);
	setlocale(LC_CTYPE, "zh_TW.Big5");
	ATF_REQUIRE_EQ(getwc(fp), WEOF);
	fclose(fp);
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, bad_big5_wprintf);
	ATF_TP_ADD_TC(tp, bad_big5_swprintf);
	ATF_TP_ADD_TC(tp, good_big5_wprintf);
	ATF_TP_ADD_TC(tp, good_big5_swprintf);
	ATF_TP_ADD_TC(tp, good_big5_getwc);
	ATF_TP_ADD_TC(tp, bad_big5_getwc);

	return atf_no_error();
}
