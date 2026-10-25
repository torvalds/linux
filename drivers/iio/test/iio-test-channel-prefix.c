// SPDX-License-Identifier: GPL-2.0
/*
 * Unit tests for IIO channel prefix generation.
 */

#include <kunit/test.h>
#include <kunit/visibility.h>

#include <linux/iio/iio.h>
#include <linux/limits.h>
#include <linux/string.h>

#include "../iio_core.h"

#define PREFIX_BUF_SIZE		(NAME_MAX + 1)

#define EXPECT_PREFIX(_test, _buf, _ret, _expected) do {			\
		struct kunit *__test = (_test);					\
		const char *__expected = (_expected);				\
										\
		KUNIT_EXPECT_EQ(__test, (_ret), (ssize_t)strlen(__expected));	\
		KUNIT_EXPECT_STREQ(__test, (_buf), __expected);			\
	} while (0)

static char *iio_test_prefix_alloc(struct kunit *test)
{
	char *buf = kunit_kzalloc(test, PREFIX_BUF_SIZE, GFP_KERNEL);

	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, buf);
	return buf;
}

static void iio_test_prefix_shared_by_all(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
	};
	const struct iio_chan_spec chan_noisy = {
		.type = IIO_ACCEL,
		.output = 1,
		.indexed = 1,
		.modified = 1,
		.channel = 7,
		.channel2 = IIO_MOD_Z,
		.extend_name = "supply",
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SHARED_BY_ALL,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "");

	ret = __iio_chan_prefix_emit(NULL, &chan_noisy, IIO_SHARED_BY_ALL,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "");
}

static void iio_test_prefix_shared_by_dir(struct kunit *test)
{
	const struct iio_chan_spec chan_in = {
		.type = IIO_VOLTAGE,
		.output = 0,
	};
	const struct iio_chan_spec chan_out = {
		.type = IIO_VOLTAGE,
		.output = 1,
	};
	const struct iio_chan_spec chan_in_noisy = {
		.type = IIO_ACCEL,
		.indexed = 1,
		.modified = 1,
		.channel = 5,
		.channel2 = IIO_MOD_Y,
		.extend_name = "supply",
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan_in, IIO_SHARED_BY_DIR,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in");

	ret = __iio_chan_prefix_emit(NULL, &chan_out, IIO_SHARED_BY_DIR,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "out");

	ret = __iio_chan_prefix_emit(NULL, &chan_in_noisy, IIO_SHARED_BY_DIR,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in");
}

static void iio_test_prefix_shared_by_type(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
	};
	const struct iio_chan_spec chan_diff = {
		.type = IIO_VOLTAGE,
		.differential = 1,
	};
	const struct iio_chan_spec chan_noisy = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.modified = 1,
		.channel = 4,
		.channel2 = IIO_MOD_X,
		.extend_name = "supply",
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SHARED_BY_TYPE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_voltage");

	ret = __iio_chan_prefix_emit(NULL, &chan_diff, IIO_SHARED_BY_TYPE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_voltage-voltage");

	ret = __iio_chan_prefix_emit(NULL, &chan_noisy, IIO_SHARED_BY_TYPE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_voltage");
}

static void iio_test_prefix_separate_simple(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_TEMP,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_temp");
}

static void iio_test_prefix_separate_indexed(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 3,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_voltage3");
}

static void iio_test_prefix_separate_indexed_diff(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.differential = 1,
		.channel = 0,
		.channel2 = 1,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_voltage0-voltage1");
}

static void iio_test_prefix_separate_modified(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_ACCEL,
		.modified = 1,
		.channel2 = IIO_MOD_X,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_accel_x");
}

static void iio_test_prefix_separate_indexed_modified(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_ACCEL,
		.indexed = 1,
		.modified = 1,
		.channel = 2,
		.channel2 = IIO_MOD_Y,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_accel2_y");
}

static void iio_test_prefix_separate_extend_name(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.channel = 2,
		.extend_name = "supply",
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "in_voltage2_supply");
}

static void iio_test_prefix_output_separate(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
		.output = 1,
		.indexed = 1,
		.channel = 0,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	EXPECT_PREFIX(test, buf, ret, "out_voltage0");
}

static void iio_test_prefix_diff_unindexed_fails(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
		.differential = 1,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void iio_test_prefix_diff_modified_fails(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
		.indexed = 1,
		.differential = 1,
		.modified = 1,
		.channel = 0,
		.channel2 = 1,
	};
	char *buf = iio_test_prefix_alloc(test);
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SEPARATE,
				     buf, PREFIX_BUF_SIZE);
	KUNIT_EXPECT_EQ(test, ret, -EINVAL);
}

static void iio_test_prefix_overflow(struct kunit *test)
{
	const struct iio_chan_spec chan = {
		.type = IIO_VOLTAGE,
	};
	char small[4];
	ssize_t ret;

	ret = __iio_chan_prefix_emit(NULL, &chan, IIO_SHARED_BY_TYPE,
				     small, sizeof(small));
	KUNIT_EXPECT_EQ(test, ret, -EOVERFLOW);
}

static struct kunit_case iio_chan_prefix_test_cases[] = {
	KUNIT_CASE(iio_test_prefix_shared_by_all),
	KUNIT_CASE(iio_test_prefix_shared_by_dir),
	KUNIT_CASE(iio_test_prefix_shared_by_type),
	KUNIT_CASE(iio_test_prefix_separate_simple),
	KUNIT_CASE(iio_test_prefix_separate_indexed),
	KUNIT_CASE(iio_test_prefix_separate_indexed_diff),
	KUNIT_CASE(iio_test_prefix_separate_modified),
	KUNIT_CASE(iio_test_prefix_separate_indexed_modified),
	KUNIT_CASE(iio_test_prefix_separate_extend_name),
	KUNIT_CASE(iio_test_prefix_output_separate),
	KUNIT_CASE(iio_test_prefix_diff_unindexed_fails),
	KUNIT_CASE(iio_test_prefix_diff_modified_fails),
	KUNIT_CASE(iio_test_prefix_overflow),
	{ }
};

static struct kunit_suite iio_channel_prefix_test_suite = {
	.name = "iio-channel-prefix",
	.test_cases = iio_chan_prefix_test_cases,
};

kunit_test_suite(iio_channel_prefix_test_suite);

MODULE_AUTHOR("Rodrigo Alencar <rodrigo.alencar@analog.com>");
MODULE_DESCRIPTION("Test IIO channel prefix generation");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
