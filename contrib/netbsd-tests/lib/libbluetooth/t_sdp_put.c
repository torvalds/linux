/*	$NetBSD: t_sdp_put.c,v 1.3 2011/04/16 07:32:27 plunky Exp $	*/

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

ATF_TC(check_sdp_put_data);

ATF_TC_HEAD(check_sdp_put_data, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_data results");
}

ATF_TC_BODY(check_sdp_put_data, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };
	uint8_t data[] = {
		0x35, 0x05,		// seq8(5)
		0x08, 0x00,		//   uint8	0x00
		0x09, 0x12, 0x34,	//   uint16	0x1234
	};
	sdp_data_t value = { data, data + sizeof(data) };

	ATF_REQUIRE(sdp_put_data(&test, &value));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x35, 0x05,		// seq8(5)
		0x08, 0x00,		//   uint8	0x00
		0x09, 0x12, 0x34,	//   uint16	0x1234
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_attr);

ATF_TC_HEAD(check_sdp_put_attr, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_attr results");
}

ATF_TC_BODY(check_sdp_put_attr, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };
	uint8_t data[] = {
		0x00,			// nil
		0x19, 0x33, 0x44,	// uuid16	0x3344
	};
	sdp_data_t value = { data, data + sizeof(data) };

	ATF_REQUIRE_EQ(sdp_put_attr(&test, 0xabcd, &value), false);
	value.next += 1; // skip "nil"
	ATF_REQUIRE(sdp_put_attr(&test, 0x1337, &value));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x09, 0x13, 0x37,	// uint16	0x1337
		0x19, 0x33, 0x44,	// uuid16	0x3344
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uuid);

ATF_TC_HEAD(check_sdp_put_uuid, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uuid results");
}

ATF_TC_BODY(check_sdp_put_uuid, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };
	const uuid_t u16 = {
		0x00001234,
		0x0000,
		0x1000,
		0x80,
		0x00,
		{ 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb }
	};
	const uuid_t u32 = {
		0x12345678,
		0x0000,
		0x1000,
		0x80,
		0x00,
		{ 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb }
	};
	const uuid_t u128 = {
		0x00112233,
		0x4444,
		0x5555,
		0x66,
		0x77,
		{ 0x88, 0x99, 0xaa, 0xbb, 0xcc, 0xdd }
	};

	ATF_REQUIRE(sdp_put_uuid(&test, &u16));
	ATF_REQUIRE(sdp_put_uuid(&test, &u32));
	ATF_REQUIRE(sdp_put_uuid(&test, &u128));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x19, 0x12, 0x34,	// uuid16	0x1234
		0x1a, 0x12, 0x34, 0x56, // uuid32	0x12345678
		0x78,
		0x1c, 0x00, 0x11, 0x22,	// uuid128	00112233-4444-5555-6677-8899aabbccdd
		0x33, 0x44, 0x44, 0x55,
		0x55, 0x66, 0x77, 0x88,
		0x99, 0xaa, 0xbb, 0xcc,
		0xdd,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uuid16);

ATF_TC_HEAD(check_sdp_put_uuid16, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uuid16 results");
}

ATF_TC_BODY(check_sdp_put_uuid16, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uuid16(&test, 0x4567));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x19, 0x45, 0x67,	// uuid16	0x4567
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uuid32);

ATF_TC_HEAD(check_sdp_put_uuid32, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uuid32 results");
}

ATF_TC_BODY(check_sdp_put_uuid32, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uuid32(&test, 0xabcdef00));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x1a, 0xab, 0xcd, 0xef, // uuid32	0xabcdef00
		0x00,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uuid128);

ATF_TC_HEAD(check_sdp_put_uuid128, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uuid128 results");
}

ATF_TC_BODY(check_sdp_put_uuid128, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };
	uuid_t value = {
		0x00000100,
		0x0000,
		0x1000,
		0x80,
		0x00,
		{ 0x00, 0x80, 0x5f, 0x9b, 0x34, 0xfb }
	};

	ATF_REQUIRE(sdp_put_uuid128(&test, &value));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x1c, 0x00, 0x00, 0x01,	// uuid128	0000100-0000-1000-8000-00805f9b34fb
		0x00, 0x00, 0x00, 0x10,	//			(L2CAP protocol)
		0x00, 0x80, 0x00, 0x00,
		0x80, 0x5f, 0x9b, 0x34,
		0xfb,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_bool);

ATF_TC_HEAD(check_sdp_put_bool, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_bool results");
}

