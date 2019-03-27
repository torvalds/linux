/* $NetBSD: t_inet_network.c,v 1.4 2015/04/09 16:47:56 ginsbach Exp $ */

/*
 * Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Brian Ginsbach.
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
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_inet_network.c,v 1.4 2015/04/09 16:47:56 ginsbach Exp $");

#include <arpa/inet.h>

#include <atf-c.h>

#define H_REQUIRE(input, expected)					\
	ATF_REQUIRE_EQ_MSG(inet_network(input), (in_addr_t) expected,	\
	    "inet_network(%s) returned: 0x%08X, expected: %s", #input,	\
	    inet_network(input), #expected)

ATF_TC(inet_network_basic);
ATF_TC_HEAD(inet_network_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks inet_network(3)");
}

ATF_TC_BODY(inet_network_basic, tc)
{

	H_REQUIRE("0x12", 0x00000012);
	H_REQUIRE("127.1", 0x00007f01);
	H_REQUIRE("127.1.2.3", 0x7f010203);
	H_REQUIRE("0X12", 0x00000012);
	H_REQUIRE("0", 0x0);
	H_REQUIRE("01.02.07.077", 0x0102073f);
	H_REQUIRE("0x1.23.045.0", 0x01172500);
	H_REQUIRE("0x12.0x34", 0x00001234);

	/* This is valid (because of the trailing space after the digit). */
	H_REQUIRE("1 bar", 0x00000001);
}

ATF_TC(inet_network_err);
ATF_TC_HEAD(inet_network_err, tc)
{
	atf_tc_set_md_var(tc, "descr", "Invalid addresses w/ inet_network(3)");
}

ATF_TC_BODY(inet_network_err, tc)
{
	/* Malformed requests. */
	H_REQUIRE("4.2.3.1.", 0xffffffff);
	H_REQUIRE("0x123456", 0xffffffff);
	H_REQUIRE("0x12.0x345", 0xffffffff);
	H_REQUIRE("1.2.3.4.5", 0xffffffff);
	H_REQUIRE("1..3.4", 0xffffffff);
	H_REQUIRE(".", 0xffffffff);
	H_REQUIRE("1.", 0xffffffff);
	H_REQUIRE(".1", 0xffffffff);
#if defined(__FreeBSD__) || defined(__APPLE__)
	H_REQUIRE("0x", 0x0);
#else
	H_REQUIRE("0x", 0xffffffff);
#endif
	H_REQUIRE("", 0xffffffff);
	H_REQUIRE(" ", 0xffffffff);
	H_REQUIRE("bar", 0xffffffff);
	H_REQUIRE("1.2bar", 0xffffffff);
	H_REQUIRE("1.", 0xffffffff);
	H_REQUIRE("\xc3\x8a\xc3\x83\xc3\x95\xc3\x8b\xc3\x85\xc3\x8e",
		0xffffffff);
	H_REQUIRE("255.255.255.255", 0xffffffff);
	H_REQUIRE("x", 0xffffffff);
	H_REQUIRE("078", 0xffffffff);
	H_REQUIRE("127.0xfff", 0xffffffff);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, inet_network_basic);
	ATF_TP_ADD_TC(tp, inet_network_err);

	return atf_no_error();
}
