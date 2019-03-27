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

#include "netinet/in_print.c"

#include <atf-c.h>

static const struct {
	struct in_addr ia;
	const char *str;
	int len;
} tst[] = {
	{
		{	.s_addr = ntohl(INADDR_LOOPBACK)	},
		"127.0.0.1",
		9,
	},
	{
		{	.s_addr = ntohl(INADDR_ANY)		},
		"0.0.0.0",
		7,
	},
	{
		{	.s_addr = ntohl(IN_CLASSC_NET)		},
		"255.255.255.0",
		13,
	},
	{
		{	.s_addr = ntohl(INADDR_ALLHOSTS_GROUP)	},
		"224.0.0.1",
		9,
	},
};


ATF_TC(in_print);
ATF_TC_HEAD(in_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of struct in_addr");
}

ATF_TC_BODY(in_print, tc)
{
	char buf[INET_ADDRSTRLEN];
	int r;
	size_t l = sizeof(buf);

	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = in_print(buf, l, &tst[i].ia);
		ATF_REQUIRE_STREQ(buf, tst[i].str);
		ATF_REQUIRE_EQ(r, tst[i].len);
	}

	l = 8;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		r = in_print(buf, l, &tst[i].ia);
		ATF_CHECK(strncmp(buf, tst[i].str, l - 1) == 0);
		ATF_REQUIRE_EQ(buf[l - 1], '\0');
		ATF_REQUIRE_EQ(r, tst[i].len);
	}
}

ATF_TC(sin_print);
ATF_TC_HEAD(sin_print, tc)
{

	atf_tc_set_md_var(tc, "descr", "printing of sockaddr_in");
}

ATF_TC_BODY(sin_print, tc)
{
	char buf[1024];
	char res[1024];
	int r, e;
	size_t l = sizeof(buf);
	struct sockaddr_in sin;
	memset(&sin, 0, sizeof(sin));

	for (size_t i = 0; i < __arraycount(tst); i++) {
		sin.sin_addr = tst[i].ia;
		sin.sin_port = (in_port_t)htons(i);
		r = sin_print(buf, l, &sin);
		if (i == 0)
			e = snprintf(res, sizeof(res), "%s", tst[i].str);
		else
			e = snprintf(res, sizeof(res), "%s:%zu", tst[i].str, i);

		ATF_REQUIRE_STREQ(buf, res);
		ATF_REQUIRE_EQ(r, e);
	}

	l = 14;
	for (size_t i = 0; i < __arraycount(tst); i++) {
		sin.sin_addr = tst[i].ia;
		sin.sin_port = (in_port_t)htons(i);
		r = sin_print(buf, l, &sin);
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

	ATF_TP_ADD_TC(tp, in_print);
	ATF_TP_ADD_TC(tp, sin_print);
	return atf_no_error();
}
