/*	$NetBSD: t_sdp_get.c,v 1.2 2011/04/07 08:29:50 plunky Exp $	*/

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

ATF_TC(check_sdp_get_data);

ATF_TC_HEAD(check_sdp_get_data, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_data results");
}

ATF_TC_BODY(check_sdp_get_data, tc)
{
	uint8_t data[] = {
		0x09, 0x00, 0x00,	// uint16	0x0000
		0x35, 0x05,		// seq8(5)
		0x19, 0x00, 0x00,	//   uuid16	0x0000
		0x08, 0x00,		//   uint8	0x00
		0x36, 0x00, 0x01,	// seq16(1)
		0x19,			//   uint16	/* invalid */
		0x25, 0x04, 0x54, 0x45,	// str8(4)	"TEST"
		0x53, 0x54,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t value, seq;

	/*
	 * sdp_get_data constructs a new sdp_data_t containing
	 * the next data element, advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_UINT16);
	ATF_CHECK_EQ(sdp_data_size(&value), 3);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_SEQ8);
	ATF_CHECK_EQ(sdp_data_size(&value), 7);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_SEQ16);
	ATF_CHECK_EQ(sdp_data_size(&value), 4);
	ATF_REQUIRE_EQ(sdp_get_seq(&value, &seq), true);
	ATF_REQUIRE_EQ(sdp_get_data(&seq, &value), false);	/* invalid */

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_STR8);
	ATF_CHECK_EQ(sdp_data_size(&value), 6);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_attr);

ATF_TC_HEAD(check_sdp_get_attr, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_attr results");
}

ATF_TC_BODY(check_sdp_get_attr, tc)
{
	uint8_t data[] = {
		0x09, 0x00, 0x00,	// uint16	0x0000
		0x35, 0x05,		// seq8(5)
		0x19, 0x00, 0x00,	//   uuid16	0x0000
		0x08, 0x00,		//   uint8	0x00
		0x08, 0x00,		// uint8	0x00
		0x09, 0x00, 0x01,	// uint16	0x0001
		0x19, 0x12, 0x34,	// uuid16	0x1234
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t value;
	uint16_t attr;

	/*
	 * sdp_get_attr expects a UINT16 followed by any data item
	 * and advances test if successful
	 */
	ATF_REQUIRE(sdp_get_attr(&test, &attr, &value));
	ATF_CHECK_EQ(attr, 0x0000);
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_SEQ8);
	ATF_CHECK_EQ(sdp_data_size(&value), 7);

	ATF_REQUIRE_EQ(sdp_get_attr(&test, &attr, &value), false);
	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_UINT8);
	ATF_CHECK_EQ(sdp_data_size(&value), 2);

	ATF_REQUIRE(sdp_get_attr(&test, &attr, &value));
	ATF_CHECK_EQ(attr, 0x0001);
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_UUID16);
	ATF_CHECK_EQ(sdp_data_size(&value), 3);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_uuid);

ATF_TC_HEAD(check_sdp_get_uuid, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_uuid results");
}

ATF_TC_BODY(check_sdp_get_uuid, tc)
{
	uint8_t data[] = {
		0x19, 0x12, 0x34,	// uuid16	0x1234
		0x1a, 0x11, 0x22, 0x33,	// uuid32	0x11223344
		0x44,
		0x00,			// nil
		0x1c,			// uuid128	0x00112233-4444--5555-6666-778899aabbcc
		0x00, 0x11, 0x22, 0x33,
		0x44, 0x44, 0x55, 0x55,
		0x66, 0x66, 0x77, 0x88,
		0x99, 0xaa, 0xbb, 0xcc,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	uuid_t u16 = {
		0x00001234,
		0x0000,
		0x1000,
		0x80,
		0x00,
		{ 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb }
	};
	uuid_t u32 = {
		0x11223344,
		0x0000,
		0x1000,
		0x80,
		0x00,
		{ 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb }
	};
	uuid_t u128 = {
		0x00112233,
		0x4444,
		0x5555,
		0x66,
		0x66,
		{ 0x77, 0x88, 0x99, 0xaa, 0xbb, 0xcc }
	};
	sdp_data_t nil;
	uuid_t value;

	/*
	 * sdp_get_uuid expects any UUID type returns the full uuid
	 * advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_uuid(&test, &value));
	ATF_CHECK(uuid_equal(&value, &u16, NULL));

	ATF_REQUIRE(sdp_get_uuid(&test, &value));
	ATF_CHECK(uuid_equal(&value, &u32, NULL));

	ATF_REQUIRE_EQ(sdp_get_uuid(&test, &value), false);	/* not uuid */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_uuid(&test, &value));
	ATF_CHECK(uuid_equal(&value, &u128, NULL));

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_bool);

