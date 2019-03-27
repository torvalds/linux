/* $NetBSD: t_fnmatch.c,v 1.7 2016/10/31 05:08:53 dholland Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Jukka Ruohonen.
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
__RCSID("$NetBSD: t_fnmatch.c,v 1.7 2016/10/31 05:08:53 dholland Exp $");

#include <atf-c.h>
#include <fnmatch.h>
#include <stdio.h>

ATF_TC(fnmatch_backslashes);
ATF_TC_HEAD(fnmatch_backslashes, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test translation of '\\' with fnmatch(3) (PR lib/41558)");
}

ATF_TC_BODY(fnmatch_backslashes, tc)
{
	const int rv = fnmatch(/* pattern */ "\\", "\\", 0);

	if (rv != FNM_NOMATCH)
		atf_tc_fail("fnmatch(3) did not translate '\\'");
}

ATF_TC(fnmatch_casefold);
ATF_TC_HEAD(fnmatch_casefold, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test FNM_CASEFOLD");
}

ATF_TC_BODY(fnmatch_casefold, tc)
{
	ATF_CHECK(fnmatch("xxx", "XXX", 0) != 0);
	ATF_CHECK(fnmatch("XXX", "xxx", 0) != 0);
	ATF_CHECK(fnmatch("xxx", "XxX", 0) != 0);
	ATF_CHECK(fnmatch("XxX", "xxx", 0) != 0);
	ATF_CHECK(fnmatch("x*x", "XXX", 0) != 0);
	ATF_CHECK(fnmatch("**x", "XXX", 0) != 0);
	ATF_CHECK(fnmatch("*?x", "XXX", 0) != 0);

	ATF_CHECK(fnmatch("xxx", "XXX", FNM_CASEFOLD) == 0);
	ATF_CHECK(fnmatch("XXX", "xxx", FNM_CASEFOLD) == 0);
	ATF_CHECK(fnmatch("xxx", "XxX", FNM_CASEFOLD) == 0);
	ATF_CHECK(fnmatch("XxX", "xxx", FNM_CASEFOLD) == 0);
	ATF_CHECK(fnmatch("x*x", "XXX", FNM_CASEFOLD) == 0);
	ATF_CHECK(fnmatch("**x", "XXX", FNM_CASEFOLD) == 0);
	ATF_CHECK(fnmatch("*?x", "XXX", FNM_CASEFOLD) == 0);
}

ATF_TC(fnmatch_leadingdir);
ATF_TC_HEAD(fnmatch_leadingdir, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test FNM_LEADING_DIR");
}

ATF_TC_BODY(fnmatch_leadingdir, tc)
{
	ATF_CHECK(fnmatch("", "/*", 0) != 0);
	ATF_CHECK(fnmatch(" ", " /*", 0) != 0);
	ATF_CHECK(fnmatch("x", "x/*", 0) != 0);
	ATF_CHECK(fnmatch("///", "////*", 0) != 0);

	ATF_CHECK(fnmatch("", "/*", FNM_LEADING_DIR) == 0);
	ATF_CHECK(fnmatch(" ", " /*", FNM_LEADING_DIR) == 0);
	ATF_CHECK(fnmatch("x", "x/*", FNM_LEADING_DIR) == 0);
	ATF_CHECK(fnmatch("///", "////*", FNM_LEADING_DIR) == 0);
}

ATF_TC(fnmatch_noescape);
ATF_TC_HEAD(fnmatch_noescape, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test FNM_NOESCAPE");
}

ATF_TC_BODY(fnmatch_noescape, tc)
{
	ATF_CHECK(fnmatch("  \\x", "  \\x", 0) != 0);
	ATF_CHECK(fnmatch("xx\\x", "xx\\x", 0) != 0);
	ATF_CHECK(fnmatch("\\xxx", "\\xxx", 0) != 0);

	ATF_CHECK(fnmatch("  \\x", "  \\x", FNM_NOESCAPE) == 0);
	ATF_CHECK(fnmatch("xx\\x", "xx\\x", FNM_NOESCAPE) == 0);
	ATF_CHECK(fnmatch("\\xxx", "\\xxx", FNM_NOESCAPE) == 0);
}

