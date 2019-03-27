/*	$NetBSD: t_bitops.c,v 1.19 2015/03/21 05:50:19 isaki Exp $ */

/*-
 * Copyright (c) 2011, 2012 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Christos Zoulas and Jukka Ruohonen.
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
__RCSID("$NetBSD: t_bitops.c,v 1.19 2015/03/21 05:50:19 isaki Exp $");

#include <atf-c.h>

#include <sys/cdefs.h>
#include <sys/bitops.h>

#include <math.h>
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>

static const struct {
	uint32_t	val;
	int		ffs;
	int		fls;
} bits[] = {

	{ 0x00, 0, 0 }, { 0x01, 1, 1 },	{ 0x02, 2, 2 },	{ 0x03, 1, 2 },
	{ 0x04, 3, 3 }, { 0x05, 1, 3 },	{ 0x06, 2, 3 },	{ 0x07, 1, 3 },
	{ 0x08, 4, 4 }, { 0x09, 1, 4 },	{ 0x0A, 2, 4 },	{ 0x0B, 1, 4 },
	{ 0x0C, 3, 4 }, { 0x0D, 1, 4 },	{ 0x0E, 2, 4 },	{ 0x0F, 1, 4 },

	{ 0x10, 5, 5 },	{ 0x11, 1, 5 },	{ 0x12, 2, 5 },	{ 0x13, 1, 5 },
	{ 0x14, 3, 5 },	{ 0x15, 1, 5 },	{ 0x16, 2, 5 },	{ 0x17, 1, 5 },
	{ 0x18, 4, 5 },	{ 0x19, 1, 5 },	{ 0x1A, 2, 5 },	{ 0x1B, 1, 5 },
	{ 0x1C, 3, 5 },	{ 0x1D, 1, 5 },	{ 0x1E, 2, 5 },	{ 0x1F, 1, 5 },

	{ 0xF0, 5, 8 },	{ 0xF1, 1, 8 },	{ 0xF2, 2, 8 },	{ 0xF3, 1, 8 },
	{ 0xF4, 3, 8 },	{ 0xF5, 1, 8 },	{ 0xF6, 2, 8 },	{ 0xF7, 1, 8 },
	{ 0xF8, 4, 8 },	{ 0xF9, 1, 8 },	{ 0xFA, 2, 8 },	{ 0xFB, 1, 8 },
	{ 0xFC, 3, 8 },	{ 0xFD, 1, 8 },	{ 0xFE, 2, 8 },	{ 0xFF, 1, 8 },

};

ATF_TC(bitmap_basic);
ATF_TC_HEAD(bitmap_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of __BITMAP_*");
}

ATF_TC_BODY(bitmap_basic, tc)
{
	__BITMAP_TYPE(, uint32_t, 65536) bm;
	__BITMAP_ZERO(&bm);

	ATF_REQUIRE(__BITMAP_SIZE(uint32_t, 65536) == 2048);

	ATF_REQUIRE(__BITMAP_SHIFT(uint32_t) == 5);

	ATF_REQUIRE(__BITMAP_MASK(uint32_t) == 31);

	for (size_t i = 0; i < 65536; i += 2)
		__BITMAP_SET(i, &bm);

	for (size_t i = 0; i < 2048; i++)
		ATF_REQUIRE(bm._b[i] == 0x55555555);

	for (size_t i = 0; i < 65536; i++)
		if (i & 1)
			ATF_REQUIRE(!__BITMAP_ISSET(i, &bm));
		else {
			ATF_REQUIRE(__BITMAP_ISSET(i, &bm));
			__BITMAP_CLR(i, &bm);
		}

	for (size_t i = 0; i < 65536; i += 2)
		ATF_REQUIRE(!__BITMAP_ISSET(i, &bm));
}

ATF_TC(fast_divide32);
ATF_TC_HEAD(fast_divide32, tc)
{
	atf_tc_set_md_var(tc, "descr", "A basic test of fast_divide32(3)");
}

ATF_TC_BODY(fast_divide32, tc)
{
	uint32_t a, b, q, r, m;
	uint8_t i, s1, s2;

	a = 0xFFFF;
	b = 0x000F;

	fast_divide32_prepare(b, &m, &s1, &s2);

	q = fast_divide32(a, b, m, s1, s2);
	r = fast_remainder32(a, b, m, s1, s2);

	ATF_REQUIRE(q == 0x1111 && r == 0);

	for (i = 1; i < __arraycount(bits); i++) {

		a = bits[i].val;
		b = bits[i].ffs;

		fast_divide32_prepare(b, &m, &s1, &s2);

		q = fast_divide32(a, b, m, s1, s2);
		r = fast_remainder32(a, b, m, s1, s2);

		ATF_REQUIRE(q == a / b);
		ATF_REQUIRE(r == a % b);
	}
}

ATF_TC(ffsfls);
ATF_TC_HEAD(ffsfls, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ffs32(3)-family for correctness");
}

ATF_TC_BODY(ffsfls, tc)
{
	uint8_t i;

	ATF_REQUIRE(ffs32(0) == 0x00);
	ATF_REQUIRE(fls32(0) == 0x00);
	ATF_REQUIRE(ffs64(0) == 0x00);
	ATF_REQUIRE(fls64(0) == 0x00);

	ATF_REQUIRE(ffs32(UINT32_MAX) == 0x01);
	ATF_REQUIRE(fls32(UINT32_MAX) == 0x20);
	ATF_REQUIRE(ffs64(UINT64_MAX) == 0x01);
	ATF_REQUIRE(fls64(UINT64_MAX) == 0x40);

	for (i = 1; i < __arraycount(bits); i++) {

		ATF_REQUIRE(ffs32(bits[i].val) == bits[i].ffs);
		ATF_REQUIRE(fls32(bits[i].val) == bits[i].fls);
		ATF_REQUIRE(ffs64(bits[i].val) == bits[i].ffs);
		ATF_REQUIRE(fls64(bits[i].val) == bits[i].fls);

		ATF_REQUIRE(ffs32(bits[i].val << 1) == bits[i].ffs + 1);
		ATF_REQUIRE(fls32(bits[i].val << 1) == bits[i].fls + 1);
		ATF_REQUIRE(ffs64(bits[i].val << 1) == bits[i].ffs + 1);
		ATF_REQUIRE(fls64(bits[i].val << 1) == bits[i].fls + 1);

		ATF_REQUIRE(ffs32(bits[i].val << 9) == bits[i].ffs + 9);
		ATF_REQUIRE(fls32(bits[i].val << 9) == bits[i].fls + 9);
		ATF_REQUIRE(ffs64(bits[i].val << 9) == bits[i].ffs + 9);
		ATF_REQUIRE(fls64(bits[i].val << 9) == bits[i].fls + 9);
	}
}

ATF_TC(ilog2_32bit);
ATF_TC_HEAD(ilog2_32bit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ilog2(3) for 32bit variable");
}

ATF_TC_BODY(ilog2_32bit, tc)
{
	int i;
	uint32_t x;

	for (i = 0; i < 32; i++) {
		x = 1 << i;
		ATF_REQUIRE(ilog2(x) == i);
	}
}

ATF_TC(ilog2_64bit);
ATF_TC_HEAD(ilog2_64bit, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ilog2(3) for 64bit variable");
}

ATF_TC_BODY(ilog2_64bit, tc)
{
	int i;
	uint64_t x;

	for (i = 0; i < 64; i++) {
		x = ((uint64_t)1) << i;
		ATF_REQUIRE(ilog2(x) == i);
	}
}

ATF_TC(ilog2_const);
ATF_TC_HEAD(ilog2_const, tc)
{
	atf_tc_set_md_var(tc, "descr", "Test ilog2(3) for constant");
}

ATF_TC_BODY(ilog2_const, tc)
{
	/*
	 * These inlines test __builtin_constant_p() part of ilog2()
	 * at compile time, so don't change it to loop.  PR lib/49745
	 */
	ATF_REQUIRE(ilog2(0x0000000000000001ULL) == 0);
	ATF_REQUIRE(ilog2(0x0000000000000002ULL) == 1);
	ATF_REQUIRE(ilog2(0x0000000000000004ULL) == 2);
	ATF_REQUIRE(ilog2(0x0000000000000008ULL) == 3);
	ATF_REQUIRE(ilog2(0x0000000000000010ULL) == 4);
	ATF_REQUIRE(ilog2(0x0000000000000020ULL) == 5);
	ATF_REQUIRE(ilog2(0x0000000000000040ULL) == 6);
	ATF_REQUIRE(ilog2(0x0000000000000080ULL) == 7);
	ATF_REQUIRE(ilog2(0x0000000000000100ULL) == 8);
	ATF_REQUIRE(ilog2(0x0000000000000200ULL) == 9);
	ATF_REQUIRE(ilog2(0x0000000000000400ULL) == 10);
	ATF_REQUIRE(ilog2(0x0000000000000800ULL) == 11);
	ATF_REQUIRE(ilog2(0x0000000000001000ULL) == 12);
	ATF_REQUIRE(ilog2(0x0000000000002000ULL) == 13);
	ATF_REQUIRE(ilog2(0x0000000000004000ULL) == 14);
	ATF_REQUIRE(ilog2(0x0000000000008000ULL) == 15);
	ATF_REQUIRE(ilog2(0x0000000000010000ULL) == 16);
	ATF_REQUIRE(ilog2(0x0000000000020000ULL) == 17);
	ATF_REQUIRE(ilog2(0x0000000000040000ULL) == 18);
	ATF_REQUIRE(ilog2(0x0000000000080000ULL) == 19);
	ATF_REQUIRE(ilog2(0x0000000000100000ULL) == 20);
	ATF_REQUIRE(ilog2(0x0000000000200000ULL) == 21);
	ATF_REQUIRE(ilog2(0x0000000000400000ULL) == 22);
	ATF_REQUIRE(ilog2(0x0000000000800000ULL) == 23);
	ATF_REQUIRE(ilog2(0x0000000001000000ULL) == 24);
	ATF_REQUIRE(ilog2(0x0000000002000000ULL) == 25);
	ATF_REQUIRE(ilog2(0x0000000004000000ULL) == 26);
	ATF_REQUIRE(ilog2(0x0000000008000000ULL) == 27);
	ATF_REQUIRE(ilog2(0x0000000010000000ULL) == 28);
	ATF_REQUIRE(ilog2(0x0000000020000000ULL) == 29);
	ATF_REQUIRE(ilog2(0x0000000040000000ULL) == 30);
	ATF_REQUIRE(ilog2(0x0000000080000000ULL) == 31);
	ATF_REQUIRE(ilog2(0x0000000100000000ULL) == 32);
	ATF_REQUIRE(ilog2(0x0000000200000000ULL) == 33);
	ATF_REQUIRE(ilog2(0x0000000400000000ULL) == 34);
	ATF_REQUIRE(ilog2(0x0000000800000000ULL) == 35);
	ATF_REQUIRE(ilog2(0x0000001000000000ULL) == 36);
	ATF_REQUIRE(ilog2(0x0000002000000000ULL) == 37);
	ATF_REQUIRE(ilog2(0x0000004000000000ULL) == 38);
	ATF_REQUIRE(ilog2(0x0000008000000000ULL) == 39);
	ATF_REQUIRE(ilog2(0x0000010000000000ULL) == 40);
	ATF_REQUIRE(ilog2(0x0000020000000000ULL) == 41);
	ATF_REQUIRE(ilog2(0x0000040000000000ULL) == 42);
	ATF_REQUIRE(ilog2(0x0000080000000000ULL) == 43);
	ATF_REQUIRE(ilog2(0x0000100000000000ULL) == 44);
	ATF_REQUIRE(ilog2(0x0000200000000000ULL) == 45);
	ATF_REQUIRE(ilog2(0x0000400000000000ULL) == 46);
	ATF_REQUIRE(ilog2(0x0000800000000000ULL) == 47);
	ATF_REQUIRE(ilog2(0x0001000000000000ULL) == 48);
	ATF_REQUIRE(ilog2(0x0002000000000000ULL) == 49);
	ATF_REQUIRE(ilog2(0x0004000000000000ULL) == 50);
	ATF_REQUIRE(ilog2(0x0008000000000000ULL) == 51);
	ATF_REQUIRE(ilog2(0x0010000000000000ULL) == 52);
	ATF_REQUIRE(ilog2(0x0020000000000000ULL) == 53);
	ATF_REQUIRE(ilog2(0x0040000000000000ULL) == 54);
	ATF_REQUIRE(ilog2(0x0080000000000000ULL) == 55);
	ATF_REQUIRE(ilog2(0x0100000000000000ULL) == 56);
	ATF_REQUIRE(ilog2(0x0200000000000000ULL) == 57);
	ATF_REQUIRE(ilog2(0x0400000000000000ULL) == 58);
	ATF_REQUIRE(ilog2(0x0800000000000000ULL) == 59);
	ATF_REQUIRE(ilog2(0x1000000000000000ULL) == 60);
	ATF_REQUIRE(ilog2(0x2000000000000000ULL) == 61);
	ATF_REQUIRE(ilog2(0x4000000000000000ULL) == 62);
	ATF_REQUIRE(ilog2(0x8000000000000000ULL) == 63);

	ATF_REQUIRE(ilog2(0x0000000000000003ULL) == 1);
	ATF_REQUIRE(ilog2(0x0000000000000007ULL) == 2);
	ATF_REQUIRE(ilog2(0x000000000000000fULL) == 3);
	ATF_REQUIRE(ilog2(0x000000000000001fULL) == 4);
	ATF_REQUIRE(ilog2(0x000000000000003fULL) == 5);
	ATF_REQUIRE(ilog2(0x000000000000007fULL) == 6);
	ATF_REQUIRE(ilog2(0x00000000000000ffULL) == 7);
	ATF_REQUIRE(ilog2(0x00000000000001ffULL) == 8);
	ATF_REQUIRE(ilog2(0x00000000000003ffULL) == 9);
	ATF_REQUIRE(ilog2(0x00000000000007ffULL) == 10);
	ATF_REQUIRE(ilog2(0x0000000000000fffULL) == 11);
	ATF_REQUIRE(ilog2(0x0000000000001fffULL) == 12);
	ATF_REQUIRE(ilog2(0x0000000000003fffULL) == 13);
	ATF_REQUIRE(ilog2(0x0000000000007fffULL) == 14);
	ATF_REQUIRE(ilog2(0x000000000000ffffULL) == 15);
	ATF_REQUIRE(ilog2(0x000000000001ffffULL) == 16);
	ATF_REQUIRE(ilog2(0x000000000003ffffULL) == 17);
	ATF_REQUIRE(ilog2(0x000000000007ffffULL) == 18);
	ATF_REQUIRE(ilog2(0x00000000000fffffULL) == 19);
	ATF_REQUIRE(ilog2(0x00000000001fffffULL) == 20);
	ATF_REQUIRE(ilog2(0x00000000003fffffULL) == 21);
	ATF_REQUIRE(ilog2(0x00000000007fffffULL) == 22);
	ATF_REQUIRE(ilog2(0x0000000000ffffffULL) == 23);
	ATF_REQUIRE(ilog2(0x0000000001ffffffULL) == 24);
	ATF_REQUIRE(ilog2(0x0000000003ffffffULL) == 25);
	ATF_REQUIRE(ilog2(0x0000000007ffffffULL) == 26);
	ATF_REQUIRE(ilog2(0x000000000fffffffULL) == 27);
	ATF_REQUIRE(ilog2(0x000000001fffffffULL) == 28);
	ATF_REQUIRE(ilog2(0x000000003fffffffULL) == 29);
	ATF_REQUIRE(ilog2(0x000000007fffffffULL) == 30);
	ATF_REQUIRE(ilog2(0x00000000ffffffffULL) == 31);
	ATF_REQUIRE(ilog2(0x00000001ffffffffULL) == 32);
	ATF_REQUIRE(ilog2(0x00000003ffffffffULL) == 33);
	ATF_REQUIRE(ilog2(0x00000007ffffffffULL) == 34);
	ATF_REQUIRE(ilog2(0x0000000fffffffffULL) == 35);
	ATF_REQUIRE(ilog2(0x0000001fffffffffULL) == 36);
	ATF_REQUIRE(ilog2(0x0000003fffffffffULL) == 37);
	ATF_REQUIRE(ilog2(0x0000007fffffffffULL) == 38);
	ATF_REQUIRE(ilog2(0x000000ffffffffffULL) == 39);
	ATF_REQUIRE(ilog2(0x000001ffffffffffULL) == 40);
	ATF_REQUIRE(ilog2(0x000003ffffffffffULL) == 41);
	ATF_REQUIRE(ilog2(0x000007ffffffffffULL) == 42);
	ATF_REQUIRE(ilog2(0x00000fffffffffffULL) == 43);
	ATF_REQUIRE(ilog2(0x00001fffffffffffULL) == 44);
	ATF_REQUIRE(ilog2(0x00003fffffffffffULL) == 45);
	ATF_REQUIRE(ilog2(0x00007fffffffffffULL) == 46);
	ATF_REQUIRE(ilog2(0x0000ffffffffffffULL) == 47);
	ATF_REQUIRE(ilog2(0x0001ffffffffffffULL) == 48);
	ATF_REQUIRE(ilog2(0x0003ffffffffffffULL) == 49);
	ATF_REQUIRE(ilog2(0x0007ffffffffffffULL) == 50);
	ATF_REQUIRE(ilog2(0x000fffffffffffffULL) == 51);
	ATF_REQUIRE(ilog2(0x001fffffffffffffULL) == 52);
	ATF_REQUIRE(ilog2(0x003fffffffffffffULL) == 53);
	ATF_REQUIRE(ilog2(0x007fffffffffffffULL) == 54);
	ATF_REQUIRE(ilog2(0x00ffffffffffffffULL) == 55);
	ATF_REQUIRE(ilog2(0x01ffffffffffffffULL) == 56);
	ATF_REQUIRE(ilog2(0x03ffffffffffffffULL) == 57);
	ATF_REQUIRE(ilog2(0x07ffffffffffffffULL) == 58);
	ATF_REQUIRE(ilog2(0x0fffffffffffffffULL) == 59);
	ATF_REQUIRE(ilog2(0x1fffffffffffffffULL) == 60);
	ATF_REQUIRE(ilog2(0x3fffffffffffffffULL) == 61);
	ATF_REQUIRE(ilog2(0x7fffffffffffffffULL) == 62);
	ATF_REQUIRE(ilog2(0xffffffffffffffffULL) == 63);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bitmap_basic);
	ATF_TP_ADD_TC(tp, fast_divide32);
	ATF_TP_ADD_TC(tp, ffsfls);
	ATF_TP_ADD_TC(tp, ilog2_32bit);
	ATF_TP_ADD_TC(tp, ilog2_64bit);
	ATF_TP_ADD_TC(tp, ilog2_const);

	return atf_no_error();
}
