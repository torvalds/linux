/*	$NetBSD: t_efun.c,v 1.3 2012/11/04 23:37:02 christos Exp $ */

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
__RCSID("$NetBSD: t_efun.c,v 1.3 2012/11/04 23:37:02 christos Exp $");

#include <atf-c.h>

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <util.h>

static bool	fail;
static void	handler(int, const char *, ...);

static void
handler(int ef, const char *fmt, ...)
{
	fail = false;
}

ATF_TC(ecalloc);
ATF_TC_HEAD(ecalloc, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of ecalloc(3)");
}

ATF_TC_BODY(ecalloc, tc)
{
	char *x;

	fail = true;
	x = ecalloc(-1, 1);

	ATF_REQUIRE(x == NULL);
	ATF_REQUIRE(fail != true);

	fail = true;
	x = ecalloc(SIZE_MAX, 2);

	ATF_REQUIRE(x == NULL);
	ATF_REQUIRE(fail != true);
}

ATF_TC(efopen);
ATF_TC_HEAD(efopen, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of efopen(3)");
}

ATF_TC_BODY(efopen, tc)
{
	FILE *f;

	fail = true;
	f = efopen("XXX", "XXX");

	ATF_REQUIRE(f == NULL);
	ATF_REQUIRE(fail != true);
}

ATF_TC(emalloc);
ATF_TC_HEAD(emalloc, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of emalloc(3)");
}

ATF_TC_BODY(emalloc, tc)
{
	char *x;

	fail = true;
	x = emalloc(-1);

	ATF_REQUIRE(x == NULL);
	ATF_REQUIRE(fail != true);
}

ATF_TC(erealloc);
ATF_TC_HEAD(erealloc, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of erealloc(3)");
}

ATF_TC_BODY(erealloc, tc)
{
	char *x;

	fail = true;
	x = erealloc(NULL, -1);

	ATF_REQUIRE(x == NULL);
	ATF_REQUIRE(fail != true);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_REQUIRE(esetfunc(handler) != NULL);

	ATF_TP_ADD_TC(tp, ecalloc);
	ATF_TP_ADD_TC(tp, efopen);
	ATF_TP_ADD_TC(tp, emalloc);
	ATF_TP_ADD_TC(tp, erealloc);

	return atf_no_error();
}
