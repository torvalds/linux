// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_mocs_test.h"

#include <kunit/test.h>

static struct kunit_case xe_mocs_tests[] = {
	KUNIT_CASE(xe_live_mocs_kernel_kunit),
	KUNIT_CASE(xe_live_mocs_reset_kunit),
	{}
};

static struct kunit_suite xe_mocs_test_suite = {
	.name = "xe_mocs",
	.test_cases = xe_mocs_tests,
};

kunit_test_suite(xe_mocs_test_suite);
