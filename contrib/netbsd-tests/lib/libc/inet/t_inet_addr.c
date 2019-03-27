/* $NetBSD: t_inet_addr.c,v 1.1 2015/04/09 16:47:56 ginsbach Exp $ */

/*
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
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
__RCSID("$NetBSD: t_inet_addr.c,v 1.1 2015/04/09 16:47:56 ginsbach Exp $");

#include <arpa/inet.h>

#include <atf-c.h>
#include <stdio.h>
#include <string.h>

ATF_TC(inet_addr_basic);
ATF_TC_HEAD(inet_addr_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks inet_addr(3)");
}

ATF_TC_BODY(inet_addr_basic, tc)
{
	static const char *addrs[] = {
		"127.0.0.1", "99.99.99.99", "0.0.0.0", "255.255.255.255" };

	struct in_addr ia;
	const char *ian;
	in_addr_t addr;
	size_t i;

	for (i = 0; i < __arraycount(addrs); i++) {

		(void)fprintf(stderr, "checking %s\n", addrs[i]);;

		addr = inet_addr(addrs[i]);
		ia.s_addr = addr;
		ian = inet_ntoa(ia);

		ATF_REQUIRE(ian != NULL);
		ATF_CHECK(strcmp(ian, addrs[i]) == 0);
	}
}

ATF_TC(inet_addr_err);
ATF_TC_HEAD(inet_addr_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Invalid addresses with inet_addr(3)");
}

ATF_TC_BODY(inet_addr_err, tc)
{
	static const char *addrs[] = {
		". . . .", "1.2.3.", "0.0.0.256", "255.255.255.256",
		"................................................",
		"a.b.c.d", "0x0.0x1.0x2.0x3", "-1.-1.-1.-1", "", " "};

	struct in_addr ia;
	const char *ian;
	in_addr_t addr;
	size_t i;

	for (i = 0; i < __arraycount(addrs); i++) {

		(void)fprintf(stderr, "checking %s\n", addrs[i]);;

		addr = inet_addr(addrs[i]);
		ia.s_addr = addr;
		ian = inet_ntoa(ia);

		ATF_REQUIRE(ian != NULL);
		ATF_CHECK(strcmp(ian, addrs[i]) != 0);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, inet_addr_basic);
	ATF_TP_ADD_TC(tp, inet_addr_err);

	return atf_no_error();
}
