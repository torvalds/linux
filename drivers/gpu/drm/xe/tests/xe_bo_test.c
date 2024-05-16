// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_bo_test.h"

#include <kunit/test.h>

static struct kunit_case xe_bo_tests[] = {
	KUNIT_CASE(xe_ccs_migrate_kunit),
	KUNIT_CASE(xe_bo_evict_kunit),
	{}
};

static struct kunit_suite xe_bo_test_suite = {
	.name = "xe_bo",
	.test_cases = xe_bo_tests,
};

kunit_test_suite(xe_bo_test_suite);
