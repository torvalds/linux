// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2025 Imagination Technologies Ltd. */

#include "pvr_device.h"

#include <linux/errno.h>
#include <linux/stddef.h>
#include <linux/string.h>
#include <linux/types.h>

#include <kunit/test.h>
#include <kunit/visibility.h>

static void decode_gpuid_string(struct kunit *test)
{
	const struct pvr_gpu_id bad_gpuid = { 0xdead, 0xbeef, 0xcafe, 0xface };
	const u64 packed_bad_gpuid = pvr_gpu_id_to_packed_bvnc(&bad_gpuid);

#define GPUID_TEST_CASE(str_, err_, value_)					\
	do {									\
		struct pvr_gpu_id _gpuid_out = bad_gpuid;			\
		int _err;							\
		_err = pvr_gpuid_decode_string(NULL, str_, &_gpuid_out);	\
		KUNIT_EXPECT_EQ(test, _err, err_);				\
		KUNIT_EXPECT_EQ(test,						\
				pvr_gpu_id_to_packed_bvnc(&_gpuid_out),		\
				value_);					\
	} while (0)

#define GPUID_TEST_CASE_OK(str_, b_, v_, n_, c_) \
	GPUID_TEST_CASE(str_, 0, PVR_PACKED_BVNC(b_, v_, n_, c_))

#define GPUID_TEST_CASE_INVAL(str_) \
	GPUID_TEST_CASE(str_, -EINVAL, packed_bad_gpuid)

	GPUID_TEST_CASE_OK("12.34.56.78", 12, 34, 56, 78);
	GPUID_TEST_CASE_OK("0.0.0.0", 0, 0, 0, 0);

	GPUID_TEST_CASE_INVAL("");
	GPUID_TEST_CASE_INVAL("42.foobar-invalid.gpuid.bvnc");

	/* String longer than PVR_GPUID_STRING_MAX_LENGTH. */
	GPUID_TEST_CASE_INVAL("12.34.56.789012345678901234567890123456");

	/* Single value overflowing u16. */
	GPUID_TEST_CASE_INVAL("12.34.56.999999");

	/* Wrong number of parts and/or dots. */
	GPUID_TEST_CASE_INVAL("12.34.56.78.90");
	GPUID_TEST_CASE_INVAL("12.34.56..78");
	GPUID_TEST_CASE_INVAL("12.34..56");
	GPUID_TEST_CASE_INVAL("12.34.56");

#undef GPUID_TEST_CASE_INVAL
#undef GPUID_TEST_CASE_OK
#undef GPUID_TEST_CASE
}

static struct kunit_case pvr_tests_cases[] = {
	KUNIT_CASE(decode_gpuid_string),
	{},
};

static struct kunit_suite pvr_tests_suite = {
	.name = "pvr_tests",
	.test_cases = pvr_tests_cases,
};
kunit_test_suite(pvr_tests_suite);

MODULE_AUTHOR("Imagination Technologies Ltd.");
MODULE_LICENSE("Dual MIT/GPL");
MODULE_DESCRIPTION("pvr kunit tests");
MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");
