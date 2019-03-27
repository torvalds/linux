/*	$NetBSD: t_sdp_set.c,v 1.2 2011/04/07 08:29:50 plunky Exp $	*/

/*-
 * Copyright (c) 2011 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Iain Hibbert.
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

#include <limits.h>
#include <sdp.h>
#include <string.h>

ATF_TC(check_sdp_set_bool);

ATF_TC_HEAD(check_sdp_set_bool, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_set_bool results");
}

ATF_TC_BODY(check_sdp_set_bool, tc)
{
	uint8_t data[] = {
		0x28, 0x00,	// bool	false
		0x00,		// nil
		0x28,		// bool <invalid>
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t discard;

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_BOOL);
	ATF_REQUIRE(sdp_set_bool(&test, true));
	ATF_CHECK_EQ(test.next[1], 0x01);
	ATF_REQUIRE(sdp_set_bool(&test, false));
	ATF_CHECK_EQ(test.next[1], 0x00);
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_NIL);
	ATF_CHECK_EQ(sdp_set_bool(&test, true), false);		/* not bool */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_BOOL);
	ATF_CHECK_EQ(sdp_set_bool(&test, true), false);		/* no value */
}

ATF_TC(check_sdp_set_uint);

ATF_TC_HEAD(check_sdp_set_uint, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_set_uint results");
}