ATF_TC(fnmatch_pathname);
ATF_TC_HEAD(fnmatch_pathname, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test FNM_PATHNAME");
}

ATF_TC_BODY(fnmatch_pathname, tc)
{
	ATF_CHECK(fnmatch("???x", "xxx/x", FNM_PATHNAME) != 0);
	ATF_CHECK(fnmatch("***x", "xxx/x", FNM_PATHNAME) != 0);

	ATF_CHECK(fnmatch("???x", "xxxx", FNM_PATHNAME) == 0);
	ATF_CHECK(fnmatch("*/xxx", "/xxx", FNM_PATHNAME) == 0);
	ATF_CHECK(fnmatch("x/*.y", "x/z.y", FNM_PATHNAME) == 0);
}

ATF_TC(fnmatch_period);
ATF_TC_HEAD(fnmatch_period, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test FNM_PERIOD");
}

ATF_TC_BODY(fnmatch_period, tc)
{
	ATF_CHECK(fnmatch("*x*", "X", FNM_PERIOD) != 0);
	ATF_CHECK(fnmatch("*x*", "X", FNM_PERIOD | FNM_CASEFOLD) == 0);

	ATF_CHECK(fnmatch("x?y", "x.y", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch("x*y", "x.y", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch("*.c", "x.c", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch("*/?", "x/y", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch("*/*", "x/y", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch(".*/?", ".x/y", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch("*/.?", "x/.y", FNM_PATHNAME | FNM_PERIOD) == 0);
	ATF_CHECK(fnmatch("x[.]y", "x.y", FNM_PATHNAME | FNM_PERIOD) == 0);

	ATF_CHECK(fnmatch("?x/y", ".x/y", FNM_PATHNAME | FNM_PERIOD) != 0);
	ATF_CHECK(fnmatch("*x/y", ".x/y", FNM_PATHNAME | FNM_PERIOD) != 0);
	ATF_CHECK(fnmatch("x/?y", "x/.y", FNM_PATHNAME | FNM_PERIOD) != 0);
	ATF_CHECK(fnmatch("x/*y", "x/.y", FNM_PATHNAME | FNM_PERIOD) != 0);
}

ATF_TC(fnmatch_initialbracket);
ATF_TC_HEAD(fnmatch_initialbracket, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test fnmatch with initial [");
}

ATF_TC_BODY(fnmatch_initialbracket, tc)
{
	ATF_CHECK(fnmatch("[[?*\\\\]", "\\", 0) == 0);
	ATF_CHECK(fnmatch("[]?*\\\\]", "]", 0) == 0);
	ATF_CHECK(fnmatch("[!]a-]", "b", 0) == 0);
	ATF_CHECK(fnmatch("[]-_]", "^", 0) == 0); /* range: ']', '^', '_' */
	ATF_CHECK(fnmatch("[!]-_]", "X", 0) == 0);
	ATF_CHECK(fnmatch("[A-\\\\]", "[", 0) == 0);
	ATF_CHECK(fnmatch("[a-z]/[a-z]", "a/b", 0) == 0);
	ATF_CHECK(fnmatch("[*]/b", "*/b", 0) == 0);
	ATF_CHECK(fnmatch("[?]/b", "?/b", 0) == 0);
	ATF_CHECK(fnmatch("[[a]/b", "a/b", 0) == 0);
	ATF_CHECK(fnmatch("[[a]/b", "[/b", 0) == 0);
	ATF_CHECK(fnmatch("[/b", "[/b", 0) == 0);

	ATF_CHECK(fnmatch("[*]/b", "a/b", 0) != 0);
	ATF_CHECK(fnmatch("[?]/b", "a/b", 0) != 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, fnmatch_backslashes);
	ATF_TP_ADD_TC(tp, fnmatch_casefold);
	ATF_TP_ADD_TC(tp, fnmatch_leadingdir);
	ATF_TP_ADD_TC(tp, fnmatch_noescape);
	ATF_TP_ADD_TC(tp, fnmatch_pathname);
	ATF_TP_ADD_TC(tp, fnmatch_period);
	ATF_TP_ADD_TC(tp, fnmatch_initialbracket);

	return atf_no_error();
}