ATF_TC_BODY(check_sdp_put_bool, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_bool(&test, true));
	ATF_REQUIRE(sdp_put_bool(&test, false));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x28, 0x01,		// bool	true
		0x28, 0x00,		// bool	false
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uint);

ATF_TC_HEAD(check_sdp_put_uint, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uint results");
}

ATF_TC_BODY(check_sdp_put_uint, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)0));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT8_MAX));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT8_MAX + 1));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT16_MAX));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT16_MAX + 1));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT32_MAX));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT32_MAX + 1));
	ATF_REQUIRE(sdp_put_uint(&test, (uintmax_t)UINT64_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x08, 0x00,		// uint8	0x00
		0x08, 0xff,		// uint8	0xff
		0x09, 0x01, 0x00,	// uint16	0x0100
		0x09, 0xff, 0xff,	// uint16	0xffff
		0x0a, 0x00, 0x01, 0x00,	// uint32	0x00010000
		0x00,
		0x0a, 0xff, 0xff, 0xff,	// uint32	0xffffffff
		0xff,
		0x0b, 0x00, 0x00, 0x00,	// uint64	0x0000000100000000
		0x01, 0x00, 0x00, 0x00,
		0x00,
		0x0b, 0xff, 0xff, 0xff,	// uint64	0xffffffffffffffff
		0xff, 0xff, 0xff, 0xff,
		0xff,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uint8);

ATF_TC_HEAD(check_sdp_put_uint8, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uint8 results");
}

ATF_TC_BODY(check_sdp_put_uint8, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uint8(&test, (uint8_t)0));
	ATF_REQUIRE(sdp_put_uint8(&test, (uint8_t)UINT8_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x08, 0x00,		// uint8	0x00
		0x08, 0xff,		// uint8	0xff
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uint16);

ATF_TC_HEAD(check_sdp_put_uint16, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uint16 results");
}

ATF_TC_BODY(check_sdp_put_uint16, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uint16(&test, (uint16_t)0));
	ATF_REQUIRE(sdp_put_uint16(&test, (uint16_t)UINT8_MAX));
	ATF_REQUIRE(sdp_put_uint16(&test, (uint16_t)UINT16_MAX));
	ATF_REQUIRE(sdp_put_uint16(&test, (uint16_t)0xabcd));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x09, 0x00, 0x00,	// uint16	0x0000
		0x09, 0x00, 0xff,	// uint16	0x00ff
		0x09, 0xff, 0xff,	// uint16	0xffff
		0x09, 0xab, 0xcd,	// uint16	0xabcd
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uint32);

ATF_TC_HEAD(check_sdp_put_uint32, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uint32 results");
}

ATF_TC_BODY(check_sdp_put_uint32, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uint32(&test, (uint32_t)0));
	ATF_REQUIRE(sdp_put_uint32(&test, (uint32_t)UINT8_MAX));
	ATF_REQUIRE(sdp_put_uint32(&test, (uint32_t)UINT16_MAX));
	ATF_REQUIRE(sdp_put_uint32(&test, (uint32_t)UINT32_MAX));
	ATF_REQUIRE(sdp_put_uint32(&test, (uint32_t)0xdeadbeef));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x0a, 0x00, 0x00, 0x00,	// uint32	0x00000000
		0x00,
		0x0a, 0x00, 0x00, 0x00,	// uint32	0x000000ff
		0xff,
		0x0a, 0x00, 0x00, 0xff,	// uint32	0x0000ffff
		0xff,
		0x0a, 0xff, 0xff, 0xff,	// uint32	0xffffffff
		0xff,
		0x0a, 0xde, 0xad, 0xbe,	// uint32	0xdeadbeef
		0xef,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_uint64);

ATF_TC_HEAD(check_sdp_put_uint64, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_uint64 results");
}

ATF_TC_BODY(check_sdp_put_uint64, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_uint64(&test, (uint64_t)0));
	ATF_REQUIRE(sdp_put_uint64(&test, (uint64_t)UINT8_MAX));
	ATF_REQUIRE(sdp_put_uint64(&test, (uint64_t)UINT16_MAX));
	ATF_REQUIRE(sdp_put_uint64(&test, (uint64_t)UINT32_MAX));
	ATF_REQUIRE(sdp_put_uint64(&test, (uint64_t)UINT64_MAX));
	ATF_REQUIRE(sdp_put_uint64(&test, (uint64_t)0xc0ffeecafec0ffee));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x0b, 0x00, 0x00, 0x00,	// uint64	0x0000000000000000
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x0b, 0x00, 0x00, 0x00,	// uint64	0x00000000000000ff
		0x00, 0x00, 0x00, 0x00,
		0xff,
		0x0b, 0x00, 0x00, 0x00,	// uint64	0x000000000000ffff
		0x00, 0x00, 0x00, 0xff,
		0xff,
		0x0b, 0x00, 0x00, 0x00,	// uint64	0x00000000ffffffff
		0x00, 0xff, 0xff, 0xff,
		0xff,
		0x0b, 0xff, 0xff, 0xff,	// uint64	0xffffffffffffffff
		0xff, 0xff, 0xff, 0xff,
		0xff,
		0x0b, 0xc0, 0xff, 0xee,	// uint64	0xc0ffeecafec0ffee
		0xca, 0xfe, 0xc0, 0xff,
		0xee,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_int);

