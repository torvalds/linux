/*
 * common module tests
 * Copyright (c) 2014-2015, Jouni Malinen <j@w1.fi>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/module_tests.h"
#include "ieee802_11_common.h"
#include "ieee802_11_defs.h"
#include "gas.h"
#include "wpa_common.h"


struct ieee802_11_parse_test_data {
	u8 *data;
	size_t len;
	ParseRes result;
	int count;
};

static const struct ieee802_11_parse_test_data parse_tests[] = {
	{ (u8 *) "", 0, ParseOK, 0 },
	{ (u8 *) " ", 1, ParseFailed, 0 },
	{ (u8 *) "\xff\x00", 2, ParseUnknown, 1 },
	{ (u8 *) "\xff\x01", 2, ParseFailed, 0 },
	{ (u8 *) "\xdd\x03\x01\x02\x03", 5, ParseUnknown, 1 },
	{ (u8 *) "\xdd\x04\x01\x02\x03\x04", 6, ParseUnknown, 1 },
	{ (u8 *) "\xdd\x04\x00\x50\xf2\x02", 6, ParseUnknown, 1 },
	{ (u8 *) "\xdd\x05\x00\x50\xf2\x02\x02", 7, ParseOK, 1 },
	{ (u8 *) "\xdd\x05\x00\x50\xf2\x02\xff", 7, ParseUnknown, 1 },
	{ (u8 *) "\xdd\x04\x00\x50\xf2\xff", 6, ParseUnknown, 1 },
	{ (u8 *) "\xdd\x04\x50\x6f\x9a\xff", 6, ParseUnknown, 1 },
	{ (u8 *) "\xdd\x04\x00\x90\x4c\x33", 6, ParseOK, 1 },
	{ (u8 *) "\xdd\x04\x00\x90\x4c\xff\xdd\x04\x00\x90\x4c\x33", 12,
	  ParseUnknown, 2 },
	{ (u8 *) "\x10\x01\x00\x21\x00", 5, ParseOK, 2 },
	{ (u8 *) "\x24\x00", 2, ParseOK, 1 },
	{ (u8 *) "\x38\x00", 2, ParseOK, 1 },
	{ (u8 *) "\x54\x00", 2, ParseOK, 1 },
	{ (u8 *) "\x5a\x00", 2, ParseOK, 1 },
	{ (u8 *) "\x65\x00", 2, ParseOK, 1 },
	{ (u8 *) "\x65\x12\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0a\x0b\x0c\x0d\x0e\x0f\x10\x11",
	  20, ParseOK, 1 },
	{ (u8 *) "\x6e\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xc7\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xc7\x01\x00", 3, ParseOK, 1 },
	{ (u8 *) "\x03\x00\x2a\x00\x36\x00\x37\x00\x38\x00\x2d\x00\x3d\x00\xbf\x00\xc0\x00",
	  18, ParseOK, 9 },
	{ (u8 *) "\x8b\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xdd\x04\x00\x90\x4c\x04", 6, ParseUnknown, 1 },
	{ (u8 *) "\xed\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xef\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xef\x01\x11", 3, ParseOK, 1 },
	{ (u8 *) "\xf0\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xf1\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xf1\x02\x11\x22", 4, ParseOK, 1 },
	{ (u8 *) "\xf2\x00", 2, ParseOK, 1 },
	{ (u8 *) "\xff\x00", 2, ParseUnknown, 1 },
	{ (u8 *) "\xff\x01\x00", 3, ParseUnknown, 1 },
	{ (u8 *) "\xff\x01\x01", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x02\x01\x00", 4, ParseOK, 1 },
	{ (u8 *) "\xff\x01\x02", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x04\x02\x11\x22\x33", 6, ParseOK, 1 },
	{ (u8 *) "\xff\x01\x04", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x01\x05", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x0d\x05\x11\x22\x33\x44\x55\x55\x11\x22\x33\x44\x55\x55",
	  15, ParseOK, 1 },
	{ (u8 *) "\xff\x01\x06", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x02\x06\x00", 4, ParseOK, 1 },
	{ (u8 *) "\xff\x01\x07", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x09\x07\x11\x22\x33\x44\x55\x66\x77\x88", 11,
	  ParseOK, 1 },
	{ (u8 *) "\xff\x01\x0c", 3, ParseOK, 1 },
	{ (u8 *) "\xff\x02\x0c\x00", 4, ParseOK, 1 },
	{ (u8 *) "\xff\x01\x0d", 3, ParseOK, 1 },
	{ NULL, 0, ParseOK, 0 }
};

static int ieee802_11_parse_tests(void)
{
	int i, ret = 0;
	struct wpabuf *buf;

	wpa_printf(MSG_INFO, "ieee802_11_parse tests");

	for (i = 0; parse_tests[i].data; i++) {
		const struct ieee802_11_parse_test_data *test;
		struct ieee802_11_elems elems;
		ParseRes res;

		test = &parse_tests[i];
		res = ieee802_11_parse_elems(test->data, test->len, &elems, 1);
		if (res != test->result ||
		    ieee802_11_ie_count(test->data, test->len) != test->count) {
			wpa_printf(MSG_ERROR, "ieee802_11_parse test %d failed",
				   i);
			ret = -1;
		}
	}

	if (ieee802_11_vendor_ie_concat((const u8 *) "\x00\x01", 2, 0) != NULL)
	{
		wpa_printf(MSG_ERROR,
			   "ieee802_11_vendor_ie_concat test failed");
		ret = -1;
	}

	buf = ieee802_11_vendor_ie_concat((const u8 *) "\xdd\x05\x11\x22\x33\x44\x01\xdd\x05\x11\x22\x33\x44\x02\x00\x01",
					  16, 0x11223344);
	do {
		const u8 *pos;

		if (!buf) {
			wpa_printf(MSG_ERROR,
				   "ieee802_11_vendor_ie_concat test 2 failed");
			ret = -1;
			break;
		}

		if (wpabuf_len(buf) != 2) {
			wpa_printf(MSG_ERROR,
				   "ieee802_11_vendor_ie_concat test 3 failed");
			ret = -1;
			break;
		}

		pos = wpabuf_head(buf);
		if (pos[0] != 0x01 || pos[1] != 0x02) {
			wpa_printf(MSG_ERROR,
				   "ieee802_11_vendor_ie_concat test 3 failed");
			ret = -1;
			break;
		}
	} while (0);
	wpabuf_free(buf);

	return ret;
}


struct rsn_ie_parse_test_data {
	u8 *data;
	size_t len;
	int result;
};

static const struct rsn_ie_parse_test_data rsn_parse_tests[] = {
	{ (u8 *) "", 0, -1 },
	{ (u8 *) "\x30\x00", 2, -1 },
	{ (u8 *) "\x30\x02\x01\x00", 4, 0 },
	{ (u8 *) "\x30\x02\x00\x00", 4, -2 },
	{ (u8 *) "\x30\x02\x02\x00", 4, -2 },
	{ (u8 *) "\x30\x02\x00\x01", 4, -2 },
	{ (u8 *) "\x30\x02\x00\x00\x00", 5, -2 },
	{ (u8 *) "\x30\x03\x01\x00\x00", 5, -3 },
	{ (u8 *) "\x30\x06\x01\x00\x00\x00\x00\x00", 8, -1 },
	{ (u8 *) "\x30\x06\x01\x00\x00\x0f\xac\x04", 8, 0 },
	{ (u8 *) "\x30\x07\x01\x00\x00\x0f\xac\x04\x00", 9, -5 },
	{ (u8 *) "\x30\x08\x01\x00\x00\x0f\xac\x04\x00\x00", 10, -4 },
	{ (u8 *) "\x30\x08\x01\x00\x00\x0f\xac\x04\x00\x01", 10, -4 },
	{ (u8 *) "\x30\x0c\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04",
	  14, 0 },
	{ (u8 *) "\x30\x0c\x01\x00\x00\x0f\xac\x04\x00\x01\x00\x0f\xac\x04",
	  14, -4 },
	{ (u8 *) "\x30\x0c\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x06",
	  14, -1 },
	{ (u8 *) "\x30\x10\x01\x00\x00\x0f\xac\x04\x02\x00\x00\x0f\xac\x04\x00\x0f\xac\x08",
	  18, 0 },
	{ (u8 *) "\x30\x0d\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x00",
	  15, -7 },
	{ (u8 *) "\x30\x0e\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x00\x00",
	  16, -6 },
	{ (u8 *) "\x30\x0e\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x00\x01",
	  16, -6 },
	{ (u8 *) "\x30\x12\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01",
	  20, 0 },
	{ (u8 *) "\x30\x16\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x02\x00\x00\x0f\xac\x01\x00\x0f\xac\x02",
	  24, 0 },
	{ (u8 *) "\x30\x13\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00",
	  21, 0 },
	{ (u8 *) "\x30\x14\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00\x00",
	  22, 0 },
	{ (u8 *) "\x30\x16\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00\x00\x00\x00",
	  24, 0 },
	{ (u8 *) "\x30\x16\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00\x00\x00\x01",
	  24, -9 },
	{ (u8 *) "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00\x00\x00\x00\x00\x00\x00\x00",
	  28, -10 },
	{ (u8 *) "\x30\x1a\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00\x00\x00\x00\x00\x0f\xac\x06",
	  28, 0 },
	{ (u8 *) "\x30\x1c\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x04\x01\x00\x00\x0f\xac\x01\x00\x00\x00\x00\x00\x0f\xac\x06\x01\x02",
	  30, 0 },
	{ NULL, 0, 0 }
};

static int rsn_ie_parse_tests(void)
{
	int i, ret = 0;

	wpa_printf(MSG_INFO, "rsn_ie_parse tests");

	for (i = 0; rsn_parse_tests[i].data; i++) {
		const struct rsn_ie_parse_test_data *test;
		struct wpa_ie_data data;

		test = &rsn_parse_tests[i];
		if (wpa_parse_wpa_ie_rsn(test->data, test->len, &data) !=
		    test->result) {
			wpa_printf(MSG_ERROR, "rsn_ie_parse test %d failed", i);
			ret = -1;
		}
	}

	return ret;
}


static int gas_tests(void)
{
	struct wpabuf *buf;

	wpa_printf(MSG_INFO, "gas tests");
	gas_anqp_set_len(NULL);

	buf = wpabuf_alloc(1);
	if (buf == NULL)
		return -1;
	gas_anqp_set_len(buf);
	wpabuf_free(buf);

	buf = wpabuf_alloc(20);
	if (buf == NULL)
		return -1;
	wpabuf_put_u8(buf, WLAN_ACTION_PUBLIC);
	wpabuf_put_u8(buf, WLAN_PA_GAS_INITIAL_REQ);
	wpabuf_put_u8(buf, 0);
	wpabuf_put_be32(buf, 0);
	wpabuf_put_u8(buf, 0);
	gas_anqp_set_len(buf);
	wpabuf_free(buf);

	return 0;
}


int common_module_tests(void)
{
	int ret = 0;

	wpa_printf(MSG_INFO, "common module tests");

	if (ieee802_11_parse_tests() < 0 ||
	    gas_tests() < 0 ||
	    rsn_ie_parse_tests() < 0)
		ret = -1;

	return ret;
}
