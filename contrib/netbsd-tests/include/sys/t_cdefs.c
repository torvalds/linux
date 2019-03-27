/* $NetBSD: t_cdefs.c,v 1.4 2016/03/16 07:21:36 mrg Exp $ */

/*-
 * Copyright (c) 2012 The NetBSD Foundation, Inc.
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

#include <sys/cdefs.h>
__COPYRIGHT("@(#) Copyright (c) 2008\
 The NetBSD Foundation, inc. All rights reserved.");
__RCSID("$NetBSD: t_cdefs.c,v 1.4 2016/03/16 07:21:36 mrg Exp $");

#include <atf-c.h>
#include <sys/types.h>
#include <limits.h>
#include <stdint.h>

static const struct {
	const char *name;
	intmax_t min;
	intmax_t max;
} s[] = {
	{ "signed char", SCHAR_MIN, SCHAR_MAX },
	{ "signed short", SHRT_MIN, SHRT_MAX },
	{ "signed int", INT_MIN, INT_MAX },
	{ "signed long", LONG_MIN, LONG_MAX },
	{ "signed long long", LLONG_MIN, LLONG_MAX },
};

static const struct {
	const char *name;
	uintmax_t min;
	uintmax_t max;
} u[] = {
	{ "unsigned char", 0, UCHAR_MAX },
	{ "unsigned short", 0, USHRT_MAX },
	{ "unsigned int", 0, UINT_MAX },
	{ "unsigned long", 0, ULONG_MAX },
	{ "unsigned long long", 0, ULLONG_MAX },
};

ATF_TC(stypeminmax);
ATF_TC_HEAD(stypeminmax, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks signed type min/max macros");
}


ATF_TC_BODY(stypeminmax, tc)
{
#define CHECK(a, b) ATF_REQUIRE(__type_min(a) == s[b].min); \
    ATF_REQUIRE(__type_max(a) == s[b].max)

	CHECK(signed char, 0);
	CHECK(signed short, 1);
	CHECK(signed int, 2);
	CHECK(signed long, 3);
	CHECK(signed long long, 4);
#undef CHECK
}

ATF_TC(utypeminmax);
ATF_TC_HEAD(utypeminmax, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks unsigned type min/max macros");
}

ATF_TC_BODY(utypeminmax, tc)
{
#define CHECK(a, b) ATF_REQUIRE(__type_min(a) == u[b].min); \
    ATF_REQUIRE(__type_max(a) == u[b].max)

	CHECK(unsigned char, 0);
	CHECK(unsigned short, 1);
	CHECK(unsigned int, 2);
	CHECK(unsigned long, 3);
	CHECK(unsigned long long, 4);
#undef CHECK
}

ATF_TC(sissigned);
ATF_TC_HEAD(sissigned, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks issigned macro for signed");
}

ATF_TC_BODY(sissigned, tc)
{
#define CHECK(a) ATF_REQUIRE(__type_is_signed(a) == 1)

	CHECK(signed char);
	CHECK(signed short);
	CHECK(signed int);
	CHECK(signed long);
	CHECK(signed long long);
#undef CHECK
}

ATF_TC(uissigned);
ATF_TC_HEAD(uissigned, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks issigned macro for unsigned");
}

ATF_TC_BODY(uissigned, tc)
{
#define CHECK(a) ATF_REQUIRE(__type_is_signed(a) == 0)

	CHECK(unsigned char);
	CHECK(unsigned short);
	CHECK(unsigned int);
	CHECK(unsigned long);
	CHECK(unsigned long long);
#undef CHECK
}

ATF_TC(utypemask);
ATF_TC_HEAD(utypemask, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks type mask macro for unsigned");
}

ATF_TC_BODY(utypemask, tc)
{
#define CHECK(a, b) ATF_REQUIRE(__type_mask(a) == b)

	CHECK(unsigned char,      0xffffffffffffff00ULL);
	CHECK(unsigned short,     0xffffffffffff0000ULL);
	CHECK(unsigned int,       0xffffffff00000000ULL);
	CHECK(unsigned long long, 0x0000000000000000ULL);
#undef CHECK
}

ATF_TC(stypemask);
ATF_TC_HEAD(stypemask, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks type mask macro for signed");
}

ATF_TC_BODY(stypemask, tc)
{
#define CHECK(a, b) ATF_REQUIRE(__type_mask(a) == b)

	CHECK(signed char,      0xffffffffffffff00LL);
	CHECK(signed short,     0xffffffffffff0000LL);
	CHECK(signed int,       0xffffffff00000000LL);
	CHECK(signed long long, 0x0000000000000000LL);
#undef CHECK
}

ATF_TC(stypefit);
ATF_TC_HEAD(stypefit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks typefit macro for signed");
}

ATF_TC_BODY(stypefit, tc)
{
#define CHECK(a, b, c) ATF_REQUIRE(__type_fit(a, b) == c)

	CHECK(signed char, -1, 1);
	CHECK(signed char, 1, 1);
	CHECK(signed char, 0x7f, 1);
	CHECK(signed char, 0x80, 0);
	CHECK(signed char, 0xff, 0);
	CHECK(signed char, 0x1ff, 0);

	CHECK(signed short, -1, 1);
	CHECK(signed short, 1, 1);
	CHECK(signed short, 0x7fff, 1);
	CHECK(signed short, 0x8000, 0);
	CHECK(signed short, 0xffff, 0);
	CHECK(signed short, 0x1ffff, 0);

	CHECK(signed int, -1, 1);
	CHECK(signed int, 1, 1);
	CHECK(signed int, 0x7fffffff, 1);
	CHECK(signed int, 0x80000000, 0);
	CHECK(signed int, 0xffffffff, 0);
	CHECK(signed int, 0x1ffffffffLL, 0);

	CHECK(signed long long, -1, 1);
	CHECK(signed long long, 1, 1);
	CHECK(signed long long, 0x7fffffffffffffffLL, 1);
	CHECK(signed long long, 0x8000000000000000LL, 0);
	CHECK(signed long long, 0xffffffffffffffffLL, 0);

#undef CHECK
}

ATF_TC(utypefit);
ATF_TC_HEAD(utypefit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Checks typefit macro for unsigned");
}

ATF_TC_BODY(utypefit, tc)
{
#define CHECK(a, b, c) ATF_REQUIRE(__type_fit(a, b) == c)

	CHECK(unsigned char, -1, 0);
	CHECK(unsigned char, 1, 1);
	CHECK(unsigned char, 0x7f, 1);
	CHECK(unsigned char, 0x80, 1);
	CHECK(unsigned char, 0xff, 1);
	CHECK(unsigned char, 0x1ff, 0);

	CHECK(unsigned short, -1, 0);
	CHECK(unsigned short, 1, 1);
	CHECK(unsigned short, 0x7fff, 1);
	CHECK(unsigned short, 0x8000, 1);
	CHECK(unsigned short, 0xffff, 1);
	CHECK(unsigned short, 0x1ffff, 0);

	CHECK(unsigned int, -1, 0);
	CHECK(unsigned int, 1, 1);
	CHECK(unsigned int, 0x7fffffff, 1);
	CHECK(unsigned int, 0x80000000, 1);
	CHECK(unsigned int, 0xffffffff, 1);
	CHECK(unsigned int, 0x1ffffffffLL, 0);

	CHECK(unsigned long long, -1, 0);
	CHECK(unsigned long long, 1, 1);
	CHECK(unsigned long long, 0x7fffffffffffffffULL, 1);
	CHECK(unsigned long long, 0x8000000000000000ULL, 1);
	CHECK(unsigned long long, 0xffffffffffffffffULL, 1);

#undef CHECK
}

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, stypeminmax);
	ATF_TP_ADD_TC(tp, utypeminmax);
	ATF_TP_ADD_TC(tp, sissigned);
	ATF_TP_ADD_TC(tp, uissigned);
	ATF_TP_ADD_TC(tp, stypemask);
	ATF_TP_ADD_TC(tp, utypemask);
	ATF_TP_ADD_TC(tp, stypefit);
	ATF_TP_ADD_TC(tp, utypefit);

	return atf_no_error();
}