ATF_TC_HEAD(check_sdp_get_bool, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_bool results");
}

ATF_TC_BODY(check_sdp_get_bool, tc)
{
	uint8_t data[] = {
		0x28, 0x00,	// bool		false
		0x00,		// nil
		0x28, 0x01,	// bool		true
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t nil;
	bool value;

	/*
	 * sdp_get_bool expects a BOOL type
	 * advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_bool(&test, &value));
	ATF_CHECK_EQ(value, false);

	ATF_REQUIRE_EQ(sdp_get_bool(&test, &value), false);	/* not bool */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_bool(&test, &value));
	ATF_CHECK_EQ(value, true);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_uint);

ATF_TC_HEAD(check_sdp_get_uint, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_uint results");
}

ATF_TC_BODY(check_sdp_get_uint, tc)
{
	uint8_t data[] = {
		0x08, 0x00,		// uint8	0x00
		0x08, 0xff,		// uint8	0xff
		0x09, 0x01, 0x02,	// uint16	0x0102
		0x09, 0xff, 0xff,	// uint16	0xffff
		0x00,			// nil
		0x0a, 0x01, 0x02, 0x03,	// uint32	0x01020304
		0x04,
		0x0a, 0xff, 0xff, 0xff,	// uint32	0xffffffff
		0xff,
		0x0b, 0x01, 0x02, 0x03,	// uint64	0x0102030405060708
		0x04, 0x05, 0x06, 0x07,
		0x08,
		0x0b, 0xff, 0xff, 0xff,	// uint64	0xffffffffffffffff
		0xff, 0xff, 0xff, 0xff,
		0xff,
		0x0c, 0x00, 0x00, 0x00,	// uint128	0x00000000000000000000000000000000
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x0c, 0x00, 0x00, 0x00,	// uint128	0x00000000000000010000000000000000
		0x00, 0x00, 0x00, 0x00,
		0x01, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x0c, 0x00, 0x00, 0x00,	// uint128	0x0000000000000000ffffffffffffffff
		0x00, 0x00, 0x00, 0x00,
		0x00, 0xff, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff,
		0xff,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t nil;
	uintmax_t value;

	/*
	 * sdp_get_uint expects any UINT type, advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, 0x00);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, UINT8_MAX);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, 0x0102);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, UINT16_MAX);

	ATF_REQUIRE_EQ(sdp_get_uint(&test, &value), false);	/* not uint */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, 0x01020304);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, UINT32_MAX);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, 0x0102030405060708);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, UINT64_MAX);

	/*
	 * expected failure is that we cannot decode UINT128 values larger than UINT64
	 */
	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, 0x00000000000000000000000000000000);

	ATF_REQUIRE_EQ(sdp_get_uint(&test, &value), false);	/* overflow */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_UINT128);

	ATF_REQUIRE(sdp_get_uint(&test, &value));
	ATF_CHECK_EQ(value, UINT64_MAX);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_int);

ATF_TC_HEAD(check_sdp_get_int, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_int results");
}

