/*	$NetBSD: t_hid.c,v 1.8 2016/05/05 17:40:26 jakllsch Exp $	*/

/*
 * Copyright (c) 2016 Jonathan A. Kollasch
 * All rights reserved.
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
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 * TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_hid.c,v 1.8 2016/05/05 17:40:26 jakllsch Exp $");

#include <machine/types.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <atf-c.h>

#include <rump/rump.h>

#define hid_start_parse rumpns_hid_start_parse
#define hid_end_parse rumpns_hid_end_parse
#define hid_get_item rumpns_hid_get_item
#define hid_locate rumpns_hid_locate
#define hid_report_size rumpns_hid_report_size
#define hid_get_data rumpns_hid_get_data
#define hid_get_udata rumpns_hid_get_udata
#define uhidevdebug rumpns_uhidevdebug
#include <usb.h>
#include <usbhid.h>
#include <hid.h>

#include "../../lib/libusbhid/hid_test_data.c"

#define MYd_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== %d", (d))

#define MYld_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== %ld", (d))

#define MYu_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== %u", (d))

#define MYlu_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== %lu", (d))

#define MYx_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== 0x%x", (d))

#define MYlx_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== 0x%lx", (d))

int uhidevdebug;

ATF_TC(khid);

ATF_TC_HEAD(khid, tc)
{

        atf_tc_set_md_var(tc, "descr", "check kernel hid.c");
}

static int
locate_item(const void *desc, int size, u_int32_t u, u_int8_t id,
    enum hid_kind k, struct hid_item *hip)
{
	struct hid_data *d;
	struct hid_item h;

	h.report_ID = 0;
	for (d = hid_start_parse(desc, size, k); hid_get_item(d, &h); ) {
		if (h.kind == k && !(h.flags & HIO_CONST) &&
		    (/*XXX*/uint32_t)h.usage == u && h.report_ID == id) {
			if (hip != NULL)
				*hip = h;
			hid_end_parse(d);
			return (1);
		}
	}
	hid_end_parse(d);
	return (0);
}

