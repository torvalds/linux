/*	$NetBSD: t_regex_att.c,v 1.3 2017/01/14 20:59:23 christos Exp $	*/

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
__RCSID("$NetBSD: t_regex_att.c,v 1.3 2017/01/14 20:59:23 christos Exp $");

#include <sys/param.h>

#include <atf-c.h>
#include <ctype.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <util.h>
#include <vis.h>

static const char sep[] = "\r\n\t";
static const char delim[3] = "\\\\\0";


static void
fail(const char *pattern, const char *input, size_t lineno) {
	fprintf(stderr,
	    "skipping failed test at line %zu (pattern=%s, input=%s)\n",
	    lineno, pattern, input);
}

static int
bug(const char *pattern, const char *input, size_t lineno) {
	static const struct {
		const char *p;
		const char *i;
	} b[] = {
#if defined(REGEX_SPENCER)
		/*
		 * The default libc implementation by Henry Spencer
		 */
		{ "a[-]?c", "ac" },			// basic.dat
		{ "(a*)*", "a" },			// categorization.dat
		{ "(aba|a*b)*", "ababa" },		// categorization.dat
		{ "\\(a\\(b\\)*\\)*\\2", "abab" },	// categorization.dat
		{ "(a*)*", "aaaaaa" },			// nullsubexpression.dat
		{ "(a*)*", "aaaaaax" },			// nullsubexpression.dat
		{ "(a*)+", "a" },			// nullsubexpression.dat
		{ "(a*)+", "aaaaaa" },			// nullsubexpression.dat
		{ "(a*)+", "aaaaaax" },			// nullsubexpression.dat
		{ "([a]*)*", "a" },			// nullsubexpression.dat
		{ "([a]*)*", "aaaaaa" },		// nullsubexpression.dat
		{ "([a]*)*", "aaaaaax" },		// nullsubexpression.dat
		{ "([a]*)+", "a" },			// nullsubexpression.dat
		{ "([a]*)+", "aaaaaa" },		// nullsubexpression.dat
		{ "([a]*)+", "aaaaaax" },		// nullsubexpression.dat
		{ "([^b]*)*", "a" },			// nullsubexpression.dat
		{ "([^b]*)*", "aaaaaa" },		// nullsubexpression.dat
		{ "([^b]*)*", "aaaaaab" },		// nullsubexpression.dat
		{ "([ab]*)*", "a" },			// nullsubexpression.dat
		{ "([ab]*)*", "aaaaaa" },		// nullsubexpression.dat
		{ "([ab]*)*", "ababab" },		// nullsubexpression.dat
		{ "([ab]*)*", "bababa" },		// nullsubexpression.dat
		{ "([ab]*)*", "b" },			// nullsubexpression.dat
		{ "([ab]*)*", "bbbbbb" },		// nullsubexpression.dat
		{ "([ab]*)*", "aaaabcde" },		// nullsubexpression.dat
		{ "([^a]*)*", "b" },			// nullsubexpression.dat
		{ "([^a]*)*", "bbbbbb" },		// nullsubexpression.dat
		{ "([^ab]*)*", "ccccxx" },		// nullsubexpression.dat
		{ "\\(a*\\)*\\(x\\)", "ax" },		// nullsubexpression.dat
		{ "\\(a*\\)*\\(x\\)", "axa" },		// nullsubexpression.dat
		{ "\\(a*\\)*\\(x\\)\\(\\1\\)", "x" },	// nullsubexpression.dat
/* crash! */	{ "\\(a*\\)*\\(x\\)\\(\\1\\)", "ax" },	// nullsubexpression.dat
/* crash! */	{ "\\(a*\\)*\\(x\\)\\(\\1\\)\\(x\\)", "axxa" },	// ""
		{ "(a*)*(x)",  "ax" },			// nullsubexpression.dat
		{ "(a*)*(x)",  "axa" },			// nullsubexpression.dat
		{ "(a*)+(x)",  "ax" },			// nullsubexpression.dat
		{ "(a*)+(x)",  "axa" },			// nullsubexpression.dat
		{ "((a|ab)(c|bcd))(d*)", "abcd" },	// forcedassoc.dat
		{ "((a|ab)(bcd|c))(d*)", "abcd" },	// forcedassoc.dat
		{ "((ab|a)(c|bcd))(d*)", "abcd" },	// forcedassoc.dat
		{ "((ab|a)(bcd|c))(d*)", "abcd" },	// forcedassoc.dat
		{ "((a*)(b|abc))(c*)", "abc" },		// forcedassoc.dat
		{ "((a*)(abc|b))(c*)", "abc" },		// forcedassoc.dat
		{ "((..)|(.)){2}", "aaa" },		// repetition.dat
		{ "((..)|(.)){3}", "aaa" },		// repetition.dat
		{ "((..)|(.)){3}", "aaaa" },		// repetition.dat
		{ "((..)|(.)){3}", "aaaaa" },		// repetition.dat
		{ "X(.?){0,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){1,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){2,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){3,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){4,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){5,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){6,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){7,}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){0,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){1,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){2,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){3,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){4,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){5,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){6,8}Y", "X1234567Y" },		// repetition.dat
		{ "X(.?){7,8}Y", "X1234567Y" },		// repetition.dat
		{ "(a|ab|c|bcd){0,}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd){1,}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd){2,}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd){3,}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd){1,10}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd){2,10}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd){3,10}(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd)*(d*)", "ababcd" },	// repetition.dat
		{ "(a|ab|c|bcd)+(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){0,}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){1,}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){2,}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){3,}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){1,10}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){2,10}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd){3,10}(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd)*(d*)", "ababcd" },	// repetition.dat
		{ "(ab|a|c|bcd)+(d*)", "ababcd" },	// repetition.dat
#elif defined(REGEX_TRE)
		{ "a[-]?c", "ac" },			// basic.dat
		{ "a\\(b\\)*\\1", "a" },		// categorization.dat
		{ "a\\(b\\)*\\1", "abab" },		// categorization.dat
		{ "\\(a\\(b\\)*\\)*\\2", "abab" },	// categorization.dat
		{ "\\(a*\\)*\\(x\\)\\(\\1\\)", "ax" },	// categorization.dat
		{ "\\(a*\\)*\\(x\\)\\(\\1\\)\\(x\\)", "axxa" },	// ""
		{ "((..)|(.))*", "aa" },		// repetition.dat
		{ "((..)|(.))*", "aaa" },		// repetition.dat
		{ "((..)|(.))*", "aaaaa" },		// repetition.dat
		{ "X(.?){7,}Y", "X1234567Y" },		// repetition.dat
#else
		{ "", "" }
#endif
	};

	for (size_t i = 0; i < __arraycount(b); i++) {
		if (strcmp(pattern, b[i].p) == 0 &&
		    strcmp(input, b[i].i) == 0) {
			fail(pattern, input, lineno);
			return 1;
		}
	}
	return 0;
}