ATF_TC_BODY(check_sdp_get_int, tc)
{
	uint8_t data[] = {
		0x10, 0x00,		// int8		0x00
		0x10, 0x7f,		// int8		0x7f
		0x10, 0x80,		// int8		0x80
		0x11, 0x01, 0x02,	// int16	0x0102
		0x11, 0x7f, 0xff,	// int16	0x7fff
		0x11, 0x80, 0x00,	// int16	0x8000
		0x00,			// nil
		0x12, 0x01, 0x02, 0x03,	// int32	0x01020304
		0x04,
		0x12, 0x7f, 0xff, 0xff,	// int32	0x7fffffff
		0xff,
		0x12, 0x80, 0x00, 0x00,	// int32	0x80000000
		0x00,
		0x13, 0x01, 0x02, 0x03,	// int64	0x0102030405060708
		0x04, 0x05, 0x06, 0x07,
		0x08,
		0x13, 0x7f, 0xff, 0xff,	// int64	0x7fffffffffffffff
		0xff, 0xff, 0xff, 0xff,
		0xff,
		0x13, 0x80, 0x00, 0x00,	// int64	0x8000000000000000
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x14, 0x00, 0x00, 0x00,	// int128	0x00000000000000000000000000000000
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x14, 0x00, 0x00, 0x00,	// int128	0x00000000000000007fffffffffffffff
		0x00, 0x00, 0x00, 0x00,	//			(INT64_MAX)
		0x00, 0x7f, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff,
		0xff,
		0x14, 0x00, 0x00, 0x00,	// int128	0x00000000000000008000000000000000
		0x00, 0x00, 0x00, 0x00,	//			(INT64_MAX + 1)
		0x00, 0x80, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x14, 0xff, 0xff, 0xff,	// int128	0xffffffffffffffff8000000000000000
		0xff, 0xff, 0xff, 0xff,	//			(INT64_MIN)
		0xff, 0x80, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x14, 0xff, 0xff, 0xff,	// int128	0xffffffffffffffff7fffffffffffffff
		0xff, 0xff, 0xff, 0xff,	//			(INT64_MIN - 1)
		0xff, 0x7f, 0xff, 0xff,
		0xff, 0xff, 0xff, 0xff,
		0xff,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t nil;
	intmax_t value;

	/*
	 * sdp_get_int expects any INT type, advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, 0);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT8_MAX);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT8_MIN);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, 0x0102);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT16_MAX);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT16_MIN);

	ATF_REQUIRE_EQ(sdp_get_int(&test, &value), false);	/* not int */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, 0x01020304);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT32_MAX);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT32_MIN);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, 0x0102030405060708);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT64_MAX);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT64_MIN);

	/*
	 * expected failure is that we cannot decode INT128 values larger than INT64
	 */
	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, 0);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT64_MAX);

	ATF_REQUIRE_EQ(sdp_get_int(&test, &value), false);	/* overflow */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_INT128);

	ATF_REQUIRE(sdp_get_int(&test, &value));
	ATF_CHECK_EQ(value, INT64_MIN);

	ATF_REQUIRE_EQ(sdp_get_int(&test, &value), false);	/* underflow */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_INT128);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_seq);

ATF_TC_HEAD(check_sdp_get_seq, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_seq results");
}

ATF_TC_BODY(check_sdp_get_seq, tc)
{
	uint8_t data[] = {
		0x35, 0x00,		// seq8(0)
		0x00,			// nil
		0x36, 0x00, 0x00,	// seq16(0)
		0x37, 0x00, 0x00, 0x00,	// seq32(0)
		0x00,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t value;

	/*
	 * sdp_get_seq expects a SEQ type
	 * advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_seq(&test, &value));
	ATF_CHECK_EQ(value.next, value.end);

	ATF_REQUIRE_EQ(sdp_get_seq(&test, &value), false);	/* not seq */
	ATF_REQUIRE(sdp_get_data(&test, &value));		/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_seq(&test, &value));
	ATF_CHECK_EQ(value.next, value.end);

	ATF_REQUIRE(sdp_get_seq(&test, &value));
	ATF_CHECK_EQ(value.next, value.end);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_alt);

ATF_TC_HEAD(check_sdp_get_alt, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_alt results");
}

