/*	$NetBSD: t_strtol.c,v 1.6 2016/06/01 01:12:02 pgoyette Exp $ */

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
__RCSID("$NetBSD: t_strtol.c,v 1.6 2016/06/01 01:12:02 pgoyette Exp $");

#include <atf-c.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct test {
	const char	*str;
	int64_t		 res;
	int		 base;
	const char	*end;
};

static void	check(struct test *, long int, long long int, char *);

static void
check(struct test *t, long int li, long long int lli, char *end)
{

	if (li != -1 && li != t->res)
		atf_tc_fail_nonfatal("strtol(%s, &end, %d) failed "
		    "(rv = %ld)", t->str, t->base, li);

	if (lli != -1 && lli != t->res)
		atf_tc_fail_nonfatal("strtoll(%s, NULL, %d) failed "
		    "(rv = %lld)", t->str, t->base, lli);

	if ((t->end != NULL && strcmp(t->end, end) != 0) ||
	    (t->end == NULL && *end != '\0'))
		atf_tc_fail_nonfatal("invalid end pointer ('%s') from "
		    "strtol(%s, &end, %d)", end, t->str, t->base);
}

ATF_TC(strtol_base);
ATF_TC_HEAD(strtol_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtol(3) with different bases");
}

ATF_TC_BODY(strtol_base, tc)
{
	struct test t[] = {
		{ "123456789",			 123456789,  0, NULL	},
		{ "111010110111100110100010101", 123456789,  2, NULL	},
		{ "22121022020212200",		 123456789,  3, NULL	},
		{ "13112330310111",		 123456789,  4, NULL	},
		{ "223101104124",		 123456789,  5, NULL	},
		{ "20130035113",		 123456789,  6, NULL	},
		{ "3026236221",			 123456789,  7, NULL	},
		{ "726746425",			 123456789,  8, NULL	},
		{ "277266780",			 123456789,  9, NULL	},
		{ "123456789",			 123456789, 10, NULL	},
		{ "63762A05",			 123456789, 11, NULL	},
		{ "35418A99",			 123456789, 12, NULL	},
		{ "1C767471",			 123456789, 13, NULL	},
		{ "12579781",			 123456789, 14, NULL	},
		{ "AC89BC9",			 123456789, 15, NULL	},
		{ "75BCD15",			 123456789, 16, NULL	},
		{ "1234567",			    342391,  8, NULL	},
		{ "01234567",			    342391,  0, NULL	},
		{ "0123456789",			 123456789, 10, NULL	},
		{ "0x75bcd15",		         123456789,  0, NULL	},
	};

	long long int lli;
	long int li;
	char *end;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		li = strtol(t[i].str, &end, t[i].base);
		lli = strtoll(t[i].str, NULL, t[i].base);

		check(&t[i], li, lli, end);
	}
}

ATF_TC(strtol_case);
ATF_TC_HEAD(strtol_case, tc)
{
	atf_tc_set_md_var(tc, "descr", "Case insensitivity with strtol(3)");
}

ATF_TC_BODY(strtol_case, tc)
{
	struct test t[] = {
		{ "abcd",	0xabcd, 16, NULL	},
		{ "     dcba",	0xdcba, 16, NULL	},
		{ "abcd dcba",	0xabcd, 16, " dcba"	},
		{ "abc0x123",	0xabc0, 16, "x123"	},
		{ "abcd\0x123",	0xabcd, 16, "\0x123"	},
		{ "ABCD",	0xabcd, 16, NULL	},
		{ "aBcD",	0xabcd, 16, NULL	},
		{ "0xABCD",	0xabcd, 16, NULL	},
		{ "0xABCDX",	0xabcd, 16, "X"		},
	};

	long long int lli;
	long int li;
	char *end;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		li = strtol(t[i].str, &end, t[i].base);
		lli = strtoll(t[i].str, NULL, t[i].base);

		check(&t[i], li, lli, end);
	}
}

ATF_TC(strtol_range);
ATF_TC_HEAD(strtol_range, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ERANGE from strtol(3)");
}

ATF_TC_BODY(strtol_range, tc)
{

#if LONG_MAX == 0x7fffffff	/* XXX: Is this portable? */

	struct test t[] = {
		{ "20000000000", 2147483647, 8, NULL },
		{ "2147483648",  2147483647, 10, NULL },
		{ "80000000",	 2147483647, 16, NULL },
	};
#else
	struct test t[] = {
		{ "1000000000000000000000", 9223372036854775807, 8, NULL },
		{ "9223372036854775808",    9223372036854775807, 10, NULL },
		{ "8000000000000000",       9223372036854775807, 16, NULL },
	};
#endif

	long int li;
	char *end;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		errno = 0;
		li = strtol(t[i].str, &end, t[i].base);

		if (errno != ERANGE)
			atf_tc_fail("strtol(3) did not catch ERANGE");

		check(&t[i], li, -1, end);
	}
}

ATF_TC(strtol_signed);
ATF_TC_HEAD(strtol_signed, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of strtol(3)");
}

ATF_TC_BODY(strtol_signed, tc)
{
	struct test t[] = {
		{ "1",		 1, 0, NULL	},
		{ " 2",		 2, 0, NULL	},
		{ "  3",	 3, 0, NULL	},
		{ " -3",	-3, 0, NULL	},
		{ "--1",	 0, 0, "--1"	},
		{ "+-2",	 0, 0, "+-2"	},
		{ "++3",	 0, 0, "++3"	},
		{ "+9",		 9, 0, NULL	},
		{ "+123",      123, 0, NULL	},
		{ "-1 3",       -1, 0, " 3"	},
		{ "-1.3",       -1, 0, ".3"	},
		{ "-  3",        0, 0, "-  3"	},
		{ "+33.",       33, 0, "."	},
		{ "30x0",       30, 0, "x0"	},
	};

	long long int lli;
	long int li;
	char *end;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		li = strtol(t[i].str, &end, t[i].base);
		lli = strtoll(t[i].str, NULL, t[i].base);

		check(&t[i], li, lli, end);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strtol_base);
	ATF_TP_ADD_TC(tp, strtol_case);
	ATF_TP_ADD_TC(tp, strtol_range);
	ATF_TP_ADD_TC(tp, strtol_signed);

	return atf_no_error();
}