#ifdef REGEX_SPENCER
#define HAVE_BRACES	1
#define HAVE_MINIMAL	0
#endif
#ifndef HAVE_BRACES
#define HAVE_BRACES	1
#endif
#ifndef HAVE_MINIMAL
#define HAVE_MINIMAL	1
#endif

static int
optional(const char *s)
{
	static const struct{
		const char *n;
		int v;
	} nv[]= {
		{ "[[<element>]] not supported", HAVE_BRACES },
		{ "no *? +? mimimal match ops", HAVE_MINIMAL },
	};

	for (size_t i = 0; i < __arraycount(nv); i++)
		if (strcmp(nv[i].n, s) == 0) {
			if (nv[i].v)
				return 0;
			fprintf(stderr, "skipping unsupported [%s] tests\n", s);
			return 1;
		}

	ATF_REQUIRE_MSG(0, "Unknown feature: %s", s);
	return 0;
}

static int
unsupported(const char *s)
{
	static const char *we[] = {
#if defined(REGEX_SPENCER)
		"ASSOCIATIVITY=left",		// have right associativity
		"SUBEXPRESSION=precedence",	// have grouping subexpression
		"REPEAT_LONGEST=last",		// have first repeat longest
		"BUG=alternation-order",	// don't have it
		"BUG=first-match",		// don't have it
		"BUG=nomatch-match",		// don't have it
		"BUG=repeat-any",		// don't have it
		"BUG=range-null",		// don't have it
		"BUG=repeat-null-unknown",	// don't have it
		"BUG=repeat-null",		// don't have it
		"BUG=repeat-artifact",		// don't have it
		"BUG=subexpression-first",	// don't have it
#elif defined(REGEX_TRE)
		"ASSOCIATIVITY=right",		// have left associativity
		"SUBEXPRESSION=grouping",	// have precedence subexpression
		"REPEAT_LONGEST=first",		// have last repeat longest
		"LENGTH=first",			// have last length
		"BUG=alternation-order",	// don't have it
		"BUG=first-match",		// don't have it
		"BUG=range-null",		// don't have it
		"BUG=repeat-null",		// don't have it
		"BUG=repeat-artifact",		// don't have it
		"BUG=subexpression-first",	// don't have it
		"BUG=repeat-short",		// don't have it
#endif
	};

	if (s == NULL)
		return 0;

	while (*s == '#' || isspace((unsigned char)*s))
		s++;

	for (size_t i = 0; i < __arraycount(we); i++)
		if (strcmp(we[i], s) == 0)
			return 1;
	return 0;
}

