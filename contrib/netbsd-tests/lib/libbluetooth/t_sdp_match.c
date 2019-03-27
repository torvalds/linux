/*	$NetBSD: t_sdp_match.c,v 1.2 2011/04/07 08:29:50 plunky Exp $	*/

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

ATF_TC(check_sdp_match_uuid16);

ATF_TC_HEAD(check_sdp_match_uuid16, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test sdp_match_uuid16 results");
}

ATF_TC_BODY(check_sdp_match_uuid16, tc)
{
	uint8_t data[] = {
		0x19, 0x11, 0x11,	// uuid16	0x1111
		0x00,			// nil
		0x19, 0x12, 0x34,	// uuid16	0x1234
		0x1a, 0x00, 0x00, 0x34,	// uuid32	0x00003456
		0x56,
		0x1c, 0x00, 0x00, 0x43,	// uuid128	00004321-0000-1000-8000-00805f9b34fb
		0x21, 0x00, 0x00, 0x10,
		0x00, 0x80, 0x00, 0x00,
		0x80, 0x5f, 0x9b, 0x34,
		0xfb,
	};
	sdp_data_t test = { data, data + sizeof(data) };
	sdp_data_t nil;

	/*
	 * sdp_match_uuid16 advances if the UUID matches the 16-bit short alias given
	 */

	ATF_REQUIRE_EQ(sdp_match_uuid16(&test, 0x1100), false);	/* mismatch */
	ATF_REQUIRE(sdp_match_uuid16(&test, 0x1111));

	ATF_REQUIRE_EQ(sdp_match_uuid16(&test, 0x1234), false);	/* not uuid */
	ATF_REQUIRE(sdp_get_data(&test, &nil));			/* (skip) */
	ATF_CHECK_EQ(sdp_data_type(&nil), SDP_DATA_NIL);
	ATF_REQUIRE(sdp_match_uuid16(&test, 0x1234));

	ATF_REQUIRE(sdp_match_uuid16(&test, 0x3456));

	ATF_REQUIRE_EQ(sdp_match_uuid16(&test, 0x1234), false);	/* mismatch */
	ATF_REQUIRE(sdp_match_uuid16(&test, 0x4321));

	ATF_CHECK_EQ(test.next, test.end);
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_sdp_match_uuid16);

	return atf_no_error();
}
