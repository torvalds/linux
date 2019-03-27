/*	$NetBSD: t_print.c,v 1.1 2014/12/02 19:48:21 christos Exp $	*/

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
__RCSID("$NetBSD: t_print.c,v 1.1 2014/12/02 19:48:21 christos Exp $");

#include "netatalk/at_print.c"

#include <atf-c.h>

static const struct {
	struct at_addr ia;
	const char *str;
	int len;
} tst[] = {
	{
		{ 0, 0 },
		"0.0",
		3,
	},
	{
		{ htons(3), 255 },
		"3.255",
		5,
	},
};


ATF_TC(at_print);
ATF_TC_HEAD(at_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of struct at_addr");
}

ATF_TC_BODY(at_print, tc)
{
	char buf[ATALK_ADDRSTRLEN];
	int r;
	size_t l = sizeof(buf);

	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = at_print(buf, l, &tst[i].ia);
		ATF_REQUIRE_STREQ(buf, tst[i].str);
		ATF_REQUIRE_EQ(r, tst[i].len);
	}

	l = 4;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = at_print(buf, l, &tst[i].ia);
		ATF_CHECK(strncmp(buf, tst[i].str, l - 1) == 0);
		if (r > (int)l)
			ATF_REQUIRE_EQ(buf[l - 1], '\0');
		ATF_REQUIRE_EQ(r, tst[i].len);
	}
}

ATF_TC(sat_print);
ATF_TC_HEAD(sat_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of sockaddr_at");
}

ATF_TC_BODY(sat_print, tc)
{
	char buf[1024];
	char res[1024];
	int r, e;
	size_t l = sizeof(buf);
	struct sockaddr_at sat;

	memset(&sat, 0, sizeof(sat));
	for (size_t i = 0; i < __arraycount(tst); i++) {
		sat.sat_addr = tst[i].ia;
		sat.sat_port = (uint8_t)i;
		r = sat_print(buf, l, &sat);
		if (i == 0)
			e = snprintf(res, sizeof(res), "%s", tst[i].str);
		else
			e = snprintf(res, sizeof(res), "%s:%zu", tst[i].str, i);

		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}

	l = 8;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		sat.sat_addr = tst[i].ia;
		sat.sat_port = (uint8_t)i;
		r = sat_print(buf, l, &sat);
		if (i == 0)
			e = snprintf(res, l, "%s", tst[i].str);
		else
			e = snprintf(res, l, "%s:%zu", tst[i].str, i);

		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, at_print);
	ATF_TP_ADD_TC(tp, sat_print);
	return atf_no_error();
}
