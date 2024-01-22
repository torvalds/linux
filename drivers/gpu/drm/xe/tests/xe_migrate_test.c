// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_migrate_test.h"

#include <kunit/test.h>

static struct kunit_case xe_migrate_tests[] = {
	KUNIT_CASE(xe_migrate_sanity_kunit),
	{}
};

static struct kunit_suite xe_migrate_test_suite = {
	.name = "xe_migrate",
	.test_cases = xe_migrate_tests,
};

kunit_test_suite(xe_migrate_test_suite);

MODULE_AUTHOR("Intel Corporation");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("xe_migrate kunit test");
MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);
