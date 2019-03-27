/*	$NetBSD: t_sdp_data.c,v 1.2 2011/04/07 08:29:50 plunky Exp $	*/

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

#include <sdp.h>

ATF_TC(check_sdp_data_type);

ATF_TC_HEAD(check_sdp_data_type, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_data_type results");
}

ATF_TC_BODY(check_sdp_data_type, tc)
{
	uint8_t data[] = {
		0x00,			// nil
		0x08, 0x00,		// uint8	0x00
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t value;

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_NIL);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_type(&value), SDP_DATA_UINT8);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TC(check_sdp_data_size);

ATF_TC_HEAD(check_sdp_data_size, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_data_size results");
}

ATF_TC_BODY(check_sdp_data_size, tc)
{
	uint8_t data[] = {
		0x00,			// nil
		0x08, 0x00,		// uint8
		0x11, 0x00, 0x00,	// int16
		0x1a, 0x00, 0x00, 0x00,	// uuid32
		0x00,
		0x28, 0x00,		// bool
		0x25, 0x00,		// str8(0)
		0x25, 0x02, 0x00, 0x00,	// str8(2)
		0x36, 0x00, 0x00,	// seq16(0)
		0x3e, 0x00, 0x05, 0x00,	// alt16(5)
		0x00, 0x00, 0x00, 0x00,
		0x37, 0x00, 0x00, 0x00,	// seq32(0)
		0x00,
		0x47, 0x00, 0x00, 0x00,	// url32(7)
		0x07, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t value;

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 1);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 2);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 3);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 5);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 2);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 2);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 4);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 3);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 8);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 5);

	ATF_REQUIRE(sdp_get_data(&test, &value));
	ATF_CHECK_EQ(sdp_data_size(&value), 12);

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_sdp_data_type);
	ATF_TP_ADD_TC(tp, check_sdp_data_size);

	return atf_no_error();
}
