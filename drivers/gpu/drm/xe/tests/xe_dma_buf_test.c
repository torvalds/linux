// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright © 2022 Intel Corporation
 */

#include "xe_dma_buf_test.h"

#include <kunit/test.h>

static struct kunit_case xe_dma_buf_tests[] = {
	KUNIT_CASE(xe_dma_buf_kunit),
	{}
};

static struct kunit_suite xe_dma_buf_test_suite = {
	.name = "xe_dma_buf",
	.test_cases = xe_dma_buf_tests,
};

kunit_test_suite(xe_dma_buf_test_suite);
