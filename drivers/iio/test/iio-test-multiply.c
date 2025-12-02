// SPDX-License-Identifier: GPL-2.0-only
/* Unit tests for IIO multiply functions
 *
 * Copyright (c) 2025 Hans de Goede <hans@hansg.org>
 * Based on iio-test-format.c which is:
 * Copyright (c) 2020 Lars-Peter Clausen <lars@metafoo.de>
 */

#include <kunit/test.h>
#include <linux/iio/consumer.h>
#include <linux/math64.h>
#include <linux/types.h>

static void __iio_test_iio_multiply_value_integer(struct kunit *test, s64 multiplier)
{
	int ret, result, val;

	val = 42;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT, val, 0);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, multiplier * val);

	val = -23;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT, val, 0);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, multiplier * val);

	val = 0;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT, val, 0);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, multiplier * val);
}

static void iio_test_iio_multiply_value_integer(struct kunit *test)
{
	__iio_test_iio_multiply_value_integer(test, 20);
	__iio_test_iio_multiply_value_integer(test, -20);
}

static void __iio_test_iio_multiply_value_fixedpoint(struct kunit *test, s64 multiplier)
{
	int ret, result, val, val2;

	/* positive >= 1 (1.5) */
	val = 1;
	val2 = 500000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_MICRO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * 15, 10));

	val = 1;
	val2 = 500000000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_NANO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * 15, 10));

	/* positive < 1 (0.5) */
	val = 0;
	val2 = 500000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_MICRO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * 5, 10));

	val = 0;
	val2 = 500000000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_NANO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * 5, 10));

	/* negative <= -1 (-1.5) */
	val = -1;
	val2 = 500000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_MICRO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * -15, 10));

	val = -1;
	val2 = 500000000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_NANO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * -15, 10));

	/* negative > -1 (-0.5) */
	val = 0;
	val2 = -500000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_MICRO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * -5, 10));

	val = 0;
	val2 = -500000000;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_INT_PLUS_NANO, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * -5, 10));
}

static void iio_test_iio_multiply_value_fixedpoint(struct kunit *test)
{
	__iio_test_iio_multiply_value_fixedpoint(test, 20);
	__iio_test_iio_multiply_value_fixedpoint(test, -20);
}

static void __iio_test_iio_multiply_value_fractional(struct kunit *test, s64 multiplier)
{
	int ret, result, val, val2;

	/* positive < 1 (1/10)*/
	val = 1;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * val, val2));

	/* positive >= 1 (100/3)*/
	val = 100;
	val2 = 3;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * val, val2));

	/* negative > -1 (-1/10) */
	val = -1;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * val, val2));

	/* negative <= -1 (-200/3)*/
	val = -200;
	val2 = 3;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * val, val2));

	/* Zero (0/-10) */
	val = 0;
	val2 = -10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, div_s64(multiplier * val, val2));
}

static void iio_test_iio_multiply_value_fractional(struct kunit *test)
{
	__iio_test_iio_multiply_value_fractional(test, 20);
	__iio_test_iio_multiply_value_fractional(test, -20);
}

static void __iio_test_iio_multiply_value_fractional_log2(struct kunit *test, s64 multiplier)
{
	int ret, result, val, val2;

	/* positive < 1 (123/1024) */
	val = 123;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL_LOG2, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, (multiplier * val) >> val2);

	/* positive >= 1 (1234567/1024) */
	val = 1234567;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL_LOG2, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, (multiplier * val) >> val2);

	/* negative > -1 (-123/1024) */
	val = -123;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL_LOG2, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, (multiplier * val) >> val2);

	/* negative <= -1 (-1234567/1024) */
	val = -1234567;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL_LOG2, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, (multiplier * val) >> val2);

	/* Zero (0/1024) */
	val = 0;
	val2 = 10;
	ret = iio_multiply_value(&result, multiplier, IIO_VAL_FRACTIONAL_LOG2, val, val2);
	KUNIT_EXPECT_EQ(test, ret, IIO_VAL_INT);
	KUNIT_EXPECT_EQ(test, result, (multiplier * val) >> val2);
}

static void iio_test_iio_multiply_value_fractional_log2(struct kunit *test)
{
	__iio_test_iio_multiply_value_fractional_log2(test, 20);
	__iio_test_iio_multiply_value_fractional_log2(test, -20);
}

static struct kunit_case iio_multiply_test_cases[] = {
		KUNIT_CASE(iio_test_iio_multiply_value_integer),
		KUNIT_CASE(iio_test_iio_multiply_value_fixedpoint),
		KUNIT_CASE(iio_test_iio_multiply_value_fractional),
		KUNIT_CASE(iio_test_iio_multiply_value_fractional_log2),
		{ }
};

static struct kunit_suite iio_multiply_test_suite = {
	.name = "iio-multiply",
	.test_cases = iio_multiply_test_cases,
};
kunit_test_suite(iio_multiply_test_suite);

MODULE_AUTHOR("Hans de Goede <hans@hansg.org>");
MODULE_DESCRIPTION("Test IIO multiply functions");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_UNIT_TEST");
