/* $NetBSD: t_abs.c,v 1.3 2014/03/01 22:38:13 joerg Exp $ */

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
__RCSID("$NetBSD: t_abs.c,v 1.3 2014/03/01 22:38:13 joerg Exp $");

#include <atf-c.h>
#include <inttypes.h>
#include <limits.h>
#include <stdlib.h>

ATF_TC(abs_basic);
ATF_TC_HEAD(abs_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that abs(3) works");
}

ATF_TC_BODY(abs_basic, tc)
{
	static const struct {
		int val;
		int res;
	} table[] = {
		{ 0,		0		},
		{ +0,		0		},
		{ -0,		0		},
		{ -0x1010,	0x1010		},
		{ INT_MAX,	INT_MAX		},
		{ -INT_MAX,	INT_MAX		},
	};

	for (size_t i = 0; i < __arraycount(table); i++)
		ATF_CHECK(abs(table[i].val) == table[i].res);
}

ATF_TC(imaxabs_basic);
ATF_TC_HEAD(imaxabs_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that imaxabs(3) works");
}

ATF_TC_BODY(imaxabs_basic, tc)
{
	static const struct {
		intmax_t val;
		intmax_t res;
	} table[] = {
		{ 0,		0		},
		{ INT_MAX,	INT_MAX		},
		{ -INT_MAX,	INT_MAX		},
		{ LONG_MAX,	LONG_MAX	},
		{ -LONG_MAX,	LONG_MAX	},
		{ LLONG_MAX,	LLONG_MAX	},
		{ -LLONG_MAX,	LLONG_MAX	},
		{ INT_MAX,	INT_MAX		},
		{ -INT_MAX,	INT_MAX		},
	};

	for (size_t i = 0; i < __arraycount(table); i++)
		ATF_CHECK(imaxabs(table[i].val) == table[i].res);
}

ATF_TC(labs_basic);
ATF_TC_HEAD(labs_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that labs(3) works");
}

ATF_TC_BODY(labs_basic, tc)
{
	static const struct {
		long val;
		long res;
	} table[] = {
		{ 0,		0		},
		{ +0,		0		},
		{ -0,		0		},
		{ -1,		1		},
		{ LONG_MAX,	LONG_MAX	},
		{ -LONG_MAX,	LONG_MAX	},
		{ INT_MAX,	INT_MAX		},
		{ -INT_MAX,	INT_MAX		},
	};

	for (size_t i = 0; i < __arraycount(table); i++)
		ATF_CHECK(labs(table[i].val) == table[i].res);
}

ATF_TC(llabs_basic);
ATF_TC_HEAD(llabs_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test that llabs(3) works");
}

ATF_TC_BODY(llabs_basic, tc)
{
	static const struct {
		long long val;
		long long res;
	} table[] = {
		{ 0,		0		},
		{ +0,		0		},
		{ -0,		0		},
		{ -1,		1		},
		{ INT_MAX,	INT_MAX		},
		{ -INT_MAX,	INT_MAX		},
		{ LONG_MAX,	LONG_MAX	},
		{ -LONG_MAX,	LONG_MAX	},
		{ LLONG_MAX,	LLONG_MAX	},
		{ -LLONG_MAX,	LLONG_MAX	},
		{ -0x100000000LL,	0x100000000LL	},
	};

	for (size_t i = 0; i < __arraycount(table); i++)
		ATF_CHECK(llabs(table[i].val) == table[i].res);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, abs_basic);
	ATF_TP_ADD_TC(tp, imaxabs_basic);
	ATF_TP_ADD_TC(tp, labs_basic);
	ATF_TP_ADD_TC(tp, llabs_basic);

	return atf_no_error();
}