ATF_TC_BODY(check_sdp_get_alt, tc)
{
	uint8_t data[] = {
		0x3d, 0x00,		// alt8(0)
		0x00,			// nil
		0x3e, 0x00, 0x00,	// alt16(0)
		0x3f, 0x00, 0x00, 0x00,	// alt32(0)
		0x00,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t value;

	/*
	 * sdp_get_alt expects a ALT type
	 * advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_alt(&test, &value));
	ATF_CHECK_EQ(value.next, value.end);

	ATF_REQUIRE_EQ(sdp_get_alt(&test, &value), false);	/* not alt */
	ATF_REQUIRE(sdp_get_data(&test, &value));		/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_alt(&test, &value));
	ATF_CHECK_EQ(value.next, value.end);

	ATF_REQUIRE(sdp_get_alt(&test, &value));
	ATF_CHECK_EQ(value.next, value.end);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_str);

ATF_TC_HEAD(check_sdp_get_str, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_str results");
}

ATF_TC_BODY(check_sdp_get_str, tc)
{
	uint8_t data[] = {
		0x25, 0x04, 0x53, 0x54, // str8(4)	"STR8"
		0x52, 0x38,
		0x00,			// nil
		0x26, 0x00, 0x05, 0x53,	// str16(5)	"STR16"
		0x54, 0x52, 0x31, 0x36,
		0x27, 0x00, 0x00, 0x00,	// str32(5)	"STR32"
		0x05, 0x53, 0x54, 0x52,
		0x33, 0x32,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t nil;
	char *str;
	size_t len;

	/*
	 * sdp_get_str expects a STR type
	 * advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_str(&test, &str, &len));
	ATF_CHECK(len == 4 && strncmp(str, "STR8", 4) == 0);

	ATF_REQUIRE_EQ(sdp_get_str(&test, &str, &len), false);	/* not str */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_str(&test, &str, &len));
	ATF_CHECK(len == 5 && strncmp(str, "STR16", 5) == 0);

	ATF_REQUIRE(sdp_get_str(&test, &str, &len));
	ATF_CHECK(len == 5 && strncmp(str, "STR32", 5) == 0);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_get_url);

ATF_TC_HEAD(check_sdp_get_url, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_get_url results");
}

ATF_TC_BODY(check_sdp_get_url, tc)
{
	uint8_t data[] = {
		0x45, 0x04, 0x55, 0x52, // url8(4)	"URL8"
		0x4c, 0x38,
		0x00,			// nil
		0x46, 0x00, 0x05, 0x55,	// url16(5)	"URL16"
		0x52, 0x4c, 0x31, 0x36,
		0x47, 0x00, 0x00, 0x00,	// url32(5)	"URL32"
		0x05, 0x55, 0x52, 0x4c,
		0x33, 0x32,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t nil;
	char *url;
	size_t len;

	/*
	 * sdp_get_url expects a URL type
	 * advancing test if successful
	 */
	ATF_REQUIRE(sdp_get_url(&test, &url, &len));
	ATF_CHECK(len == 4 && strncmp(url, "URL8", 4) == 0);

	ATF_REQUIRE_EQ(sdp_get_url(&test, &url, &len), false);	/* not url */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_url(&test, &url, &len));
	ATF_CHECK(len == 5 && strncmp(url, "URL16", 5) == 0);

	ATF_REQUIRE(sdp_get_url(&test, &url, &len));
	ATF_CHECK(len == 5 && strncmp(url, "URL32", 5) == 0);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_sdp_get_data);
	ATF_TP_ADD_TC(tp, check_sdp_get_attr);
	ATF_TP_ADD_TC(tp, check_sdp_get_uuid);
	ATF_TP_ADD_TC(tp, check_sdp_get_bool);
	ATF_TP_ADD_TC(tp, check_sdp_get_uint);
	ATF_TP_ADD_TC(tp, check_sdp_get_int);
	ATF_TP_ADD_TC(tp, check_sdp_get_seq);
	ATF_TP_ADD_TC(tp, check_sdp_get_alt);
	ATF_TP_ADD_TC(tp, check_sdp_get_str);
	ATF_TP_ADD_TC(tp, check_sdp_get_url);

	return atf_no_error();
}
