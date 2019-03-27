/*	$NetBSD: t_usbhid.c,v 1.12 2016/08/17 12:10:42 jakllsch Exp $	*/

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
__RCSID("$NetBSD: t_usbhid.c,v 1.12 2016/08/17 12:10:42 jakllsch Exp $");

#include <atf-c.h>

#include <inttypes.h>
#include <usbhid.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include <limits.h>

ATF_TC(check_hid_logical_range);
ATF_TC(check_hid_physical_range);
ATF_TC(check_hid_usage);
ATF_TC(check_hid_get_data);
ATF_TC(check_hid_set_data);
ATF_TC(check_parse_just_pop);

#define MYd_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== %d", (d))

#define MYu_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== %u", (d))

#define MYx_ATF_CHECK_EQ(d, v) \
	ATF_CHECK_EQ_MSG(d, v, "== 0x%x", (d))

#include "hid_test_data.c"

ATF_TC_HEAD(check_hid_usage, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test libusbhid usage.c");
}

ATF_TC_BODY(check_hid_usage, tc)
{
	char usages_path[PATH_MAX];

	(void)strlcpy(usages_path, atf_tc_get_config_var(tc, "srcdir"),
	    sizeof(usages_path));
	(void)strlcat(usages_path, "/test_usb_hid_usages",
	    sizeof(usages_path));

	hid_init(usages_path);

	ATF_CHECK_STREQ("t_usbhid_page", hid_usage_page(0xff1b));
	ATF_CHECK_EQ((uint32_t)hid_parse_usage_page("t_usbhid_page"), 0xff1b);

	ATF_CHECK_STREQ("t_usbhid_usage", hid_usage_in_page(0xff1bff2a));
	ATF_CHECK_EQ((uint32_t)hid_parse_usage_in_page(
	    "t_usbhid_page:t_usbhid_usage"), 0xff1bff2a);

	ATF_CHECK_STREQ("Quick_zephyrs_blow_vexing_daft_Jim_",
	    hid_usage_page(0xff2a));
	ATF_CHECK_EQ((uint32_t)hid_parse_usage_page(
	    "Quick_zephyrs_blow_vexing_daft_Jim_"), 0xff2a);

	ATF_CHECK_STREQ("Usage_ID_Zero_%", hid_usage_in_page(0xff2a0000));
	ATF_CHECK_EQ((uint32_t)hid_parse_usage_in_page(
	    "Quick_zephyrs_blow_vexing_daft_Jim_:Usage_ID_Zero_%"),
	    0xff2a0000);

	//ATF_CHECK_STREQ("Usage_ID_0_%", hid_usage_in_page(0xff2a0000));
	ATF_CHECK_EQ((uint32_t)hid_parse_usage_in_page(
	    "Quick_zephyrs_blow_vexing_daft_Jim_:Usage_ID_0_%"), 0xff2a0000);

	ATF_CHECK_STREQ("Usage_ID_65535_%", hid_usage_in_page(0xff2affff));
	ATF_CHECK_EQ((uint32_t)hid_parse_usage_in_page(
	    "Quick_zephyrs_blow_vexing_daft_Jim_:Usage_ID_65535_%"),
	    0xff2affff);

	MYx_ATF_CHECK_EQ((uint32_t)hid_parse_usage_in_page("0xff2a:0xff1b"),
	    0xff2aff1b);
}

ATF_TC_HEAD(check_hid_logical_range, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test hid_get_item "
	    "Logical Minimum/Maximum results");
}

