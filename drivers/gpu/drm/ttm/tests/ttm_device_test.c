// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>

#include "ttm_kunit_helpers.h"

struct ttm_device_test_case {
	const char *description;
	bool use_dma_alloc;
	bool use_dma32;
	bool pools_init_expected;
};

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

static void ttm_device_init_multiple(struct kunit *test)
{
	struct ttm_test_devices *priv = test->priv;
	struct ttm_device *ttm_devs;
	unsigned int i, num_dev = 3;
	int err;

	ttm_devs = kunit_kcalloc(test, num_dev, sizeof(*ttm_devs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_devs);

	for (i = 0; i < num_dev; i++) {
		err = ttm_device_kunit_init(priv, &ttm_devs[i], false, false);
		KUNIT_ASSERT_EQ(test, err, 0);

		KUNIT_EXPECT_PTR_EQ(test, ttm_devs[i].dev_mapping,
				    priv->drm->anon_inode->i_mapping);
		KUNIT_ASSERT_NOT_NULL(test, ttm_devs[i].wq);
		KUNIT_EXPECT_PTR_EQ(test, ttm_devs[i].funcs, &ttm_dev_funcs);
		KUNIT_ASSERT_NOT_NULL(test, ttm_devs[i].man_drv[TTM_PL_SYSTEM]);
	}

	KUNIT_ASSERT_EQ(test, list_count_nodes(&ttm_devs[0].device_list), num_dev);

	for (i = 0; i < num_dev; i++)
		ttm_device_fini(&ttm_devs[i]);
}

static void ttm_device_fini_basic(struct kunit *test)
{
	struct ttm_test_devices *priv = test->priv;
	struct ttm_device *ttm_dev;
	struct ttm_resource_manager *man;
	int err;

	ttm_dev = kunit_kzalloc(test, sizeof(*ttm_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev);

	err = ttm_device_kunit_init(priv, ttm_dev, false, false);
	KUNIT_ASSERT_EQ(test, err, 0);

	man = ttm_manager_type(ttm_dev, TTM_PL_SYSTEM);
	KUNIT_ASSERT_NOT_NULL(test, man);

	ttm_device_fini(ttm_dev);

	KUNIT_ASSERT_FALSE(test, man->use_type);
	KUNIT_ASSERT_TRUE(test, list_empty(&man->lru[0]));
	KUNIT_ASSERT_NULL(test, ttm_dev->man_drv[TTM_PL_SYSTEM]);
}

static void ttm_device_init_no_vma_man(struct kunit *test)
{
	struct ttm_test_devices *priv = test->priv;
	struct drm_device *drm = priv->drm;
	struct ttm_device *ttm_dev;
	struct drm_vma_offset_manager *vma_man;
	int err;

	ttm_dev = kunit_kzalloc(test, sizeof(*ttm_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev);

	/* Let's pretend there's no VMA manager allocated */
	vma_man = drm->vma_offset_manager;
	drm->vma_offset_manager = NULL;

	err = ttm_device_kunit_init(priv, ttm_dev, false, false);
	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	/* Bring the manager back for a graceful cleanup */
	drm->vma_offset_manager = vma_man;
}

static const struct ttm_device_test_case ttm_device_cases[] = {
	{
		.description = "No DMA allocations, no DMA32 required",
		.use_dma_alloc = false,
		.use_dma32 = false,
		.pools_init_expected = false,
	},
	{
		.description = "DMA allocations, DMA32 required",
		.use_dma_alloc = true,
		.use_dma32 = true,
		.pools_init_expected = true,
	},
	{
		.description = "No DMA allocations, DMA32 required",
		.use_dma_alloc = false,
		.use_dma32 = true,
		.pools_init_expected = false,
	},
	{
		.description = "DMA allocations, no DMA32 required",
		.use_dma_alloc = true,
		.use_dma32 = false,
		.pools_init_expected = true,
	},
};

static void ttm_device_case_desc(const struct ttm_device_test_case *t, char *desc)
{
	strscpy(desc, t->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ttm_device, ttm_device_cases, ttm_device_case_desc);

static void ttm_device_init_pools(struct kunit *test)
{
	struct ttm_test_devices *priv = test->priv;
	const struct ttm_device_test_case *params = test->param_value;
	struct ttm_device *ttm_dev;
	struct ttm_pool *pool;
	struct ttm_pool_type pt;
	int err;

	ttm_dev = kunit_kzalloc(test, sizeof(*ttm_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev);

	err = ttm_device_kunit_init(priv, ttm_dev,
				    params->use_dma_alloc,
				    params->use_dma32);
	KUNIT_ASSERT_EQ(test, err, 0);

	pool = &ttm_dev->pool;
	KUNIT_ASSERT_NOT_NULL(test, pool);
	KUNIT_EXPECT_PTR_EQ(test, pool->dev, priv->dev);
	KUNIT_EXPECT_EQ(test, pool->use_dma_alloc, params->use_dma_alloc);
	KUNIT_EXPECT_EQ(test, pool->use_dma32, params->use_dma32);

	if (params->pools_init_expected) {
		for (int i = 0; i < TTM_NUM_CACHING_TYPES; ++i) {
			for (int j = 0; j < NR_PAGE_ORDERS; ++j) {
				pt = pool->caching[i].orders[j];
				KUNIT_EXPECT_PTR_EQ(test, pt.pool, pool);
				KUNIT_EXPECT_EQ(test, pt.caching, i);
				KUNIT_EXPECT_EQ(test, pt.order, j);

				if (params->use_dma_alloc)
					KUNIT_ASSERT_FALSE(test,
							   list_empty(&pt.pages));
			}
		}
	}

	ttm_device_fini(ttm_dev);
}

static struct kunit_case ttm_device_test_cases[] = {
	KUNIT_CASE(ttm_device_init_basic),
	KUNIT_CASE(ttm_device_init_multiple),
	KUNIT_CASE(ttm_device_fini_basic),
	KUNIT_CASE(ttm_device_init_no_vma_man),
	KUNIT_CASE_PARAM(ttm_device_init_pools, ttm_device_gen_params),
	{}
};

static struct kunit_suite ttm_device_test_suite = {
	.name = "ttm_device",
	.init = ttm_test_devices_init,
	.exit = ttm_test_devices_fini,
	.test_cases = ttm_device_test_cases,
};

kunit_test_suites(&ttm_device_test_suite);

MODULE_DESCRIPTION("KUnit tests for ttm_device APIs");
MODULE_LICENSE("GPL and additional rights");
