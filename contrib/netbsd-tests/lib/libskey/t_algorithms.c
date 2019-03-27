/* $NetBSD: t_algorithms.c,v 1.1 2010/07/16 13:56:32 jmmv Exp $ */

/*
 * Copyright (c) 2000, 2008, 2010 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008, 2010\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_algorithms.c,v 1.1 2010/07/16 13:56:32 jmmv Exp $");

#include <stdio.h>
#include <strings.h>
#include <skey.h>

#include <atf-c.h>

#define H_REQUIRE(x, y) \
	ATF_REQUIRE_MSG(strcasecmp((x), (y)) == 0, "\"%s\" != \"%s\"", (x), (y))

static void
h_check(const char *pass, const char *seed,
        const char *algo, const char *zero,
        const char *one,  const char *nine)
{
	char prn[64];
	char data[16];
	int i;

	skey_set_algorithm(algo);

	keycrunch(data, seed, pass);
	btoa8(prn, data);
	H_REQUIRE(prn, zero);

	f(data);
	btoa8(prn, data);
	H_REQUIRE(prn, one);

	for(i = 1; i < 99; ++i)
		f(data);
	btoa8(prn, data);
	H_REQUIRE(prn, nine);
}

ATF_TC(md4);
ATF_TC_HEAD(md4, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks MD4 algorithm");
}
ATF_TC_BODY(md4, tc)
{
	h_check("This is a test.", "TeSt", "md4", "D1854218EBBB0B51",
		"63473EF01CD0B444", "C5E612776E6C237A");
	h_check("AbCdEfGhIjK", "alpha1", "md4", "50076F47EB1ADE4E",
		"65D20D1949B5F7AB", "D150C82CCE6F62D1");
	h_check("OTP's are good", "correct", "md4", "849C79D4F6F55388",
		"8C0992FB250847B1", "3F3BF4B4145FD74B");
}

ATF_TC(md5);
ATF_TC_HEAD(md5, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks MD5 algorithm");
}
ATF_TC_BODY(md5, tc)
{
	h_check("This is a test.", "TeSt", "md5", "9E876134D90499DD",
		"7965E05436F5029F", "50FE1962C4965880");
	h_check("AbCdEfGhIjK", "alpha1", "md5", "87066DD9644BF206",
		"7CD34C1040ADD14B", "5AA37A81F212146C");
	h_check("OTP's are good", "correct", "md5", "F205753943DE4CF9",
		"DDCDAC956F234937", "B203E28FA525BE47");
}

ATF_TC(sha1);
ATF_TC_HEAD(sha1, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks SHA1 algorithm");
}
ATF_TC_BODY(sha1, tc)
{
	h_check("This is a test.", "TeSt", "sha1","BB9E6AE1979D8FF4",
		"63D936639734385B", "87FEC7768B73CCF9");
	h_check("AbCdEfGhIjK", "alpha1", "sha1","AD85F658EBE383C9",
		"D07CE229B5CF119B", "27BC71035AAF3DC6");
	h_check("OTP's are good", "correct", "sha1","D51F3E99BF8E6F0B",
		"82AEB52D943774E4", "4F296A74FE1567EC");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, md4);
	ATF_TP_ADD_TC(tp, md5);
	ATF_TP_ADD_TC(tp, sha1);

	return atf_no_error();
}
