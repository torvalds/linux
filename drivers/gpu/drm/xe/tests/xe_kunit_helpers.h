/* SPDX-License-Identifier: GPL-2.0 AND MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_KUNIT_HELPERS_H_
#define _XE_KUNIT_HELPERS_H_

struct device;
struct kunit;
struct xe_device;

struct xe_device *xe_kunit_helper_alloc_xe_device(struct kunit *test,
						  struct device *dev);
int xe_kunit_helper_xe_device_test_init(struct kunit *test);

int xe_kunit_helper_xe_device_live_test_init(struct kunit *test);

#endif
