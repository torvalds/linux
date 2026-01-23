// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * KUnit test for the ACPI-WMI string conversion code.
 *
 * Copyright (C) 2025 Armin Wolf <W_Armin@gmx.de>
 */

#include <linux/module.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/wmi.h>

#include <kunit/resource.h>
#include <kunit/test.h>

#include <asm/byteorder.h>

struct wmi_string_param {
	const char *name;
	const struct wmi_string *wmi_string;
	/*
	 * Remember that using sizeof() on a struct wmi_string will
	 * always return a size of two bytes due to the flexible
	 * array member!
	 */
	size_t wmi_string_length;
	const u8 *utf8_string;
	size_t utf8_string_length;
};

#define TEST_WMI_STRING_LENGTH 12

static const struct wmi_string test_wmi_string = {
	.length = cpu_to_le16(10),
	.chars = {
		cpu_to_le16(u'T'),
		cpu_to_le16(u'E'),
		cpu_to_le16(u'S'),
		cpu_to_le16(u'T'),
		cpu_to_le16(u'\0'),
	},
};

static const u8 test_utf8_string[] = "TEST";

#define SPECIAL_WMI_STRING_LENGTH 14

static const struct wmi_string special_wmi_string = {
	.length = cpu_to_le16(12),
	.chars = {
		cpu_to_le16(u'Ä'),
		cpu_to_le16(u'Ö'),
		cpu_to_le16(u'Ü'),
		cpu_to_le16(u'ß'),
		cpu_to_le16(u'€'),
		cpu_to_le16(u'\0'),
	},
};

static const u8 special_utf8_string[] = "ÄÖÜß€";

#define MULTI_POINT_WMI_STRING_LENGTH 12

static const struct wmi_string multi_point_wmi_string = {
	.length = cpu_to_le16(10),
	.chars = {
		cpu_to_le16(u'K'),
		/* 🐧 */
		cpu_to_le16(0xD83D),
		cpu_to_le16(0xDC27),
		cpu_to_le16(u'!'),
		cpu_to_le16(u'\0'),
	},
};

static const u8 multi_point_utf8_string[] = "K🐧!";

#define PADDED_TEST_WMI_STRING_LENGTH 14

static const struct wmi_string padded_test_wmi_string = {
	.length = cpu_to_le16(12),
	.chars = {
		cpu_to_le16(u'T'),
		cpu_to_le16(u'E'),
		cpu_to_le16(u'S'),
		cpu_to_le16(u'T'),
		cpu_to_le16(u'\0'),
		cpu_to_le16(u'\0'),
	},
};

static const u8 padded_test_utf8_string[] = "TEST\0";

#define OVERSIZED_TEST_WMI_STRING_LENGTH 14

static const struct wmi_string oversized_test_wmi_string = {
	.length = cpu_to_le16(8),
	.chars = {
		cpu_to_le16(u'T'),
		cpu_to_le16(u'E'),
		cpu_to_le16(u'S'),
		cpu_to_le16(u'T'),
		cpu_to_le16(u'!'),
		cpu_to_le16(u'\0'),
	},
};

static const u8 oversized_test_utf8_string[] = "TEST!";

#define INVALID_TEST_WMI_STRING_LENGTH 14

static const struct wmi_string invalid_test_wmi_string = {
	.length = cpu_to_le16(12),
	.chars = {
		cpu_to_le16(u'T'),
		/* 🐧, with low surrogate missing */
		cpu_to_le16(0xD83D),
		cpu_to_le16(u'E'),
		cpu_to_le16(u'S'),
		cpu_to_le16(u'T'),
		cpu_to_le16(u'\0'),
	},
};

/* We have to split the string here to end the hex escape sequence */
static const u8 invalid_test_utf8_string[] = "T" "\xF0\x9F" "EST";

static const struct wmi_string_param wmi_string_params_array[] = {
	{
		.name = "ascii_string",
		.wmi_string = &test_wmi_string,
		.wmi_string_length = TEST_WMI_STRING_LENGTH,
		.utf8_string = test_utf8_string,
		.utf8_string_length = sizeof(test_utf8_string),
	},
	{
		.name = "special_string",
		.wmi_string = &special_wmi_string,
		.wmi_string_length = SPECIAL_WMI_STRING_LENGTH,
		.utf8_string = special_utf8_string,
		.utf8_string_length = sizeof(special_utf8_string),
	},
	{
		.name = "multi_point_string",
		.wmi_string = &multi_point_wmi_string,
		.wmi_string_length = MULTI_POINT_WMI_STRING_LENGTH,
		.utf8_string = multi_point_utf8_string,
		.utf8_string_length = sizeof(multi_point_utf8_string),
	},
};

