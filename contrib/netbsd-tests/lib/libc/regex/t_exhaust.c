/*	$NetBSD: t_exhaust.c,v 1.9 2019/03/16 21:57:15 christos Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
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
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *        This product includes software developed by the NetBSD
 *        Foundation, Inc. and its contributors.
 * 4. Neither the name of The NetBSD Foundation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
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
__RCSID("$NetBSD: t_exhaust.c,v 1.9 2019/03/16 21:57:15 christos Exp $");

#include <sys/resource.h>
#include <atf-c.h>
#include <err.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef REGEX_MAXSIZE
#define REGEX_MAXSIZE	9999
#endif

static char *
mkstr(const char *str, size_t len)
{
	size_t slen = strlen(str);
	char *p = malloc(slen * len + 1);
	ATF_REQUIRE_MSG(p != NULL, "slen=%zu, len=%zu", slen, len);
	for (size_t i = 0; i < len; i++)
		strcpy(&p[i * slen], str);
	return p;
}

static char *
concat(const char *d, const char *s)
{
	size_t dlen = strlen(d);
	size_t slen = strlen(s);
	char *p = malloc(dlen + slen + 1);

	ATF_REQUIRE(p != NULL);
	strcpy(p, d);
	strcpy(p + dlen, s);
	return p;
}

static char *
p0(size_t len)
{
	char *d, *s1, *s2;
	s1 = mkstr("\\(", len);
	s2 = concat(s1, ")");
	free(s1);
	d = concat("(", s2);
	free(s2);
	return d;
}

static char *
p1(size_t len)
{
	char *d, *s1, *s2, *s3;
	s1 = mkstr("\\(", 60);
	s2 = mkstr("(.*)", len);
	s3 = concat(s1, s2);
	free(s2);
	free(s1);
	s1 = concat(s3, ")");
	free(s3);
	d = concat("(", s1);
	free(s1);
	return d;
}

static char *
ps(const char *m, const char *s, size_t len)
{
	char *d, *s1, *s2, *s3;
	s1 = mkstr(m, len);
	s2 = mkstr(s, len);
	s3 = concat(s1, s2);
	free(s2);
	free(s1);
	d = concat("(.?)", s3);
	free(s3);
	return d;
}

static char *
p2(size_t len)
{
	return ps("((.*){0,255}", ")", len);
}

static char *
p3(size_t len)
{
	return ps("(.\\{0,}", ")", len);
}

static char *
p4(size_t len)
{
	return ps("((.*){1,255}", ")", len);
}

static char *
p5(size_t len)
{
	return ps("(", "){1,100}", len);
}

static char *
p6(size_t len)
{
	char *d, *s1, *s2;
	s1 = mkstr("(?:(.*)|", len);
	s2 = concat(s1, "(.*)");
	free(s1);
	s1 = mkstr(")", len);
	d = concat(s2, s1);
	free(s1);
	free(s2);
	return d;
}

static const struct {
	char *(*pattern)(size_t);
	int type;
} tests[] = {
	{ p0, REG_EXTENDED },
	{ p1, REG_EXTENDED },
	{ p2, REG_EXTENDED },
	{ p3, REG_EXTENDED },
	{ p4, REG_EXTENDED },
	{ p5, REG_EXTENDED },
	{ p6, REG_BASIC },
};

ATF_TC(regcomp_too_big);

ATF_TC_HEAD(regcomp_too_big, tc)
{

	atf_tc_set_md_var(tc, "descr", "Check that large patterns don't"
	    " crash, but return a proper error code");
	// libtre needs it.
	atf_tc_set_md_var(tc, "timeout", "600");
	atf_tc_set_md_var(tc, "require.memory", "64M");
}

ATF_TC_BODY(regcomp_too_big, tc)
{
	regex_t re;
	int e;
	struct rlimit limit;

	limit.rlim_cur = limit.rlim_max = 256 * 1024 * 1024;
	ATF_REQUIRE(setrlimit(RLIMIT_VMEM, &limit) != -1);

	for (size_t i = 0; i < __arraycount(tests); i++) {
		char *d = (*tests[i].pattern)(REGEX_MAXSIZE);
		e = regcomp(&re, d, tests[i].type);
		if (e) {
			char ebuf[1024];
			(void)regerror(e, &re, ebuf, sizeof(ebuf));
			ATF_REQUIRE_MSG(e == REG_ESPACE,
			    "regcomp returned %d (%s) for pattern %zu [%s]", e, ebuf,
			    i, d);
			free(d);
			continue;
		}
		free(d);
		(void)regexec(&re, "aaaaaaaaaaa", 0, NULL, 0);
		regfree(&re);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, regcomp_too_big);
	return atf_no_error();
}
