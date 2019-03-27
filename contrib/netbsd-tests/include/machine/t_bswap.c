/* $NetBSD: t_bswap.c,v 1.1 2011/05/05 05:39:11 jruoho Exp $ */

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
__RCSID("$NetBSD: t_bswap.c,v 1.1 2011/05/05 05:39:11 jruoho Exp $");

#include <sys/types.h>
#include <machine/bswap.h>

#include <atf-c.h>

static uint16_t x16;
static uint32_t x32;
static uint64_t x64;

static uint16_t	unconst16(uint16_t);
static uint32_t	unconst32(uint32_t);
static uint64_t	unconst64(uint64_t);

/*
 * Given the use of __builtin_constant_p(3),
 * these functions try to avoid gcc(1) from
 * treating the arguments as constants.
 */
static uint16_t
unconst16(uint16_t val)
{
	return val + x16;
}

static uint32_t
unconst32(uint32_t val)
{
	return val + x32;
}

static uint64_t
unconst64(uint64_t val)
{
	return val + x64;
}

ATF_TC(bswap16_basic);
ATF_TC_HEAD(bswap16_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of bswap16(3), #1");
}

ATF_TC_BODY(bswap16_basic, tc)
{
	ATF_REQUIRE_EQ(bswap16(0x0000), 0x0000);
	ATF_REQUIRE_EQ(bswap16(0xff00), 0x00ff);
	ATF_REQUIRE_EQ(bswap16(0xffff), 0xffff);
	ATF_REQUIRE_EQ(bswap16(0x1234), 0x3412);
}

ATF_TC(bswap16_unconst);
ATF_TC_HEAD(bswap16_unconst, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of bswap16(3), #2");
}

ATF_TC_BODY(bswap16_unconst, tc)
{
	x16 = 0;

	ATF_REQUIRE_EQ(bswap16(unconst16(0x0000)), 0x0000);
	ATF_REQUIRE_EQ(bswap16(unconst16(0xff00)), 0x00ff);
	ATF_REQUIRE_EQ(bswap16(unconst16(0xffff)), 0xffff);
	ATF_REQUIRE_EQ(bswap16(unconst16(0x1234)), 0x3412);
}

ATF_TC(bswap32_basic);
ATF_TC_HEAD(bswap32_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of bswap32(3), #1");
}

ATF_TC_BODY(bswap32_basic, tc)
{
	ATF_REQUIRE_EQ(bswap32(0x00000000), 0x00000000);
	ATF_REQUIRE_EQ(bswap32(0xffff0000), 0x0000ffff);
	ATF_REQUIRE_EQ(bswap32(0xffffffff), 0xffffffff);
	ATF_REQUIRE_EQ(bswap32(0x12345678), 0x78563412);
}

ATF_TC(bswap32_unconst);
ATF_TC_HEAD(bswap32_unconst, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of bswap32(3), #2");
}

ATF_TC_BODY(bswap32_unconst, tc)
{
	x32 = 0;

	ATF_REQUIRE_EQ(bswap32(unconst32(0x00000000)), 0x00000000);
	ATF_REQUIRE_EQ(bswap32(unconst32(0xffff0000)), 0x0000ffff);
	ATF_REQUIRE_EQ(bswap32(unconst32(0xffffffff)), 0xffffffff);
	ATF_REQUIRE_EQ(bswap32(unconst32(0x12345678)), 0x78563412);
}

ATF_TC(bswap64_basic);
ATF_TC_HEAD(bswap64_basic, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of bswap64(3), #1");
}

ATF_TC_BODY(bswap64_basic, tc)
{
	ATF_REQUIRE_EQ(bswap64(0x0000000000000000), 0x0000000000000000);
	ATF_REQUIRE_EQ(bswap64(0xffffffff00000000), 0x00000000ffffffff);
	ATF_REQUIRE_EQ(bswap64(0xffffffffffffffff), 0xffffffffffffffff);
	ATF_REQUIRE_EQ(bswap64(0x123456789abcdeff), 0xffdebc9a78563412);
}

ATF_TC(bswap64_unconst);
ATF_TC_HEAD(bswap64_unconst, tc)
{
	atf_tc_set_md_var(tc, "descr", "A naive test of bswap64(3), #2");
}

ATF_TC_BODY(bswap64_unconst, tc)
{
	x64 = 0;

	ATF_REQUIRE_EQ(bswap64(unconst64(0x0000000000000000)),
	    0x0000000000000000);

	ATF_REQUIRE_EQ(bswap64(unconst64(0xffffffff00000000)),
	    0x00000000ffffffff);

	ATF_REQUIRE_EQ(bswap64(unconst64(0xffffffffffffffff)),
	    0xffffffffffffffff);

	ATF_REQUIRE_EQ(bswap64(unconst64(0x123456789abcdeff)),
	    0xffdebc9a78563412);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, bswap16_basic);
	ATF_TP_ADD_TC(tp, bswap16_unconst);
	ATF_TP_ADD_TC(tp, bswap32_basic);
	ATF_TP_ADD_TC(tp, bswap32_unconst);
	ATF_TP_ADD_TC(tp, bswap64_basic);
	ATF_TP_ADD_TC(tp, bswap64_unconst);

	return atf_no_error();
}