static void
geterror(const char *s, int *comp, int *exec)
{
	static const struct {
		const char *n;
		int v;
		int ce;
	} nv[] = {
#define COMP 1
#define EXEC 2
		{ "OK", 0, COMP|EXEC },
#define _DO(a, b)	{ # a, REG_ ## a, b },
		_DO(NOMATCH, EXEC)
		_DO(BADPAT, COMP)
		_DO(ECOLLATE, COMP)
		_DO(ECTYPE, COMP)
		_DO(EESCAPE, COMP)
		_DO(ESUBREG, COMP)
		_DO(EBRACK, COMP)
		_DO(EPAREN, COMP)
		_DO(EBRACE, COMP)
		_DO(BADBR, COMP)
		_DO(ERANGE, COMP)
		_DO(ESPACE, EXEC)
		_DO(BADRPT, COMP)
		_DO(EMPTY, COMP)
		_DO(ASSERT, COMP)
		_DO(INVARG, COMP)
		_DO(ENOSYS, COMP)
#undef _DO
	};
	*comp = 0;
	*exec = 0;
	for (size_t i = 0; i < __arraycount(nv); i++)
		if (strcmp(s, nv[i].n) == 0) {
			if (nv[i].ce & COMP)
				*comp = nv[i].v;
			if (nv[i].ce & EXEC)
				*exec = nv[i].v;
			return;
		}
	ATF_REQUIRE_MSG(0, "Unknown error %s", s);
	return;
}

static int
getflags(char *s)
{
	int flags = 0;

	for (;; s++)
		switch (*s) {
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			*s = '\0';
			break;
		case '\0':
			return flags;
		case 'B':
		case 'E':
		case 'F':
		case 'L':
			break;
		case 'i':
			flags |= REG_ICASE;
			*s = '\0';
			break;
		case '$':
			*s = '\0';
			break;
		case 'n':
			*s = '\0';
			break;
		default:
			ATF_REQUIRE_MSG(0, "Unknown char %c", *s);
			break;
		}
}

static size_t
getmatches(const char *s)
{
	size_t i;
	char *q;
	for (i = 0; (q = strchr(s, '(')) != NULL; i++, s = q + 1)
		continue;
	ATF_REQUIRE_MSG(i != 0, "No parentheses found");
	return i;
}

static void
checkcomment(const char *s, size_t lineno)
{
	if (s && strstr(s, "BUG") != NULL)
		fprintf(stderr, "Expected %s at line %zu\n", s, lineno);
}

