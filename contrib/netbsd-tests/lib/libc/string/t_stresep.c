/*	$NetBSD: t_stresep.c,v 1.3 2013/02/15 23:56:32 christos Exp $ */

/*-
 * Copyright (c) 2005 The NetBSD Foundation, Inc.
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

#include <atf-c.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define expect(a)							\
	if ((p = stresep(&q, " ", '\\')) == NULL || strcmp(p, a)) {	\
		fprintf(stderr, "failed on line %d: %s != %s\n",	\
		    __LINE__, p, a);					\
		atf_tc_fail("Check stderr for test id/line");		\
	}

ATF_TC(stresep_basic);
ATF_TC_HEAD(stresep_basic, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test stresep results");
}

ATF_TC_BODY(stresep_basic, tc)
{
	char brkstr[] = "foo\\ \\ bar baz bar\\ foo\\  bar\\ \\ foo \\ \\ \\ "
		     "baz bar\\ \\ ";
	char *p, *q = brkstr;

	expect("foo  bar");
	expect("baz");
	expect("bar foo ");
	expect("bar  foo");
	expect("   baz");
	expect("bar  ");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, stresep_basic);

	return atf_no_error();
}
