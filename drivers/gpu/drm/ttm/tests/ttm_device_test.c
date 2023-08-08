// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>

#include "ttm_kunit_helpers.h"

static void ttm_device_init_basic(struct kunit *test)
{
	struct ttm_test_devices *priv = test->priv;
	struct ttm_device *ttm_dev;
	struct ttm_resource_manager *ttm_sys_man;
	int err;

	ttm_dev = kunit_kzalloc(test, sizeof(*ttm_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev);

	err = ttm_device_kunit_init(priv, ttm_dev, false, false);
	KUNIT_ASSERT_EQ(test, err, 0);

	KUNIT_EXPECT_PTR_EQ(test, ttm_dev->funcs, &ttm_dev_funcs);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev->wq);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev->man_drv[TTM_PL_SYSTEM]);

	ttm_sys_man = &ttm_dev->sysman;
	KUNIT_ASSERT_NOT_NULL(test, ttm_sys_man);
	KUNIT_EXPECT_TRUE(test, ttm_sys_man->use_tt);
	KUNIT_EXPECT_TRUE(test, ttm_sys_man->use_type);
	KUNIT_ASSERT_NOT_NULL(test, ttm_sys_man->func);

	KUNIT_EXPECT_PTR_EQ(test, ttm_dev->dev_mapping,
			    priv->drm->anon_inode->i_mapping);

	ttm_device_fini(ttm_dev);
}

static struct kunit_case ttm_device_test_cases[] = {
	KUNIT_CASE(ttm_device_init_basic),
	{}
};

static struct kunit_suite ttm_device_test_suite = {
	.name = "ttm_device",
	.init = ttm_test_devices_init,
	.exit = ttm_test_devices_fini,
	.test_cases = ttm_device_test_cases,
};

kunit_test_suites(&ttm_device_test_suite);

MODULE_LICENSE("GPL");
