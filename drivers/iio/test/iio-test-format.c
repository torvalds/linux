// SPDX-License-Identifier: GPL-2.0-only
/* Unit tests for IIO formatting functions
 *
 * Copyright (c) 2020 Lars-Peter Clausen <lars@metafoo.de>
 */

#include <kunit/test.h>
#include <linux/iio/iio.h>

#define IIO_TEST_FORMAT_EXPECT_EQ(_test, _buf, _ret, _val) do { \
		KUNIT_EXPECT_EQ(_test, strlen(_buf), _ret); \
		KUNIT_EXPECT_STREQ(_test, (_buf), (_val)); \
	} while (0)

static void iio_test_iio_format_value_integer(struct kunit *test)
{
	char *buf;
	int val;
	int ret;

	buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	val = 42;
	ret = iio_format_value(buf, IIO_VAL_INT, 1, &val);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "42\n");

	val = -23;
	ret = iio_format_value(buf, IIO_VAL_INT, 1, &val);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-23\n");

	val = 0;
	ret = iio_format_value(buf, IIO_VAL_INT, 1, &val);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0\n");

	val = INT_MAX;
	ret = iio_format_value(buf, IIO_VAL_INT, 1, &val);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "2147483647\n");

	val = INT_MIN;
	ret = iio_format_value(buf, IIO_VAL_INT, 1, &val);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-2147483648\n");
}

static void iio_test_iio_format_value_fixedpoint(struct kunit *test)
{
	int values[2];
	char *buf;
	int ret;

	buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/* positive >= 1 */
	values[0] = 1;
	values[1] = 10;

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "1.000010\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO_DB, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "1.000010 dB\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_NANO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "1.000000010\n");

	/* positive < 1 */
	values[0] = 0;
	values[1] = 12;

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.000012\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO_DB, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.000012 dB\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_NANO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.000000012\n");

	/* negative <= -1 */
	values[0] = -1;
	values[1] = 10;

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-1.000010\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO_DB, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-1.000010 dB\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_NANO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-1.000000010\n");

	/* negative > -1 */
	values[0] = 0;
	values[1] = -123;
	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-0.000123\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_MICRO_DB, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-0.000123 dB\n");

	ret = iio_format_value(buf, IIO_VAL_INT_PLUS_NANO, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-0.000000123\n");
}

static void iio_test_iio_format_value_fractional(struct kunit *test)
{
	int values[2];
	char *buf;
	int ret;

	buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/* positive < 1 */
	values[0] = 1;
	values[1] = 10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.100000000\n");

	/* positive >= 1 */
	values[0] = 100;
	values[1] = 3;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "33.333333333\n");

	/* negative > -1 */
	values[0] = -1;
	values[1] = 1000000000;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-0.000000001\n");

	/* negative <= -1 */
	values[0] = -200;
	values[1] = 3;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-66.666666666\n");

	/* Zero */
	values[0] = 0;
	values[1] = -10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.000000000\n");
}

static void iio_test_iio_format_value_fractional_log2(struct kunit *test)
{
	int values[2];
	char *buf;
	int ret;

	buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	/* positive < 1 */
	values[0] = 123;
	values[1] = 10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL_LOG2, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.120117187\n");

	/* positive >= 1 */
	values[0] = 1234567;
	values[1] = 10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL_LOG2, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "1205.631835937\n");

	/* negative > -1 */
	values[0] = -123;
	values[1] = 10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL_LOG2, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-0.120117187\n");

	/* negative <= -1 */
	values[0] = -1234567;
	values[1] = 10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL_LOG2, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-1205.631835937\n");

	/* Zero */
	values[0] = 0;
	values[1] = 10;
	ret = iio_format_value(buf, IIO_VAL_FRACTIONAL_LOG2, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0.000000000\n");
}

static void iio_test_iio_format_value_multiple(struct kunit *test)
{
	int values[] = {1, -2, 3, -4, 5};
	char *buf;
	int ret;

	buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	ret = iio_format_value(buf, IIO_VAL_INT_MULTIPLE,
			       ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "1 -2 3 -4 5 \n");
}

static void iio_test_iio_format_value_integer_64(struct kunit *test)
{
	int values[2];
	s64 value;
	char *buf;
	int ret;

	buf = kunit_kmalloc(test, PAGE_SIZE, GFP_KERNEL);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);

	value = 24;
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "24\n");

	value = -24;
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-24\n");

	value = 0;
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "0\n");

	value = UINT_MAX;
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "4294967295\n");

	value = -((s64)UINT_MAX);
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-4294967295\n");

	value = LLONG_MAX;
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "9223372036854775807\n");

	value = LLONG_MIN;
	values[0] = lower_32_bits(value);
	values[1] = upper_32_bits(value);
	ret = iio_format_value(buf, IIO_VAL_INT_64, ARRAY_SIZE(values), values);
	IIO_TEST_FORMAT_EXPECT_EQ(test, buf, ret, "-9223372036854775808\n");
}

static struct kunit_case iio_format_test_cases[] = {
		KUNIT_CASE(iio_test_iio_format_value_integer),
		KUNIT_CASE(iio_test_iio_format_value_fixedpoint),
		KUNIT_CASE(iio_test_iio_format_value_fractional),
		KUNIT_CASE(iio_test_iio_format_value_fractional_log2),
		KUNIT_CASE(iio_test_iio_format_value_multiple),
		KUNIT_CASE(iio_test_iio_format_value_integer_64),
		{}
};

static struct kunit_suite iio_format_test_suite = {
	.name = "iio-format",
	.test_cases = iio_format_test_cases,
};
kunit_test_suite(iio_format_test_suite);

MODULE_AUTHOR("Lars-Peter Clausen <lars@metafoo.de>");
MODULE_DESCRIPTION("Test IIO formatting functions");
MODULE_LICENSE("GPL v2");
