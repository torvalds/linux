// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_mocs_test.h"

#include <kunit/test.h>

static struct kunit_case xe_mocs_tests[] = {
	KUNIT_CASE(xe_live_mocs_kernel_kunit),
	{}
};

static struct kunit_suite xe_mocs_test_suite = {
	.name = "xe_mocs",
	.test_cases = xe_mocs_tests,
};

kunit_test_suite(xe_mocs_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("xe_mocs kunit test");
MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