static void
checkmatches(const char *matches, size_t nm, const regmatch_t *pm,
    size_t lineno)
{
	if (nm == 0)
		return;

	char *res;
	size_t len = strlen(matches) + 1, off = 0;

	ATF_REQUIRE((res = strdup(matches)) != NULL);
	for (size_t i = 0; i < nm; i++) {
		int l;
		if (pm[i].rm_so == -1 && pm[i].rm_eo == -1)
			l = snprintf(res + off, len - off, "(?,?)");
		else
			l = snprintf(res + off, len - off, "(%lld,%lld)",
			    (long long)pm[i].rm_so, (long long)pm[i].rm_eo);
		ATF_REQUIRE_MSG((size_t) l < len - off, "String too long %s"
		    " cur=%d, max=%zu", res, l, len - off);
		off += l;
	}
	ATF_CHECK_STREQ_MSG(res, matches, " at line %zu", lineno);
	free(res);
}

static void
att_test(const struct atf_tc *tc, const char *data_name)
{
	regex_t re;
	char *line, *lastpattern = NULL, data_path[MAXPATHLEN];
	size_t len, lineno = 0;
	int skipping = 0;
	FILE *input_file;

	snprintf(data_path, sizeof(data_path), "%s/data/%s.dat",
	    atf_tc_get_config_var(tc, "srcdir"), data_name);

	input_file = fopen(data_path, "r");
	if (input_file == NULL)
		atf_tc_fail("Failed to open input file %s", data_path);

	for (; (line = fparseln(input_file, &len, &lineno, delim, 0))
	    != NULL; free(line)) {
		char *name, *pattern, *input, *matches, *comment;
		regmatch_t *pm;
		size_t nm;
#ifdef DEBUG
		fprintf(stderr, "[%s]\n", line);
#endif
		if ((name = strtok(line, sep)) == NULL)
			continue;

		/*
		 * We check these early so that we skip the lines quickly
		 * in order to do more strict testing on the other arguments
		 * The same characters are also tested in the switch below
		 */
		if (*name == '}') {
			skipping = 0;
			continue;
		}
		if (skipping)
			continue;
		if (*name == ';' || *name == '#' || strcmp(name, "NOTE") == 0)
			continue;
		if (*name == ':') {
			/* Skip ":HA#???:" prefix */
			while (*++name && *name != ':')
				continue;
			if (*name)
				name++;
		}

		ATF_REQUIRE_MSG((pattern = strtok(NULL, sep)) != NULL,
			"Missing pattern at line %zu", lineno);
		ATF_REQUIRE_MSG((input = strtok(NULL, sep)) != NULL,
			"Missing input at line %zu", lineno);

		if (strchr(name, '$')) {
			ATF_REQUIRE(strunvis(pattern, pattern) != -1);
			ATF_REQUIRE(strunvis(input, input) != -1);
		}


		if (strcmp(input, "NULL") == 0)
			*input = '\0';

		if (strcmp(pattern, "SAME") == 0) {
			ATF_REQUIRE(lastpattern != NULL);
			pattern = lastpattern;
		} else {
			free(lastpattern);
			ATF_REQUIRE((lastpattern = strdup(pattern)) != NULL);
		}

		ATF_REQUIRE_MSG((matches = strtok(NULL, sep)) != NULL,
		    "Missing matches at line %zu", lineno);

		comment = strtok(NULL, sep);
		switch (*name) {
		case '{':	/* Begin optional implementation */
			if (optional(comment)) {
				skipping++;
				continue;
			}
			name++;	/* We have it, so ignore */
			break;
		case '}':	/* End optional implementation */
			skipping = 0;
			continue;
		case '?':	/* Optional */
		case '|':	/* Alternative */
			if (unsupported(comment))
				continue;
			name++;	/* We have it, so ignore */
			break;
		case '#':	/* Comment */
		case ';':	/* Skip */
			continue;
		default:
			break;
		}

		/* XXX: Our bug */
		if (bug(pattern, input, lineno))
			continue;

		int comp, exec;
		if (*matches != '(') {
			geterror(matches, &comp, &exec);
			pm = NULL;
			nm = 0;
		} else {
			comp = exec = 0;
			nm = getmatches(matches);
			ATF_REQUIRE((pm = calloc(nm, sizeof(*pm))) != NULL);
		}



		int iflags = getflags(name);
		for (; *name; name++) {
			int flags;
			switch (*name) {
			case 'B':
				flags = REG_BASIC;
				break;
			case 'E':
				flags = REG_EXTENDED;
				break;
			case 'L':
				flags = REG_NOSPEC;
				break;
			default:
				ATF_REQUIRE_MSG(0, "Bad name %c", *name);
				continue;
			}
			int c = regcomp(&re, pattern, flags | iflags);
			ATF_REQUIRE_MSG(c == comp,
			    "regcomp returned %d for pattern %s at line %zu",
			    c, pattern, lineno);
			if (c)
				continue;
			int e = regexec(&re, input, nm, pm, 0);
			ATF_REQUIRE_MSG(e == exec, "Expected error %d,"
			    " got %d at line %zu", exec, e, lineno);
			checkmatches(matches, nm, pm, lineno);
			checkcomment(comment, lineno);
			regfree(&re);
		}
		free(pm);
	}

	fclose(input_file);
}

