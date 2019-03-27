/*	$NetBSD: t_strtoi.c,v 1.1 2015/05/01 14:17:56 christos Exp $	*/

/*-
 * Copyright (c) 2015 The NetBSD Foundation, Inc.
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

/*
 * Created by Kamil Rytarowski, vesed on ID:
 * NetBSD: t_strtol.c,v 1.5 2011/06/14 02:45:58 jruoho Exp
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_strtoi.c,v 1.1 2015/05/01 14:17:56 christos Exp $");

#include <atf-c.h>
#include <errno.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

struct test {
	const char	*str;
	intmax_t	 res;
	int		 base;
	const char	*end;
	intmax_t	 lo;
	intmax_t	 hi;
	int		 rstatus;
};

static void	check(struct test *, intmax_t, char *, int);

static void
check(struct test *t, intmax_t rv, char *end, int rstatus)
{

	if (rv != t->res)
		atf_tc_fail_nonfatal("strtoi(%s, &end, %d, %jd, %jd, &rstatus)"
		    " failed (rv = %jd)", t->str, t->base, t->lo, t->hi, rv);

	if (rstatus != t->rstatus)
		atf_tc_fail_nonfatal("strtoi(%s, &end, %d, %jd, %jd, &rstatus)"
		    " failed (rstatus: %d ('%s'))",
		    t->str, t->base, t->lo, t->hi, rstatus, strerror(rstatus));

	if ((t->end != NULL && strcmp(t->end, end) != 0) ||
	    (t->end == NULL && *end != '\0'))
		atf_tc_fail_nonfatal("invalid end pointer ('%s') from "
		    "strtoi(%s, &end, %d, %jd, %jd, &rstatus)",
		     end, t->str, t->base, t->lo, t->hi);
}

ATF_TC(strtoi_base);
ATF_TC_HEAD(strtoi_base, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test strtoi(3) with different bases");
}

ATF_TC_BODY(strtoi_base, tc)
{
	struct test t[] = {
		{ "123456789",                  123456789,	0,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "111010110111100110100010101",123456789,	2,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "22121022020212200",          123456789,	3,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "13112330310111",	        123456789,	4,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "223101104124",               123456789,	5,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "20130035113",                123456789,	6,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "3026236221",	                123456789,	7,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "726746425",                  123456789,	8,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "277266780",                  123456789,	9,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "123456789",                  123456789,	10,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "63762A05",                   123456789,	11,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "35418A99",                   123456789,	12,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "1C767471",                   123456789,	13,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "12579781",                   123456789,	14,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "AC89BC9",                    123456789,	15,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "75BCD15",                    123456789,	16,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "1234567",                       342391,	8,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "01234567",                      342391,	0,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "0123456789",                 123456789,	10,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "0x75bcd15",                  123456789,	0,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
	};

	intmax_t rv;
	char *end;
	int e;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		errno = 0;
		rv = strtoi(t[i].str, &end, t[i].base, t[i].lo, t[i].hi, &e);

		if (errno != 0)
			atf_tc_fail("strtoi(3) changed errno to %d ('%s')",
			            e, strerror(e));

		check(&t[i], rv, end, e);
	}
}

ATF_TC(strtoi_case);
ATF_TC_HEAD(strtoi_case, tc)
{
	atf_tc_set_md_var(tc, "descr", "Case insensitivity with strtoi(3)");
}

ATF_TC_BODY(strtoi_case, tc)
{
	struct test t[] = {
		{ "abcd",	0xabcd,	16,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "     dcba",	0xdcba,	16,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "abcd dcba",	0xabcd,	16,	" dcba",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP	},
		{ "abc0x123",	0xabc0, 16,	"x123",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP	},
		{ "abcd\0x123",	0xabcd, 16,	"\0x123",
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "ABCD",	0xabcd, 16,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "aBcD",	0xabcd, 16,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "0xABCD",	0xabcd, 16,	NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0	},
		{ "0xABCDX",	0xabcd, 16,	"X",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP},
	};

	intmax_t rv;
	char *end;
	int e;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		errno = 0;
		rv = strtoi(t[i].str, &end, t[i].base, t[i].lo, t[i].hi, &e);

		if (errno != 0)
			atf_tc_fail("strtoi(3) changed errno to %d ('%s')",
			            e, strerror(e));

		check(&t[i], rv, end, e);
	}
}

ATF_TC(strtoi_range);
ATF_TC_HEAD(strtoi_range, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ERANGE from strtoi(3)");
}

ATF_TC_BODY(strtoi_range, tc)
{
	struct test t[] = {
#if INTMAX_MAX == 0x7fffffffffffffff
		{ "1000000000000000000000", INTMAX_MAX, 8, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	ERANGE },
		{ "9223372036854775808",    INTMAX_MAX, 10, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	ERANGE },
		{ "8000000000000000",       INTMAX_MAX, 16, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	ERANGE },
#else
#error extend this test to your platform!
#endif
		{ "10",	1,	10,	NULL,
		  -1,	1,	ERANGE	},
		{ "10",	11,	10,	NULL,
		  11,	20,	ERANGE	},
	};

	intmax_t rv;
	char *end;
	int e;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		errno = 0;
		rv = strtoi(t[i].str, &end, t[i].base, t[i].lo, t[i].hi, &e);

		if (errno != 0)
			atf_tc_fail("strtoi(3) changed errno to %d ('%s')",
			            e, strerror(e));

		check(&t[i], rv, end, e);
	}
}

ATF_TC(strtoi_signed);
ATF_TC_HEAD(strtoi_signed, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of strtoi(3)");
}

ATF_TC_BODY(strtoi_signed, tc)
{
	struct test t[] = {
		{ "1",		 1, 0, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0 },
		{ " 2",		 2, 0, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0 },
		{ "  3",	 3, 0, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0 },
		{ " -3",	-3, 0, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0 },
		{ "--1",	 0, 0, "--1",
		  INTMAX_MIN,	INTMAX_MAX,	ECANCELED },
		{ "+-2",	 0, 0, "+-2",
		  INTMAX_MIN,	INTMAX_MAX,	ECANCELED },
		{ "++3",	 0, 0, "++3",
		  INTMAX_MIN,	INTMAX_MAX,	ECANCELED },
		{ "+9",		 9, 0, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0 },
		{ "+123",      123, 0, NULL,
		  INTMAX_MIN,	INTMAX_MAX,	0 },
		{ "-1 3",       -1, 0, " 3",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP },
		{ "-1.3",       -1, 0, ".3",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP },
		{ "-  3",        0, 0, "-  3",
		  INTMAX_MIN,	INTMAX_MAX,	ECANCELED },
		{ "+33.",       33, 0, ".",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP },
		{ "30x0",       30, 0, "x0",
		  INTMAX_MIN,	INTMAX_MAX,	ENOTSUP },
	};

	intmax_t rv;
	char *end;
	int e;
	size_t i;

	for (i = 0; i < __arraycount(t); i++) {

		errno = 0;
		rv = strtoi(t[i].str, &end, t[i].base, t[i].lo, t[i].hi, &e);

		if (errno != 0)
			atf_tc_fail("strtoi(3) changed errno to %d ('%s')",
			            e, strerror(e));

		check(&t[i], rv, end, e);
	}
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, strtoi_base);
	ATF_TP_ADD_TC(tp, strtoi_case);
	ATF_TP_ADD_TC(tp, strtoi_range);
	ATF_TP_ADD_TC(tp, strtoi_signed);

	return atf_no_error();
}
