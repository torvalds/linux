// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/ttm/ttm_resource.h>

#include "ttm_kunit_helpers.h"

#define RES_SIZE		SZ_4K
#define TTM_PRIV_DUMMY_REG	(TTM_NUM_MEM_TYPES - 1)

struct ttm_resource_test_case {
	const char *description;
	u32 mem_type;
	u32 flags;
};

struct ttm_resource_test_priv {
	struct ttm_test_devices *devs;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
};

static const struct ttm_resource_manager_func ttm_resource_manager_mock_funcs = { };

static int ttm_resource_test_init(struct kunit *test)
{
	struct ttm_resource_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	priv->devs = ttm_test_devices_all(test);
	KUNIT_ASSERT_NOT_NULL(test, priv->devs);

	test->priv = priv;

	return 0;
}

static void ttm_resource_test_fini(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;

	ttm_test_devices_put(test, priv->devs);
}

static void ttm_init_test_mocks(struct kunit *test,
				struct ttm_resource_test_priv *priv,
				u32 mem_type, u32 flags)
{
	size_t size = RES_SIZE;

	/* Make sure we have what we need for a good BO mock */
	KUNIT_ASSERT_NOT_NULL(test, priv->devs->ttm_dev);

	priv->bo = ttm_bo_kunit_init(test, priv->devs, size, NULL);
	priv->place = ttm_place_kunit_init(test, mem_type, flags);
}

static void ttm_init_test_manager(struct kunit *test,
				  struct ttm_resource_test_priv *priv,
				  u32 mem_type)
{
	struct ttm_device *ttm_dev = priv->devs->ttm_dev;
	struct ttm_resource_manager *man;
	size_t size = SZ_16K;

	man = kunit_kzalloc(test, sizeof(*man), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, man);

	man->use_tt = false;
	man->func = &ttm_resource_manager_mock_funcs;

	ttm_resource_manager_init(man, ttm_dev, size);
	ttm_set_driver_manager(ttm_dev, mem_type, man);
	ttm_resource_manager_set_used(man, true);
}

static const struct ttm_resource_test_case ttm_resource_cases[] = {
	{
		.description = "Init resource in TTM_PL_SYSTEM",
		.mem_type = TTM_PL_SYSTEM,
	},
	{
		.description = "Init resource in TTM_PL_VRAM",
		.mem_type = TTM_PL_VRAM,
	},
	{
		.description = "Init resource in a private placement",
		.mem_type = TTM_PRIV_DUMMY_REG,
	},
	{
		.description = "Init resource in TTM_PL_SYSTEM, set placement flags",
		.mem_type = TTM_PL_SYSTEM,
		.flags = TTM_PL_FLAG_TOPDOWN,
	},
};

