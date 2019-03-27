/*	$NetBSD: t_print.c,v 1.2 2016/08/27 11:30:49 christos Exp $	*/

/*-
 * Copyright (c) 2014 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_print.c,v 1.2 2016/08/27 11:30:49 christos Exp $");

#include "net/dl_print.c"

#include <atf-c.h>

static const struct {
	struct dl_addr ia;
	const char *str;
	int len;
} tst[] = {
	{
		{
			.dl_type = 6,
			.dl_nlen = 0,
			.dl_alen = 6,
			.dl_slen = 0,
			.dl_data = {
			    (char)0x01, (char)0xa2, (char)0x03,
			    (char)0xc4, (char)0x05, (char)0xf6,
			},
		},
		"/6#01:a2:03:c4:05:f6",
		20,
	},
	{
		{
			.dl_type = 24,
			.dl_nlen = 3,
			.dl_alen = 6,
			.dl_slen = 0,
			.dl_data = {
			    'l', 'o', '0',
			    (char)0x11, (char)0x22, (char)0x33,
			    (char)0x44, (char)0x55, (char)0x66,
			},
		},
		"lo0/24#11:22:33:44:55:66",
		24,
	},
	{
		{
			.dl_type = 24,
			.dl_nlen = 7,
			.dl_alen = 1,
			.dl_slen = 0,
			.dl_data = {
			    'n', 'p', 'f', 'l', 'o', 'g', '0', (char)0xa5,
			},
		},
		"npflog0/24#a5",
		13,
	},
	{
		{
			.dl_type = 0,
			.dl_nlen = 0,
			.dl_alen = 0,
			.dl_slen = 0,
			.dl_data = {
			    '\0'
			},
		},
		"/0#",
		3,
	},
};


ATF_TC(dl_print);
ATF_TC_HEAD(dl_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of link address");
}

ATF_TC_BODY(dl_print, tc)
{
	char buf[LINK_ADDRSTRLEN];
	int r;
	size_t l = sizeof(buf);

	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = dl_print(buf, l, &tst[i].ia);
		ATF_REQUIRE_STREQ(buf, tst[i].str);
		ATF_REQUIRE_EQ(r, tst[i].len);
	}

	l = 4;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = dl_print(buf, l, &tst[i].ia);
		ATF_CHECK(strncmp(buf, tst[i].str, l - 1) == 0);
		if (r > (int)l)
			ATF_REQUIRE_EQ(buf[l - 1], '\0');
		ATF_REQUIRE_EQ(r, tst[i].len);
	}
}

ATF_TC(sdl_print);
ATF_TC_HEAD(sdl_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of sockaddr_dl");
}

ATF_TC_BODY(sdl_print, tc)
{
	char buf[1024];
	char res[1024];
	int r, e;
	size_t l = sizeof(buf);
	struct sockaddr_dl sdl;

	memset(&sdl, 0, sizeof(sdl));
	for (size_t i = 0; i < __arraycount(tst); i++) {
		memcpy(&sdl.sdl_addr, &tst[i].ia, sizeof(sdl.sdl_addr));
		sdl.sdl_index = (uint16_t)i;
		r = sdl_print(buf, l, &sdl);
		if (i == 3)
			e = snprintf(res, l, "link#%zu", i);
		else
			e = snprintf(res, l, "[%s]:%zu", tst[i].str, i);
		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}

	l = 8;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		memcpy(&sdl.sdl_addr, &tst[i].ia, sizeof(sdl.sdl_addr));
		sdl.sdl_index = (uint16_t)i;
		r = sdl_print(buf, l, &sdl);
		if (i == 3)
			e = snprintf(res, l, "link#%zu", i);
		else
			e = snprintf(res, l, "[%s]:%zu", tst[i].str, i);
		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, dl_print);
	ATF_TP_ADD_TC(tp, sdl_print);
	return atf_no_error();
}
