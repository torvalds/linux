/*	$NetBSD: t_print.c,v 1.2 2014/12/03 13:10:49 christos Exp $	*/

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
__RCSID("$NetBSD: t_print.c,v 1.2 2014/12/03 13:10:49 christos Exp $");

#include "netinet6/in6_print.c"
#include "netinet/in_print.c"

#include <atf-c.h>

static const struct {
	struct in6_addr ia;
	const char *str;
	int len;
} tst[] = {
	{
		IN6ADDR_ANY_INIT,
		"::",
		2,
	},
	{
		IN6ADDR_LOOPBACK_INIT,
		"::1",
		3,
	},
	{
		IN6ADDR_NODELOCAL_ALLNODES_INIT,
		"ff01::1",
		7,
	},
	{
		{{{ 0x10, 0x20, 0x30, 0x40, 0x50, 0x60, 0x70, 0x80,
		    0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08 }}},
		"1020:3040:5060:7080:102:304:506:708",
		35,
	},
	{
		{{{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
		    0x00, 0x00, 0xff, 0xff, 0x88, 0x44, 0x22, 0x11 }}},
		"::ffff:136.68.34.17",
		19,
	},
};


ATF_TC(in6_print);
ATF_TC_HEAD(in6_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of struct in6_addr");
}

ATF_TC_BODY(in6_print, tc)
{
	char buf[INET6_ADDRSTRLEN];
	int r;
	size_t l = sizeof(buf);

	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = in6_print(buf, l, &tst[i].ia);
		ATF_REQUIRE_STREQ(buf, tst[i].str);
		ATF_REQUIRE_EQ(r, tst[i].len);
	}

	l = 12;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = in6_print(buf, l, &tst[i].ia);
		ATF_CHECK(strncmp(buf, tst[i].str, l - 1) == 0);
		if (r > (int)l)
			ATF_REQUIRE_EQ(buf[l - 1], '\0');
		ATF_REQUIRE_EQ(r, tst[i].len);
	}
}

ATF_TC(sin6_print);
ATF_TC_HEAD(sin6_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of sockaddr_in6");
}

ATF_TC_BODY(sin6_print, tc)
{
	char buf[1024];
	char res[1024];
	int r, e;
	size_t l = sizeof(buf);
	struct sockaddr_in6 sin6;
	memset(&sin6, 0, sizeof(sin6));

	for (size_t i = 0; i < __arraycount(tst); i++) {
		sin6.sin6_addr = tst[i].ia;
		sin6.sin6_port = (in_port_t)htons(i);
		r = sin6_print(buf, l, &sin6);
		if (i == 0)
			e = snprintf(res, sizeof(res), "%s", tst[i].str);
		else
			e = snprintf(res, sizeof(res), "[%s]:%zu",
			    tst[i].str, i);

		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}

	l = 14;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		sin6.sin6_addr = tst[i].ia;
		sin6.sin6_port = (in_port_t)htons(i);
		r = sin6_print(buf, l, &sin6);
		if (i == 0)
			e = snprintf(res, l, "%s", tst[i].str);
		else
			e = snprintf(res, l, "[%s]:%zu", tst[i].str, i);

		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, in6_print);
	ATF_TP_ADD_TC(tp, sin6_print);
	return atf_no_error();
}