static void ttm_resource_case_desc(const struct ttm_resource_test_case *t, char *desc)
{
	strscpy(desc, t->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ttm_resource, ttm_resource_cases, ttm_resource_case_desc);

static void ttm_resource_init_basic(struct kunit *test)
{
	const struct ttm_resource_test_case *params = test->param_value;
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource *res;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_resource_manager *man;
	u64 expected_usage;

	ttm_init_test_mocks(test, priv, params->mem_type, params->flags);
	bo = priv->bo;
	place = priv->place;

	if (params->mem_type > TTM_PL_SYSTEM)
		ttm_init_test_manager(test, priv, params->mem_type);

	res = kunit_kzalloc(test, sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);

	man = ttm_manager_type(priv->devs->ttm_dev, place->mem_type);
	expected_usage = man->usage + RES_SIZE;

	KUNIT_ASSERT_TRUE(test, list_empty(&man->lru[bo->priority]));

	ttm_resource_init(bo, place, res);

	KUNIT_ASSERT_EQ(test, res->start, 0);
	KUNIT_ASSERT_EQ(test, res->size, RES_SIZE);
	KUNIT_ASSERT_EQ(test, res->mem_type, place->mem_type);
	KUNIT_ASSERT_EQ(test, res->placement, place->flags);
	KUNIT_ASSERT_PTR_EQ(test, res->bo, bo);

	KUNIT_ASSERT_NULL(test, res->bus.addr);
	KUNIT_ASSERT_EQ(test, res->bus.offset, 0);
	KUNIT_ASSERT_FALSE(test, res->bus.is_iomem);
	KUNIT_ASSERT_EQ(test, res->bus.caching, ttm_cached);
	KUNIT_ASSERT_EQ(test, man->usage, expected_usage);

	KUNIT_ASSERT_TRUE(test, list_is_singular(&man->lru[bo->priority]));

	ttm_resource_fini(man, res);
}

static void ttm_resource_init_pinned(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource *res;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_resource_manager *man;

	ttm_init_test_mocks(test, priv, TTM_PL_SYSTEM, 0);
	bo = priv->bo;
	place = priv->place;

	man = ttm_manager_type(priv->devs->ttm_dev, place->mem_type);

	res = kunit_kzalloc(test, sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);
	KUNIT_ASSERT_TRUE(test, list_empty(&bo->bdev->pinned));

	dma_resv_lock(bo->base.resv, NULL);
	ttm_bo_pin(bo);
	ttm_resource_init(bo, place, res);
	KUNIT_ASSERT_TRUE(test, list_is_singular(&bo->bdev->pinned));

	ttm_bo_unpin(bo);
	ttm_resource_fini(man, res);
	dma_resv_unlock(bo->base.resv);

	KUNIT_ASSERT_TRUE(test, list_empty(&bo->bdev->pinned));
}

static void ttm_resource_fini_basic(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource *res;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_resource_manager *man;

	ttm_init_test_mocks(test, priv, TTM_PL_SYSTEM, 0);
	bo = priv->bo;
	place = priv->place;

	man = ttm_manager_type(priv->devs->ttm_dev, place->mem_type);

	res = kunit_kzalloc(test, sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);

	ttm_resource_init(bo, place, res);
	ttm_resource_fini(man, res);

	KUNIT_ASSERT_TRUE(test, list_empty(&res->lru.link));
	KUNIT_ASSERT_EQ(test, man->usage, 0);
}

static void ttm_resource_manager_init_basic(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource_manager *man;
	size_t size = SZ_16K;

	man = kunit_kzalloc(test, sizeof(*man), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, man);

	ttm_resource_manager_init(man, priv->devs->ttm_dev, size);

	KUNIT_ASSERT_PTR_EQ(test, man->bdev, priv->devs->ttm_dev);
	KUNIT_ASSERT_EQ(test, man->size, size);
	KUNIT_ASSERT_EQ(test, man->usage, 0);
	KUNIT_ASSERT_NULL(test, man->move);
	KUNIT_ASSERT_NOT_NULL(test, &man->move_lock);

	for (int i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		KUNIT_ASSERT_TRUE(test, list_empty(&man->lru[i]));
}

static void ttm_resource_manager_usage_basic(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource *res;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_resource_manager *man;
	u64 actual_usage;

	ttm_init_test_mocks(test, priv, TTM_PL_SYSTEM, TTM_PL_FLAG_TOPDOWN);
	bo = priv->bo;
	place = priv->place;

	res = kunit_kzalloc(test, sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);

	man = ttm_manager_type(priv->devs->ttm_dev, place->mem_type);

	ttm_resource_init(bo, place, res);
	actual_usage = ttm_resource_manager_usage(man);

	KUNIT_ASSERT_EQ(test, actual_usage, RES_SIZE);

	ttm_resource_fini(man, res);
}

static void ttm_resource_manager_set_used_basic(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource_manager *man;

	man = ttm_manager_type(priv->devs->ttm_dev, TTM_PL_SYSTEM);
	KUNIT_ASSERT_TRUE(test, man->use_type);

	ttm_resource_manager_set_used(man, false);
	KUNIT_ASSERT_FALSE(test, man->use_type);
}

static void ttm_sys_man_alloc_basic(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource_manager *man;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_resource *res;
	u32 mem_type = TTM_PL_SYSTEM;
	int ret;

	ttm_init_test_mocks(test, priv, mem_type, 0);
	bo = priv->bo;
	place = priv->place;

	man = ttm_manager_type(priv->devs->ttm_dev, mem_type);
	ret = man->func->alloc(man, bo, place, &res);

	KUNIT_ASSERT_EQ(test, ret, 0);
	KUNIT_ASSERT_EQ(test, res->size, RES_SIZE);
	KUNIT_ASSERT_EQ(test, res->mem_type, mem_type);
	KUNIT_ASSERT_PTR_EQ(test, res->bo, bo);

	ttm_resource_fini(man, res);
}

static void ttm_sys_man_free_basic(struct kunit *test)
{
	struct ttm_resource_test_priv *priv = test->priv;
	struct ttm_resource_manager *man;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_resource *res;
	u32 mem_type = TTM_PL_SYSTEM;

	ttm_init_test_mocks(test, priv, mem_type, 0);
	bo = priv->bo;
	place = priv->place;

	res = kunit_kzalloc(test, sizeof(*res), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, res);

	ttm_resource_alloc(bo, place, &res);

	man = ttm_manager_type(priv->devs->ttm_dev, mem_type);
	man->func->free(man, res);

	KUNIT_ASSERT_TRUE(test, list_empty(&man->lru[bo->priority]));
	KUNIT_ASSERT_EQ(test, man->usage, 0);
}

static struct kunit_case ttm_resource_test_cases[] = {
	KUNIT_CASE_PARAM(ttm_resource_init_basic, ttm_resource_gen_params),
	KUNIT_CASE(ttm_resource_init_pinned),
	KUNIT_CASE(ttm_resource_fini_basic),
	KUNIT_CASE(ttm_resource_manager_init_basic),
	KUNIT_CASE(ttm_resource_manager_usage_basic),
	KUNIT_CASE(ttm_resource_manager_set_used_basic),
	KUNIT_CASE(ttm_sys_man_alloc_basic),
	KUNIT_CASE(ttm_sys_man_free_basic),
	{}
};

static struct kunit_suite ttm_resource_test_suite = {
	.name = "ttm_resource",
	.init = ttm_resource_test_init,
	.exit = ttm_resource_test_fini,
	.test_cases = ttm_resource_test_cases,
};

kunit_test_suites(&ttm_resource_test_suite);

MODULE_DESCRIPTION("KUnit tests for ttm_resource and ttm_sys_man APIs");
MODULE_LICENSE("GPL and additional rights");