ATF_TC_BODY(khid, tc)
{
	int ret;
	struct hid_item hi;

	uhidevdebug = 0;

	rump_init();

	rump_schedule();

	ret = locate_item(range_test_report_descriptor,
	    sizeof(range_test_report_descriptor), 0xff000003, 0, hid_input,
	    &hi);
	ATF_REQUIRE(ret > 0);
	MYu_ATF_CHECK_EQ(hi.loc.size, 32);
	MYu_ATF_CHECK_EQ(hi.loc.count, 1);
	MYu_ATF_CHECK_EQ(hi.loc.pos, 0);
	MYx_ATF_CHECK_EQ(hi.flags, 0);
	MYd_ATF_CHECK_EQ(hi.logical_minimum, -2147483648);
	MYd_ATF_CHECK_EQ(hi.logical_maximum, 2147483647);
	MYd_ATF_CHECK_EQ(hi.physical_minimum, -2147483648);
	MYd_ATF_CHECK_EQ(hi.physical_maximum, 2147483647);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_minimum_report,
	    &hi.loc), -2147483648);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_negative_one_report,
	    &hi.loc), -1);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_positive_one_report,
	    &hi.loc), 1);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_maximum_report,
	    &hi.loc), 2147483647);

	ret = locate_item(range_test_report_descriptor,
	    sizeof(range_test_report_descriptor), 0xff000002, 0, hid_input,
	    &hi);
	ATF_REQUIRE(ret > 0);
	MYu_ATF_CHECK_EQ(hi.loc.size, 16);
	MYu_ATF_CHECK_EQ(hi.loc.count, 1);
	MYu_ATF_CHECK_EQ(hi.loc.pos, 32);
	MYx_ATF_CHECK_EQ(hi.flags, 0);
	MYd_ATF_CHECK_EQ(hi.logical_minimum, -32768);
	MYd_ATF_CHECK_EQ(hi.logical_maximum, 32767);
	MYd_ATF_CHECK_EQ(hi.physical_minimum, -32768);
	MYd_ATF_CHECK_EQ(hi.physical_maximum, 32767);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_minimum_report,
	    &hi.loc), -32768);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_negative_one_report,
	    &hi.loc), -1);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_positive_one_report,
	    &hi.loc), 1);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_maximum_report,
	    &hi.loc), 32767);

	ret = locate_item(range_test_report_descriptor,
	    sizeof(range_test_report_descriptor), 0xff000001, 0, hid_input,
	    &hi);
	ATF_REQUIRE(ret > 0);
	MYu_ATF_CHECK_EQ(hi.loc.size, 8);
	MYu_ATF_CHECK_EQ(hi.loc.count, 1);
	MYu_ATF_CHECK_EQ(hi.loc.pos, 48);
	MYx_ATF_CHECK_EQ(hi.flags, 0);
	MYd_ATF_CHECK_EQ(hi.logical_minimum, -128);
	MYd_ATF_CHECK_EQ(hi.logical_maximum, 127);
	MYd_ATF_CHECK_EQ(hi.physical_minimum, -128);
	MYd_ATF_CHECK_EQ(hi.physical_maximum, 127);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_minimum_report,
	    &hi.loc), -128);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_negative_one_report,
	    &hi.loc), -1);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_positive_one_report,
	    &hi.loc), 1);
	MYld_ATF_CHECK_EQ(hid_get_data(range_test_maximum_report,
	    &hi.loc), 127);


	ret = locate_item(unsigned_range_test_report_descriptor,
	    sizeof(unsigned_range_test_report_descriptor), 0xff000013, 0,
	    hid_input, &hi);
	ATF_REQUIRE(ret > 0);
	MYu_ATF_CHECK_EQ(hi.loc.size, 32);
	MYu_ATF_CHECK_EQ(hi.loc.count, 1);
	MYu_ATF_CHECK_EQ(hi.loc.pos, 0);
	MYx_ATF_CHECK_EQ(hi.flags, 0);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_minimum_report,
	    &hi.loc), 0x0);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_positive_one_report,
	    &hi.loc), 0x1);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_negative_one_report,
	    &hi.loc), 0xfffffffe);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_maximum_report,
	    &hi.loc), 0xffffffff);

	ret = locate_item(unsigned_range_test_report_descriptor,
	    sizeof(unsigned_range_test_report_descriptor), 0xff000012, 0,
	    hid_input, &hi);
	ATF_REQUIRE(ret > 0);
	MYu_ATF_CHECK_EQ(hi.loc.size, 16);
	MYu_ATF_CHECK_EQ(hi.loc.count, 1);
	MYu_ATF_CHECK_EQ(hi.loc.pos, 32);
	MYx_ATF_CHECK_EQ(hi.flags, 0);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_minimum_report,
	    &hi.loc), 0x0);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_positive_one_report,
	    &hi.loc), 0x1);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_negative_one_report,
	    &hi.loc), 0xfffe);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_maximum_report,
	    &hi.loc), 0xffff);

	ret = locate_item(unsigned_range_test_report_descriptor,
	    sizeof(unsigned_range_test_report_descriptor), 0xff000011, 0,
	    hid_input, &hi);
	ATF_REQUIRE(ret > 0);
	MYu_ATF_CHECK_EQ(hi.loc.size, 8);
	MYu_ATF_CHECK_EQ(hi.loc.count, 1);
	MYu_ATF_CHECK_EQ(hi.loc.pos, 48);
	MYx_ATF_CHECK_EQ(hi.flags, 0);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_minimum_report,
	    &hi.loc), 0x0);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_positive_one_report,
	    &hi.loc), 0x1);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_negative_one_report,
	    &hi.loc), 0xfe);
	MYlx_ATF_CHECK_EQ(hid_get_udata(unsigned_range_test_maximum_report,
	    &hi.loc), 0xff);

	rump_unschedule();
}

ATF_TC(khid_parse_just_pop);

ATF_TC_HEAD(khid_parse_just_pop, tc)
{

        atf_tc_set_md_var(tc, "descr", "check kernel hid.c for "
	    "Pop on empty stack bug");
}

ATF_TC_BODY(khid_parse_just_pop, tc)
{
	struct hid_data *hdp;
	struct hid_item hi;

	rump_init();

	rump_schedule();

	hdp = hid_start_parse(just_pop_report_descriptor,
	    sizeof just_pop_report_descriptor, hid_none);
	while (hid_get_item(hdp, &hi) > 0) {
	}
	hid_end_parse(hdp);

	rump_unschedule();
}

ATF_TP_ADD_TCS(tp)
{

        ATF_TP_ADD_TC(tp, khid);
        ATF_TP_ADD_TC(tp, khid_parse_just_pop);

	return atf_no_error();
}