ATF_TC_HEAD(check_sdp_put_int, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_int results");
}

ATF_TC_BODY(check_sdp_put_int, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)0));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT8_MIN));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT8_MAX));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT8_MIN - 1));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT8_MAX + 1));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT16_MIN));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT16_MAX));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT16_MIN - 1));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT16_MAX + 1));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT32_MIN));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT32_MAX));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT32_MIN - 1));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT32_MAX + 1));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT64_MIN));
	ATF_REQUIRE(sdp_put_int(&test, (intmax_t)INT64_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x10, 0x00,		// int8		0
		0x10, 0x80,		// int8		-128
		0x10, 0x7f,		// int8		127
		0x11, 0xff, 0x7f,	// int16	-129
		0x11, 0x00, 0x80,	// int16	128
		0x11, 0x80, 0x00,	// int16	-32768
		0x11, 0x7f, 0xff,	// int16	32767
		0x12, 0xff, 0xff, 0x7f,	// int32	-32769
		0xff,
		0x12, 0x00, 0x00, 0x80,	// int32	32768
		0x00,
		0x12, 0x80, 0x00, 0x00,	// int32	-2147483648
		0x00,
		0x12, 0x7f, 0xff, 0xff,	// int32	2147483647
		0xff,
		0x13, 0xff, 0xff, 0xff,	// int64	-2147483649
		0xff, 0x7f, 0xff, 0xff,
		0xff,
		0x13, 0x00, 0x00, 0x00,	// int64	2147483648
		0x00, 0x80, 0x00, 0x00,
		0x00,
		0x13, 0x80, 0x00, 0x00,	// int64	-9223372036854775808
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x13, 0x7f, 0xff, 0xff,	// int64	9223372036854775807
		0xff, 0xff, 0xff, 0xff,
		0xff,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_int8);

ATF_TC_HEAD(check_sdp_put_int8, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_int8 results");
}

ATF_TC_BODY(check_sdp_put_int8, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_int8(&test, (int8_t)0));
	ATF_REQUIRE(sdp_put_int8(&test, (int8_t)INT8_MIN));
	ATF_REQUIRE(sdp_put_int8(&test, (int8_t)INT8_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x10, 0x00,		// int8		0
		0x10, 0x80,		// int8		-128
		0x10, 0x7f,		// int8		127
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_int16);

ATF_TC_HEAD(check_sdp_put_int16, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_int16 results");
}

ATF_TC_BODY(check_sdp_put_int16, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_int16(&test, (int16_t)0));
	ATF_REQUIRE(sdp_put_int16(&test, (int16_t)INT8_MIN));
	ATF_REQUIRE(sdp_put_int16(&test, (int16_t)INT8_MAX));
	ATF_REQUIRE(sdp_put_int16(&test, (int16_t)INT16_MIN));
	ATF_REQUIRE(sdp_put_int16(&test, (int16_t)INT16_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x11, 0x00, 0x00,	// int16	0
		0x11, 0xff, 0x80,	// int16	-128
		0x11, 0x00, 0x7f,	// int16	127
		0x11, 0x80, 0x00,	// int16	-32768
		0x11, 0x7f, 0xff,	// int16	32767
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_int32);

ATF_TC_HEAD(check_sdp_put_int32, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_int32 results");
}

ATF_TC_BODY(check_sdp_put_int32, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)0));
	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)INT8_MIN));
	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)INT8_MAX));
	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)INT16_MIN));
	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)INT16_MAX));
	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)INT32_MIN));
	ATF_REQUIRE(sdp_put_int32(&test, (int32_t)INT32_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x12, 0x00, 0x00, 0x00,	// int32	0
		0x00,
		0x12, 0xff, 0xff, 0xff,	// int32	-128
		0x80,
		0x12, 0x00, 0x00, 0x00,	// int32	127
		0x7f,
		0x12, 0xff, 0xff, 0x80,	// int32	-32768
		0x00,
		0x12, 0x00, 0x00, 0x7f,	// int32	32767
		0xff,
		0x12, 0x80, 0x00, 0x00,	// int32	-2147483648
		0x00,
		0x12, 0x7f, 0xff, 0xff,	// int32	2147483647
		0xff,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_int64);

ATF_TC_HEAD(check_sdp_put_int64, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_int64 results");
}

ATF_TC_BODY(check_sdp_put_int64, tc)
{
	uint8_t buf[256];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)0));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT8_MIN));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT8_MAX));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT16_MIN));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT16_MAX));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT32_MIN));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT32_MAX));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT64_MIN));
	ATF_REQUIRE(sdp_put_int64(&test, (int64_t)INT64_MAX));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x13, 0x00, 0x00, 0x00,	// int64	0
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x13, 0xff, 0xff, 0xff,	// int64	-128
		0xff, 0xff, 0xff, 0xff,
		0x80,
		0x13, 0x00, 0x00, 0x00,	// int64	127
		0x00, 0x00, 0x00, 0x00,
		0x7f,
		0x13, 0xff, 0xff, 0xff,	// int64	-32768
		0xff, 0xff, 0xff, 0x80,
		0x00,
		0x13, 0x00, 0x00, 0x00,	// int64	32767
		0x00, 0x00, 0x00, 0x7f,
		0xff,
		0x13, 0xff, 0xff, 0xff,	// int64	-2147483648
		0xff, 0x80, 0x00, 0x00,
		0x00,
		0x13, 0x00, 0x00, 0x00,	// int64	2147483647
		0x00, 0x7f, 0xff, 0xff,
		0xff,
		0x13, 0x80, 0x00, 0x00,	// int64	-9223372036854775808
		0x00, 0x00, 0x00, 0x00,
		0x00,
		0x13, 0x7f, 0xff, 0xff,	// int64	9223372036854775807
		0xff, 0xff, 0xff, 0xff,
		0xff,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_seq);