ATF_TC_BODY(check_sdp_set_uint, tc)
{
	uint8_t data[] = {
		0x08, 0x00,		// uint8	0x00
		0x00,			// nil
		0x09, 0x00, 0x00,	// uint16	0x0000
		0x0a, 0x00, 0x00, 0x00,	// uint32	0x00000000
		0x00,
		0x0b, 0x00, 0x00, 0x00,	// uint64	0x0000000000000000
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x0c, 0x00, 0x44, 0x00,	// uint128	0x00440044004400440044004400440044
		0x44, 0x00, 0x44, 0x00,
		0x44, 0x00, 0x44, 0x00,
		0x44, 0x00, 0x44, 0x00,
		0x00,
		0x09, 0x00,		// uint16	<invalid>
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t discard;

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_UINT8);
	ATF_REQUIRE(sdp_set_uint(&test, 0x44));
	ATF_CHECK_EQ(sdp_set_uint(&test, UINT8_MAX + 1), false);	/* too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_NIL);
	ATF_CHECK_EQ(sdp_set_uint(&test, 0x00), false);			/* not uint */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_UINT16);
	ATF_REQUIRE(sdp_set_uint(&test, 0xabcd));
	ATF_CHECK_EQ(sdp_set_uint(&test, UINT16_MAX + 1), false);	/* too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_UINT32);
	ATF_REQUIRE(sdp_set_uint(&test, 0xdeadbeef));
	ATF_CHECK_EQ(sdp_set_uint(&test, (uintmax_t)UINT32_MAX + 1), false);	/* too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_UINT64);
	ATF_REQUIRE(sdp_set_uint(&test, 0xc0ffeecafec0ffee));
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_UINT128);
	ATF_REQUIRE(sdp_set_uint(&test, 0xabcdef0123456789));
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_UINT16);
	ATF_CHECK_EQ(sdp_set_uint(&test, 0x3344), false);		/* no value */

	const uint8_t expect[] = {
		0x08, 0x44,		// uint8	0x44
		0x00,			// nil
		0x09, 0xab, 0xcd,	// uint16	0xabcd
		0x0a, 0xde, 0xad, 0xbe,	// uint32	0xdeadbeef
		0xef,
		0x0b, 0xc0, 0xff, 0xee,	// uint64	0xc0ffeecafec0ffee
		0xca, 0xfe, 0xc0, 0xff,
		0xee,
		0x0c, 0x00, 0x00, 0x00,	// uint128	0x0000000000000000abcdef0123456789
		0x00, 0x00, 0x00, 0x00,
		0x00, 0xab, 0xcd, 0xef,
		0x01, 0x23, 0x45, 0x67,
		0x89,
		0x09, 0x00,		// uint16	<invalid>
	};

	ATF_REQUIRE_EQ(sizeof(data), sizeof(expect));
	ATF_CHECK(memcmp(expect, data, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_set_int);

ATF_TC_HEAD(check_sdp_set_int, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_set_int results");
}

ATF_TC_BODY(check_sdp_set_int, tc)
{
	uint8_t data[] = {
		0x10, 0x00,		// int8		0
		0x00,			// nil
		0x11, 0x00, 0x00,	// int16	0
		0x12, 0x00, 0x00, 0x00,	// int32	0
		0x00,
		0x13, 0x00, 0x00, 0x00,	// int64	0
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x14, 0x00, 0x44, 0x00,	// int128	0x00440044004400440044004400440044
		0x44, 0x00, 0x44, 0x00,
		0x44, 0x00, 0x44, 0x00,
		0x44, 0x00, 0x44, 0x00,
		0x00,
		0x11, 0x00,		// int16	<invalid>
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t discard;

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT8);
	ATF_REQUIRE(sdp_set_int(&test, -1));
	ATF_CHECK_EQ(sdp_set_int(&test, INT8_MAX + 1), false);	/* too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_NIL);
	ATF_CHECK_EQ(sdp_set_int(&test, 33), false);		/* not int */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT16);
	ATF_REQUIRE(sdp_set_int(&test, 789));
	ATF_CHECK_EQ(sdp_set_int(&test, INT16_MIN - 1), false);	/* too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT32);
	ATF_REQUIRE(sdp_set_int(&test, -4567));
	ATF_CHECK_EQ(sdp_set_int(&test, (intmax_t)INT32_MAX + 1), false);	/* too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT64);
	ATF_REQUIRE(sdp_set_int(&test, -3483738234));
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT128);
	ATF_REQUIRE(sdp_set_int(&test, 3423489463464));
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT16);
	ATF_CHECK_EQ(sdp_set_int(&test, 1234), false);		/* no value */

	const uint8_t expect[] = {
		0x10, 0xff,		// int8		-1
		0x00,			// nil
		0x11, 0x03, 0x15,	// int16	789
		0x12, 0xff, 0xff, 0xee,	// int32	-4567
		0x29,
		0x13, 0xff, 0xff, 0xff,	// int64	-3483738234
		0xff, 0x30, 0x5a, 0x5f,
		0x86,
		0x14, 0x00, 0x00, 0x00,	// int128	3423489463464
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x03,
		0x1d, 0x17, 0xdf, 0x94,
		0xa8,
		0x11, 0x00,		// int16	<invalid>
	};

	ATF_REQUIRE_EQ(sizeof(data), sizeof(expect));
	ATF_CHECK(memcmp(expect, data, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_set_seq);

ATF_TC_HEAD(check_sdp_set_seq, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_set_seq results");
}

ATF_TC_BODY(check_sdp_set_seq, tc)
{
	uint8_t data[] = {
		0x35, 0x03,		// seq8(3)
		0x11, 0xff, 0xff,	//   int16	-1
		0x36, 0x01, 0x00,	// seq16(256)
		0x09, 0xff, 0xff,	// uint16	0xffff
		0x37, 0x01, 0x02, 0x03,	// seq32(16909060)
		0x04,
		0x36, 0x00,		// seq16(<invalid>)
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t discard;

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_SEQ8);
	ATF_REQUIRE(sdp_set_seq(&test, 0));
	ATF_CHECK_EQ(sdp_set_seq(&test, UINT8_MAX), false);	/* data too big */
	ATF_CHECK_EQ(sdp_set_seq(&test, UINT16_MAX), false);	/* size too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT16);
	ATF_CHECK_EQ(sdp_set_seq(&test, 33), false);		/* not seq */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_SEQ16);
	ATF_REQUIRE(sdp_set_seq(&test, 3));
	ATF_CHECK_EQ(sdp_set_seq(&test, SSIZE_MAX), false);	/* size too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_SEQ32);
	ATF_REQUIRE(sdp_set_seq(&test, 0));
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_SEQ16);
	ATF_CHECK_EQ(sdp_set_seq(&test, 22), false);		/* no size */

	const uint8_t expect[] = {
		0x35, 0x00,		// seq8(0)
		0x11, 0xff, 0xff,	// int16	-1
		0x36, 0x00, 0x03,	// seq16(3)
		0x09, 0xff, 0xff,	//   uint16	0xffff
		0x37, 0x00, 0x00, 0x00,	// seq32(0)
		0x00,
		0x36, 0x00,		// seq16(<invalid>)
	};

	ATF_REQUIRE_EQ(sizeof(data), sizeof(expect));
	ATF_CHECK(memcmp(expect, data, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_set_alt);

ATF_TC_HEAD(check_sdp_set_alt, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_set_alt results");
}

ATF_TC_BODY(check_sdp_set_alt, tc)
{
	uint8_t data[] = {
		0x3d, 0x06,		// alt8(6)
		0x11, 0xff, 0xff,	//   int16	-1
		0x3e, 0xff, 0xff,	//   alt16(65535)
		0x3f, 0x01, 0x02, 0x03,	// alt32(16909060)
		0x04,
		0x0a, 0x00, 0x00, 0x00,	// uint32	0x00000003
		0x03,
		0x3e, 0x00,		// alt16(<invalid>)
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t discard;

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_ALT8);
	ATF_REQUIRE(sdp_set_alt(&test, 0));
	ATF_CHECK_EQ(sdp_set_alt(&test, UINT8_MAX), false);	/* data too big */
	ATF_CHECK_EQ(sdp_set_alt(&test, UINT16_MAX), false);	/* size too big */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_INT16);
	ATF_CHECK_EQ(sdp_set_alt(&test, 27), false);		/* not alt */
	ATF_REQUIRE(sdp_get_data(&test, &discard));

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_ALT16);
	ATF_REQUIRE(sdp_set_alt(&test, 10));
	ATF_CHECK_EQ(sdp_set_alt(&test, SSIZE_MAX), false);	/* size too big */
	ATF_REQUIRE(sdp_get_alt(&test, &discard));
	ATF_CHECK_EQ(sdp_data_type(&discard), SDP_DATA_ALT32);
	ATF_CHECK(sdp_set_alt(&discard, -1));			/* end of alt16 */
	ATF_CHECK_EQ(sdp_set_alt(&discard, 6), false);		/* data too big */

	ATF_CHECK_EQ(sdp_data_type(&test), SDP_DATA_ALT16);
	ATF_CHECK_EQ(sdp_set_alt(&test, 22), false);		/* no size */

	const uint8_t expect[] = {
		0x3d, 0x00,		// alt8(0)
		0x11, 0xff, 0xff,	// int16	-1
		0x3e, 0x00, 0x0a,	// alt16(10)
		0x3f, 0x00, 0x00, 0x00,	//   alt32(5)
		0x05,
		0x0a, 0x00, 0x00, 0x00,	//     uint32	0x00000003
		0x03,
		0x3e, 0x00,		// alt16(<invalid>)
	};

	ATF_REQUIRE_EQ(sizeof(data), sizeof(expect));
	ATF_CHECK(memcmp(expect, data, sizeof(expect)) == 0);
}


ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_sdp_set_bool);
	ATF_TP_ADD_TC(tp, check_sdp_set_uint);
	ATF_TP_ADD_TC(tp, check_sdp_set_int);
	ATF_TP_ADD_TC(tp, check_sdp_set_seq);
	ATF_TP_ADD_TC(tp, check_sdp_set_alt);

	return atf_no_error();
}