static void wmi_string_param_get_desc(const struct wmi_string_param *param, char *desc)
{
	strscpy(desc, param->name, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(wmi_string, wmi_string_params_array, wmi_string_param_get_desc);

static void wmi_string_to_utf8s_test(struct kunit *test)
{
	const struct wmi_string_param *param = test->param_value;
	ssize_t ret;
	u8 *result;

	result = kunit_kzalloc(test, param->utf8_string_length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, result);

	ret = wmi_string_to_utf8s(param->wmi_string, result, param->utf8_string_length);

	KUNIT_EXPECT_EQ(test, ret, param->utf8_string_length - 1);
	KUNIT_EXPECT_MEMEQ(test, result, param->utf8_string, param->utf8_string_length);
}

static void wmi_string_from_utf8s_test(struct kunit *test)
{
	const struct wmi_string_param *param = test->param_value;
	struct wmi_string *result;
	size_t max_chars;
	ssize_t ret;

	max_chars = (param->wmi_string_length - sizeof(*result)) / 2;
	result = kunit_kzalloc(test, param->wmi_string_length, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, result);

	ret = wmi_string_from_utf8s(result, max_chars, param->utf8_string,
				    param->utf8_string_length);

	KUNIT_EXPECT_EQ(test, ret, max_chars - 1);
	KUNIT_EXPECT_MEMEQ(test, result, param->wmi_string, param->wmi_string_length);
}

static void wmi_string_to_utf8s_padded_test(struct kunit *test)
{
	u8 result[sizeof(padded_test_utf8_string)];
	ssize_t ret;

	ret = wmi_string_to_utf8s(&padded_test_wmi_string, result, sizeof(result));

	KUNIT_EXPECT_EQ(test, ret, sizeof(test_utf8_string) - 1);
	KUNIT_EXPECT_MEMEQ(test, result, test_utf8_string, sizeof(test_utf8_string));
}

static void wmi_string_from_utf8s_padded_test(struct kunit *test)
{
	struct wmi_string *result;
	size_t max_chars;
	ssize_t ret;

	max_chars = (PADDED_TEST_WMI_STRING_LENGTH - sizeof(*result)) / 2;
	result = kunit_kzalloc(test, PADDED_TEST_WMI_STRING_LENGTH, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, result);

	ret = wmi_string_from_utf8s(result, max_chars, padded_test_utf8_string,
				    sizeof(padded_test_utf8_string));

	KUNIT_EXPECT_EQ(test, ret, sizeof(test_utf8_string) - 1);
	KUNIT_EXPECT_MEMEQ(test, result, &test_wmi_string, sizeof(test_wmi_string));
}

static void wmi_string_to_utf8s_oversized_test(struct kunit *test)
{
	u8 result[sizeof(oversized_test_utf8_string)];
	ssize_t ret;

	ret = wmi_string_to_utf8s(&oversized_test_wmi_string, result, sizeof(result));

	KUNIT_EXPECT_EQ(test, ret, sizeof(test_utf8_string) - 1);
	KUNIT_EXPECT_MEMEQ(test, result, test_utf8_string, sizeof(test_utf8_string));
}

static void wmi_string_from_utf8s_oversized_test(struct kunit *test)
{
	struct wmi_string *result;
	size_t max_chars;
	ssize_t ret;

	max_chars = (TEST_WMI_STRING_LENGTH - sizeof(*result)) / 2;
	result = kunit_kzalloc(test, TEST_WMI_STRING_LENGTH, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, result);

	ret = wmi_string_from_utf8s(result, max_chars, oversized_test_utf8_string,
				    sizeof(oversized_test_utf8_string));

	KUNIT_EXPECT_EQ(test, ret, sizeof(test_utf8_string) - 1);
	KUNIT_EXPECT_MEMEQ(test, result, &test_wmi_string, sizeof(test_wmi_string));
}

static void wmi_string_to_utf8s_invalid_test(struct kunit *test)
{
	u8 result[sizeof(invalid_test_utf8_string)];
	ssize_t ret;

	ret = wmi_string_to_utf8s(&invalid_test_wmi_string, result, sizeof(result));

	KUNIT_EXPECT_EQ(test, ret, sizeof(test_utf8_string) - 1);
	KUNIT_EXPECT_MEMEQ(test, result, test_utf8_string, sizeof(test_utf8_string));
}

static void wmi_string_from_utf8s_invalid_test(struct kunit *test)
{
	struct wmi_string *result;
	size_t max_chars;
	ssize_t ret;

	max_chars = (INVALID_TEST_WMI_STRING_LENGTH - sizeof(*result)) / 2;
	result = kunit_kzalloc(test, INVALID_TEST_WMI_STRING_LENGTH, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, result);

	ret = wmi_string_from_utf8s(result, max_chars, invalid_test_utf8_string,
				    sizeof(invalid_test_utf8_string));

	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static struct kunit_case wmi_string_test_cases[] = {
	KUNIT_CASE_PARAM(wmi_string_to_utf8s_test, wmi_string_gen_params),
	KUNIT_CASE_PARAM(wmi_string_from_utf8s_test, wmi_string_gen_params),
	KUNIT_CASE(wmi_string_to_utf8s_padded_test),
	KUNIT_CASE(wmi_string_from_utf8s_padded_test),
	KUNIT_CASE(wmi_string_to_utf8s_oversized_test),
	KUNIT_CASE(wmi_string_from_utf8s_oversized_test),
	KUNIT_CASE(wmi_string_to_utf8s_invalid_test),
	KUNIT_CASE(wmi_string_from_utf8s_invalid_test),
	{}
};

static struct kunit_suite wmi_string_test_suite = {
	.name = "wmi_string",
	.test_cases = wmi_string_test_cases,
};

kunit_test_suite(wmi_string_test_suite);

MODULE_AUTHOR("Armin Wolf <W_Armin@gmx.de>");
MODULE_DESCRIPTION("KUnit test for the ACPI-WMI string conversion code");
MODULE_LICENSE("GPL");