ATF_TC(basic);
ATF_TC_HEAD(basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests basic functionality");
}
ATF_TC_BODY(basic, tc)
{
	att_test(tc, "basic");
}

ATF_TC(categorization);
ATF_TC_HEAD(categorization, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests implementation categorization");
}
ATF_TC_BODY(categorization, tc)
{
	att_test(tc, "categorization");
}

ATF_TC(nullsubexpr);
ATF_TC_HEAD(nullsubexpr, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests (...)*");
}
ATF_TC_BODY(nullsubexpr, tc)
{
	att_test(tc, "nullsubexpr");
}

ATF_TC(leftassoc);
ATF_TC_HEAD(leftassoc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests left-associative "
	    "implementations");
}
ATF_TC_BODY(leftassoc, tc)
{
#if SKIP_LEFTASSOC
	/* jmmv: I converted the original shell-based tests to C and they
	 * disabled this test in a very unconventional way without giving
	 * any explation.  Mark as broken here, but I don't know why. */
	atf_tc_expect_fail("Reason for breakage unknown");
#endif
	att_test(tc, "leftassoc");
}

ATF_TC(rightassoc);
ATF_TC_HEAD(rightassoc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests right-associative "
	    "implementations");
}
ATF_TC_BODY(rightassoc, tc)
{
#if SKIP_RIGHTASSOC
	/* jmmv: I converted the original shell-based tests to C and they
	 * disabled this test in a very unconventional way without giving
	 * any explation.  Mark as broken here, but I don't know why. */
	atf_tc_expect_fail("Reason for breakage unknown");
#endif
	att_test(tc, "rightassoc");
}

ATF_TC(forcedassoc);
ATF_TC_HEAD(forcedassoc, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests subexpression grouping to "
	    "force association");
}
ATF_TC_BODY(forcedassoc, tc)
{
	att_test(tc, "forcedassoc");
}

ATF_TC(repetition);
ATF_TC_HEAD(repetition, tc)
{
	atf_tc_set_md_var(tc, "descr", "Tests implicit vs. explicit "
	    "repetition");
}
ATF_TC_BODY(repetition, tc)
{
	att_test(tc, "repetition");
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, basic);
	ATF_TP_ADD_TC(tp, categorization);
	ATF_TP_ADD_TC(tp, nullsubexpr);
	ATF_TP_ADD_TC(tp, leftassoc);
	ATF_TP_ADD_TC(tp, rightassoc);
	ATF_TP_ADD_TC(tp, forcedassoc);
	ATF_TP_ADD_TC(tp, repetition);
	return atf_no_error();
}