ATF_TC_BODY(check_hid_logical_range, tc)
{
	report_desc_t hrd;
	hid_item_t hi;
	uint32_t minimum, maximum;

	atf_tc_expect_fail("only the 32-bit opcode works, "
	    "8 and 16-bit is broken");

	ATF_REQUIRE((hrd = hid_use_report_desc(range_test_report_descriptor,
	    __arraycount(range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000001U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	MYd_ATF_CHECK_EQ(hi.logical_minimum, -128);
	MYd_ATF_CHECK_EQ(hi.logical_maximum, 127);
	ATF_REQUIRE(hid_locate(hrd, 0xff000002U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	MYd_ATF_CHECK_EQ(hi.logical_minimum, -32768);
	MYd_ATF_CHECK_EQ(hi.logical_maximum, 32767);
	ATF_REQUIRE(hid_locate(hrd, 0xff000003U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	MYd_ATF_CHECK_EQ(hi.logical_minimum, -2147483648);
	MYd_ATF_CHECK_EQ(hi.logical_maximum, 2147483647);

	hid_dispose_report_desc(hrd);
	hrd = NULL;

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    unsigned_range_test_report_descriptor,
	    __arraycount(unsigned_range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000011U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	ATF_CHECK(hi.logical_minimum > hi.logical_maximum);
	minimum = (uint32_t)hi.logical_minimum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(minimum, 0);
	maximum = (uint32_t)hi.logical_maximum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(maximum, 255);
	ATF_REQUIRE(hid_locate(hrd, 0xff000012U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	ATF_CHECK(hi.logical_minimum > hi.logical_maximum);
	minimum = hi.logical_minimum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(minimum, 0);
	maximum = hi.logical_maximum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(maximum, 65535);
	ATF_REQUIRE(hid_locate(hrd, 0xff000013U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	ATF_CHECK(hi.logical_minimum > hi.logical_maximum);
	minimum = hi.logical_minimum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(minimum, 0);
	maximum = hi.logical_maximum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(maximum, 4294967295);

	hid_dispose_report_desc(hrd);
	hrd = NULL;
}

ATF_TC_HEAD(check_hid_physical_range, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test hid_get_item "
	    "Physical Minimum/Maximum results");
}

ATF_TC_BODY(check_hid_physical_range, tc)
{
	report_desc_t hrd;
	hid_item_t hi;
	uint32_t minimum, maximum;

	atf_tc_expect_fail("only the 32-bit opcode works, "
	    "8 and 16-bit is broken");

	ATF_REQUIRE((hrd = hid_use_report_desc(range_test_report_descriptor,
	    __arraycount(range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000001U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	MYd_ATF_CHECK_EQ(hi.physical_minimum, -128);
	MYd_ATF_CHECK_EQ(hi.physical_maximum, 127);
	ATF_REQUIRE(hid_locate(hrd, 0xff000002U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	MYd_ATF_CHECK_EQ(hi.physical_minimum, -32768);
	MYd_ATF_CHECK_EQ(hi.physical_maximum, 32767);
	ATF_REQUIRE(hid_locate(hrd, 0xff000003U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	MYd_ATF_CHECK_EQ(hi.physical_minimum, -2147483648);
	MYd_ATF_CHECK_EQ(hi.physical_maximum, 2147483647);

	hid_dispose_report_desc(hrd);
	hrd = NULL;

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    unsigned_range_test_report_descriptor,
	    __arraycount(unsigned_range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000011U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	ATF_CHECK(hi.physical_minimum > hi.physical_maximum);
	minimum = (uint32_t)hi.physical_minimum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(minimum, 0);
	maximum = (uint32_t)hi.physical_maximum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(maximum, 255);
	ATF_REQUIRE(hid_locate(hrd, 0xff000012U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	ATF_CHECK(hi.physical_minimum > hi.physical_maximum);
	minimum = hi.physical_minimum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(minimum, 0);
	maximum = hi.physical_maximum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(maximum, 65535);
	ATF_REQUIRE(hid_locate(hrd, 0xff000013U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	ATF_CHECK(hi.physical_minimum > hi.physical_maximum);
	minimum = hi.physical_minimum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(minimum, 0);
	maximum = hi.physical_maximum & ((1ULL<<hi.report_size)-1);
	MYu_ATF_CHECK_EQ(maximum, 4294967295);

	hid_dispose_report_desc(hrd);
	hrd = NULL;
}

ATF_TC_HEAD(check_hid_get_data, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test hid_get_data results");
}

ATF_TC_BODY(check_hid_get_data, tc)
{
	report_desc_t hrd;
	hid_item_t hi;
	int32_t data;
	uint32_t udat;

	atf_tc_expect_fail("only the 32-bit opcode works, "
	    "8 and 16-bit is broken");

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    range_test_report_descriptor,
	    __arraycount(range_test_report_descriptor))) != NULL);

	ATF_REQUIRE(hid_locate(hrd, 0xff000001U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	data = hid_get_data(range_test_minimum_report, &hi);
	MYd_ATF_CHECK_EQ(data, -128);
	data = hid_get_data(range_test_negative_one_report, &hi);
	MYd_ATF_CHECK_EQ(data, -1);
	data = hid_get_data(range_test_positive_one_report, &hi);
	MYd_ATF_CHECK_EQ(data, 1);
	data = hid_get_data(range_test_maximum_report, &hi);
	MYd_ATF_CHECK_EQ(data, 127);

	ATF_REQUIRE(hid_locate(hrd, 0xff000002U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	data = hid_get_data(range_test_minimum_report, &hi);
	MYd_ATF_CHECK_EQ(data, -32768);
	data = hid_get_data(range_test_negative_one_report, &hi);
	MYd_ATF_CHECK_EQ(data, -1);
	data = hid_get_data(range_test_positive_one_report, &hi);
	MYd_ATF_CHECK_EQ(data, 1);
	data = hid_get_data(range_test_maximum_report, &hi);
	MYd_ATF_CHECK_EQ(data, 32767);

	ATF_REQUIRE(hid_locate(hrd, 0xff000003U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	data = hid_get_data(range_test_minimum_report, &hi);
	MYd_ATF_CHECK_EQ(data, -2147483648);
	data = hid_get_data(range_test_negative_one_report, &hi);
	MYd_ATF_CHECK_EQ(data, -1);
	data = hid_get_data(range_test_positive_one_report, &hi);
	MYd_ATF_CHECK_EQ(data, 1);
	data = hid_get_data(range_test_maximum_report, &hi);
	MYd_ATF_CHECK_EQ(data, 2147483647);

	hid_dispose_report_desc(hrd);
	hrd = NULL;

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    unsigned_range_test_report_descriptor,
	    __arraycount(unsigned_range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000011U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	udat = hid_get_data(unsigned_range_test_minimum_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 0);
	udat = hid_get_data(unsigned_range_test_positive_one_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 1);
	udat = hid_get_data(unsigned_range_test_negative_one_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 254);
	udat = hid_get_data(unsigned_range_test_maximum_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 255);

	ATF_REQUIRE(hid_locate(hrd, 0xff000012U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	udat = hid_get_data(unsigned_range_test_minimum_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 0);
	udat = hid_get_data(unsigned_range_test_positive_one_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 1);
	udat = hid_get_data(unsigned_range_test_negative_one_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 65534);
	udat = hid_get_data(unsigned_range_test_maximum_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 65535);

	ATF_REQUIRE(hid_locate(hrd, 0xff000013U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	udat = hid_get_data(unsigned_range_test_minimum_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 0);
	udat = hid_get_data(unsigned_range_test_positive_one_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 1);
	udat = hid_get_data(unsigned_range_test_negative_one_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 4294967294);
	udat = hid_get_data(unsigned_range_test_maximum_report, &hi);
	MYu_ATF_CHECK_EQ(udat, 4294967295);

	hid_dispose_report_desc(hrd);
	hrd = NULL;
}

ATF_TC_HEAD(check_hid_set_data, tc)
{

	atf_tc_set_md_var(tc, "descr", "Test hid_set_data results");
}

ATF_TC_BODY(check_hid_set_data, tc)
{
	report_desc_t hrd;
	hid_item_t hi;
	uint8_t test_data_minimum[7];
	uint8_t test_data_negative_one[7];
	uint8_t test_data_positive_one[7];
	uint8_t test_data_maximum[7];

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    range_test_report_descriptor,
	    __arraycount(range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000001U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	hid_set_data(test_data_minimum, &hi, -128);
	hid_set_data(test_data_negative_one, &hi, -1);
	hid_set_data(test_data_positive_one, &hi, 1);
	hid_set_data(test_data_maximum, &hi, 127);
	ATF_REQUIRE(hid_locate(hrd, 0xff000002U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	hid_set_data(test_data_minimum, &hi, -32768);
	hid_set_data(test_data_negative_one, &hi, -1);
	hid_set_data(test_data_positive_one, &hi, 1);
	hid_set_data(test_data_maximum, &hi, 32767);
	ATF_REQUIRE(hid_locate(hrd, 0xff000003U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	hid_set_data(test_data_minimum, &hi, -2147483648);
	hid_set_data(test_data_negative_one, &hi, -1);
	hid_set_data(test_data_positive_one, &hi, 1);
	hid_set_data(test_data_maximum, &hi, 2147483647);
	ATF_CHECK(memcmp(test_data_minimum, range_test_minimum_report,
	    sizeof test_data_minimum) == 0);
	ATF_CHECK(memcmp(test_data_negative_one,
	    range_test_negative_one_report,
	    sizeof test_data_negative_one) == 0);
	ATF_CHECK(memcmp(test_data_positive_one,
	    range_test_positive_one_report,
	    sizeof test_data_positive_one) == 0);
	ATF_CHECK(memcmp(test_data_maximum, range_test_maximum_report,
	    sizeof test_data_maximum) == 0);

	hid_dispose_report_desc(hrd);
	hrd = NULL;

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    unsigned_range_test_report_descriptor,
	    __arraycount(range_test_report_descriptor))) != NULL);
	ATF_REQUIRE(hid_locate(hrd, 0xff000011U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	hid_set_data(test_data_minimum, &hi, 0);
	hid_set_data(test_data_positive_one, &hi, 1);
	hid_set_data(test_data_negative_one, &hi, 0xfffffffe);
	hid_set_data(test_data_maximum, &hi, 0xffffffff);
	ATF_REQUIRE(hid_locate(hrd, 0xff000012U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	hid_set_data(test_data_minimum, &hi, 0);
	hid_set_data(test_data_positive_one, &hi, 1);
	hid_set_data(test_data_negative_one, &hi, 0xfffe);
	hid_set_data(test_data_maximum, &hi, 0xffff);
	ATF_REQUIRE(hid_locate(hrd, 0xff000013U, hid_input, &hi,
	    NO_REPORT_ID) > 0);
	hid_set_data(test_data_minimum, &hi, 0);
	hid_set_data(test_data_positive_one, &hi, 1);
	hid_set_data(test_data_negative_one, &hi, 0xfffffffe);
	hid_set_data(test_data_maximum, &hi, 0xffffffff);
	ATF_CHECK(memcmp(test_data_minimum,
	    unsigned_range_test_minimum_report,
	    sizeof test_data_minimum) == 0);
	ATF_CHECK(memcmp(test_data_negative_one,
	    unsigned_range_test_negative_one_report,
	    sizeof test_data_negative_one) == 0);
	ATF_CHECK(memcmp(test_data_positive_one,
	    unsigned_range_test_positive_one_report,
	    sizeof test_data_positive_one) == 0);
	ATF_CHECK(memcmp(test_data_maximum,
	    unsigned_range_test_maximum_report,
	    sizeof test_data_maximum) == 0);

	hid_dispose_report_desc(hrd);
	hrd = NULL;
}

ATF_TC_HEAD(check_parse_just_pop, tc)
{

	atf_tc_set_md_var(tc, "descr", "check Pop on empty stack bug");
}

ATF_TC_BODY(check_parse_just_pop, tc)
{
	report_desc_t hrd;
	hid_data_t hd;
	hid_item_t hi;

	ATF_REQUIRE((hrd = hid_use_report_desc(
	    just_pop_report_descriptor,
	    sizeof just_pop_report_descriptor)) != NULL);
	hd = hid_start_parse(hrd, 0, NO_REPORT_ID);
	while (hid_get_item(hd, &hi) > 0) {
	}
	hid_end_parse(hd);
	hid_dispose_report_desc(hrd);
	hrd = NULL;
}

ATF_TP_ADD_TCS(tp)
{

	ATF_TP_ADD_TC(tp, check_hid_logical_range);
	ATF_TP_ADD_TC(tp, check_hid_physical_range);
	ATF_TP_ADD_TC(tp, check_hid_usage);
	ATF_TP_ADD_TC(tp, check_hid_get_data);
	ATF_TP_ADD_TC(tp, check_hid_set_data);
	ATF_TP_ADD_TC(tp, check_parse_just_pop);

	return atf_no_error();
}