ATF_TC_HEAD(check_sdp_put_seq, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_seq results");
}

ATF_TC_BODY(check_sdp_put_seq, tc)
{
	uint8_t buf[512];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_seq(&test, (ssize_t)0));
	ATF_REQUIRE(sdp_put_seq(&test, (ssize_t)UINT8_MAX));
	ATF_REQUIRE(sdp_put_seq(&test, (ssize_t)UINT8_MAX + 1));
	ATF_REQUIRE(sdp_put_seq(&test, (ssize_t)-1));
	ATF_CHECK_EQ(sdp_put_seq(&test, (ssize_t)UINT16_MAX), false);	/* no room */
	ATF_CHECK_EQ(sdp_put_seq(&test, (ssize_t)SSIZE_MAX), false);	/* no room */
	test.end = test.next;
	test.next = buf;

	/* (not a valid element list) */
	const uint8_t expect[] = {
		0x35, 0x00,		// seq8(0)
		0x35, 0xff,		// seq8(255)
		0x36, 0x01, 0x00,	// seq16(256)
		0x36, 0x01, 0xf6,	// seq16(502)	<- sizeof(buf) - 7 - 3
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_alt);

ATF_TC_HEAD(check_sdp_put_alt, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_alt results");
}

ATF_TC_BODY(check_sdp_put_alt, tc)
{
	uint8_t buf[512];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	ATF_REQUIRE(sdp_put_alt(&test, (ssize_t)0));
	ATF_REQUIRE(sdp_put_alt(&test, (ssize_t)UINT8_MAX));
	ATF_REQUIRE(sdp_put_alt(&test, (ssize_t)UINT8_MAX + 1));
	ATF_REQUIRE(sdp_put_alt(&test, (ssize_t)-1));
	ATF_CHECK_EQ(sdp_put_alt(&test, (ssize_t)UINT16_MAX), false);	/* no room */
	ATF_CHECK_EQ(sdp_put_alt(&test, (ssize_t)SSIZE_MAX), false);	/* no room */
	test.end = test.next;
	test.next = buf;

	/* (not a valid element list) */
	const uint8_t expect[] = {
		0x3d, 0x00,		// alt8(0)
		0x3d, 0xff,		// alt8(255)
		0x3e, 0x01, 0x00,	// alt16(256)
		0x3e, 0x01, 0xf6,	// alt16(502)	<- sizeof(buf) - 7 - 3
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_str);

ATF_TC_HEAD(check_sdp_put_str, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_str results");
}

ATF_TC_BODY(check_sdp_put_str, tc)
{
	uint8_t buf[512];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	/*
	 * this does not test str16 or str32, but that is
	 * handled by the same code as sdp_put_seq above..
	 */

	ATF_REQUIRE(sdp_put_str(&test, "Hello World!", 5));
	ATF_REQUIRE(sdp_put_str(&test, "Hello\0World", 11));
	ATF_REQUIRE(sdp_put_str(&test, "Hello World!", -1));
	ATF_REQUIRE(sdp_put_str(&test, "Hello\0World", -1));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x25, 0x05, 0x48, 0x65,	// str8		"Hello"
		0x6c, 0x6c, 0x6f,
		0x25, 0x0b, 0x48, 0x65,	// str8		"Hello\0World"
		0x6c, 0x6c, 0x6f, 0x00,
		0x57, 0x6f, 0x72, 0x6c,
		0x64,
		0x25, 0x0c, 0x48, 0x65,	// str8		"Hello World!"
		0x6c, 0x6c, 0x6f, 0x20,
		0x57, 0x6f, 0x72, 0x6c,
		0x64, 0x21,
		0x25, 0x05, 0x48, 0x65,	// str8		"Hello"
		0x6c, 0x6c, 0x6f,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TC(check_sdp_put_url);

ATF_TC_HEAD(check_sdp_put_url, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_put_url results");
}

ATF_TC_BODY(check_sdp_put_url, tc)
{
	uint8_t buf[512];
	sdp_data_t test = { buf, buf + sizeof(buf) };

	/*
	 * this does not test url16 or url32, but that is
	 * handled by the same code as sdp_put_seq above..
	 */

	ATF_REQUIRE(sdp_put_url(&test, "http://www.netbsd.org/", 21));
	ATF_REQUIRE(sdp_put_url(&test, "http://www.netbsd.org/", -1));
	test.end = test.next;
	test.next = buf;

	const uint8_t expect[] = {
		0x45, 0x15, 0x68, 0x74,	// url8	"http://www.netbsd.org"
		0x74, 0x70, 0x3a, 0x2f,
		0x2f, 0x77, 0x77, 0x77,
		0x2e, 0x6e, 0x65, 0x74,
		0x62, 0x73, 0x64, 0x2e,
		0x6f, 0x72, 0x67,
		0x45, 0x16, 0x68, 0x74,	// url8	"http://www.netbsd.org/"
		0x74, 0x70, 0x3a, 0x2f,
		0x2f, 0x77, 0x77, 0x77,
		0x2e, 0x6e, 0x65, 0x74,
		0x62, 0x73, 0x64, 0x2e,
		0x6f, 0x72, 0x67, 0x2f,
	};

	ATF_REQUIRE_EQ(test.end - test.next, sizeof(expect));
	ATF_CHECK(memcmp(expect, test.next, sizeof(expect)) == 0);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_sdp_put_data);
	ATF_TP_ADD_TC(tp, check_sdp_put_attr);
	ATF_TP_ADD_TC(tp, check_sdp_put_uuid);
	ATF_TP_ADD_TC(tp, check_sdp_put_uuid16);
	ATF_TP_ADD_TC(tp, check_sdp_put_uuid32);
	ATF_TP_ADD_TC(tp, check_sdp_put_uuid128);
	ATF_TP_ADD_TC(tp, check_sdp_put_bool);
	ATF_TP_ADD_TC(tp, check_sdp_put_uint);
	ATF_TP_ADD_TC(tp, check_sdp_put_uint8);
	ATF_TP_ADD_TC(tp, check_sdp_put_uint16);
	ATF_TP_ADD_TC(tp, check_sdp_put_uint32);
	ATF_TP_ADD_TC(tp, check_sdp_put_uint64);
	ATF_TP_ADD_TC(tp, check_sdp_put_int);
	ATF_TP_ADD_TC(tp, check_sdp_put_int8);
	ATF_TP_ADD_TC(tp, check_sdp_put_int16);
	ATF_TP_ADD_TC(tp, check_sdp_put_int32);
	ATF_TP_ADD_TC(tp, check_sdp_put_int64);
	ATF_TP_ADD_TC(tp, check_sdp_put_seq);
	ATF_TP_ADD_TC(tp, check_sdp_put_alt);
	ATF_TP_ADD_TC(tp, check_sdp_put_str);
	ATF_TP_ADD_TC(tp, check_sdp_put_url);

	return atf_no_error();
}
