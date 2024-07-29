// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <linux/shmem_fs.h>
#include <drm/ttm/ttm_tt.h>

#include "ttm_kunit_helpers.h"

#define BO_SIZE		SZ_4K

struct ttm_tt_test_case {
	const char *description;
	u32 size;
	u32 extra_pages_num;
};

static const struct ttm_tt_test_case ttm_tt_init_basic_cases[] = {
	{
		.description = "Page-aligned size",
		.size = SZ_4K,
	},
	{
		.description = "Extra pages requested",
		.size = SZ_4K,
		.extra_pages_num = 1,
	},
};

static void ttm_tt_init_case_desc(const struct ttm_tt_test_case *t,
				  char *desc)
{
	strscpy(desc, t->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ttm_tt_init_basic, ttm_tt_init_basic_cases,
		  ttm_tt_init_case_desc);

static void ttm_tt_init_basic(struct kunit *test)
{
	const struct ttm_tt_test_case *params = test->param_value;
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	u32 page_flags = TTM_TT_FLAG_ZERO_ALLOC;
	enum ttm_caching caching = ttm_cached;
	u32 extra_pages = params->extra_pages_num;
	int num_pages = params->size >> PAGE_SHIFT;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, test->priv, params->size, NULL);

	err = ttm_tt_init(tt, bo, page_flags, caching, extra_pages);
	KUNIT_ASSERT_EQ(test, err, 0);

	KUNIT_ASSERT_EQ(test, tt->num_pages, num_pages + extra_pages);

	KUNIT_ASSERT_EQ(test, tt->page_flags, page_flags);
	KUNIT_ASSERT_EQ(test, tt->caching, caching);

	KUNIT_ASSERT_NULL(test, tt->dma_address);
	KUNIT_ASSERT_NULL(test, tt->swap_storage);
}

static void ttm_tt_init_misaligned(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	enum ttm_caching caching = ttm_cached;
	u32 size = SZ_8K;
	int num_pages = (size + SZ_4K) >> PAGE_SHIFT;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);

	/* Make the object size misaligned */
	bo->base.size += 1;

	err = ttm_tt_init(tt, bo, 0, caching, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	KUNIT_ASSERT_EQ(test, tt->num_pages, num_pages);
}

static void ttm_tt_fini_basic(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	enum ttm_caching caching = ttm_cached;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	err = ttm_tt_init(tt, bo, 0, caching, 0);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_NOT_NULL(test, tt->pages);

	ttm_tt_fini(tt);
	KUNIT_ASSERT_NULL(test, tt->pages);
}

static void ttm_tt_fini_sg(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	enum ttm_caching caching = ttm_cached;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	err = ttm_sg_tt_init(tt, bo, 0, caching);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_NOT_NULL(test, tt->dma_address);

	ttm_tt_fini(tt);
	KUNIT_ASSERT_NULL(test, tt->dma_address);
}

static void ttm_tt_fini_shmem(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	struct file *shmem;
	enum ttm_caching caching = ttm_cached;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	err = ttm_tt_init(tt, bo, 0, caching, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	shmem = shmem_file_setup("ttm swap", BO_SIZE, 0);
	tt->swap_storage = shmem;

	ttm_tt_fini(tt);
	KUNIT_ASSERT_NULL(test, tt->swap_storage);
}

static void ttm_tt_create_basic(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo->type = ttm_bo_type_device;

	dma_resv_lock(bo->base.resv, NULL);
	err = ttm_tt_create(bo, false);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_NOT_NULL(test, bo->ttm);

	/* Free manually, as it was allocated outside of KUnit */
	kfree(bo->ttm);
}

static void ttm_tt_create_invalid_bo_type(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo->type = ttm_bo_type_sg + 1;

	dma_resv_lock(bo->base.resv, NULL);
	err = ttm_tt_create(bo, false);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, -EINVAL);
	KUNIT_EXPECT_NULL(test, bo->ttm);
}

static void ttm_tt_create_ttm_exists(struct kunit *test)
{
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	enum ttm_caching caching = ttm_cached;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	err = ttm_tt_init(tt, bo, 0, caching, 0);
	KUNIT_ASSERT_EQ(test, err, 0);
	bo->ttm = tt;

	dma_resv_lock(bo->base.resv, NULL);
	err = ttm_tt_create(bo, false);
	dma_resv_unlock(bo->base.resv);

	/* Expect to keep the previous TTM */
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_PTR_EQ(test, tt, bo->ttm);
}

static struct ttm_tt *ttm_tt_null_create(struct ttm_buffer_object *bo,
					 u32 page_flags)
{
	return NULL;
}

static struct ttm_device_funcs ttm_dev_empty_funcs = {
	.ttm_tt_create = ttm_tt_null_create,
};

static void ttm_tt_create_failed(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	struct ttm_buffer_object *bo;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	/* Update ttm_device_funcs so we don't alloc ttm_tt */
	devs->ttm_dev->funcs = &ttm_dev_empty_funcs;

	dma_resv_lock(bo->base.resv, NULL);
	err = ttm_tt_create(bo, false);
	dma_resv_unlock(bo->base.resv);

	KUNIT_ASSERT_EQ(test, err, -ENOMEM);
}

static void ttm_tt_destroy_basic(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	struct ttm_buffer_object *bo;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	dma_resv_lock(bo->base.resv, NULL);
	err = ttm_tt_create(bo, false);
	dma_resv_unlock(bo->base.resv);

	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_NOT_NULL(test, bo->ttm);

	ttm_tt_destroy(devs->ttm_dev, bo->ttm);
}

static void ttm_tt_populate_null_ttm(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	struct ttm_operation_ctx ctx = { };
	int err;

	err = ttm_tt_populate(devs->ttm_dev, NULL, &ctx);
	KUNIT_ASSERT_EQ(test, err, -EINVAL);
}

static void ttm_tt_populate_populated_ttm(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	struct ttm_operation_ctx ctx = { };
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	struct page *populated_page;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	err = ttm_tt_init(tt, bo, 0, ttm_cached, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = ttm_tt_populate(devs->ttm_dev, tt, &ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	populated_page = *tt->pages;

	err = ttm_tt_populate(devs->ttm_dev, tt, &ctx);
	KUNIT_ASSERT_PTR_EQ(test, populated_page, *tt->pages);
}

static void ttm_tt_unpopulate_basic(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	struct ttm_operation_ctx ctx = { };
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	err = ttm_tt_init(tt, bo, 0, ttm_cached, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = ttm_tt_populate(devs->ttm_dev, tt, &ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_TRUE(test, ttm_tt_is_populated(tt));

	ttm_tt_unpopulate(devs->ttm_dev, tt);
	KUNIT_ASSERT_FALSE(test, ttm_tt_is_populated(tt));
}

static void ttm_tt_unpopulate_empty_ttm(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	err = ttm_tt_init(tt, bo, 0, ttm_cached, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	ttm_tt_unpopulate(devs->ttm_dev, tt);
	/* Expect graceful handling of unpopulated TTs */
}

static void ttm_tt_swapin_basic(struct kunit *test)
{
	const struct ttm_test_devices *devs = test->priv;
	int expected_num_pages = BO_SIZE >> PAGE_SHIFT;
	struct ttm_operation_ctx ctx = { };
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	int err, num_pages;

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	err = ttm_tt_init(tt, bo, 0, ttm_cached, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = ttm_tt_populate(devs->ttm_dev, tt, &ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_TRUE(test, ttm_tt_is_populated(tt));

	num_pages = ttm_tt_swapout(devs->ttm_dev, tt, GFP_KERNEL);
	KUNIT_ASSERT_EQ(test, num_pages, expected_num_pages);
	KUNIT_ASSERT_NOT_NULL(test, tt->swap_storage);
	KUNIT_ASSERT_TRUE(test, tt->page_flags & TTM_TT_FLAG_SWAPPED);

	/* Swapout depopulates TT, allocate pages and then swap them in */
	err = ttm_pool_alloc(&devs->ttm_dev->pool, tt, &ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	err = ttm_tt_swapin(tt);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_NULL(test, tt->swap_storage);
	KUNIT_ASSERT_FALSE(test, tt->page_flags & TTM_TT_FLAG_SWAPPED);
}

static struct kunit_case ttm_tt_test_cases[] = {
	KUNIT_CASE_PARAM(ttm_tt_init_basic, ttm_tt_init_basic_gen_params),
	KUNIT_CASE(ttm_tt_init_misaligned),
	KUNIT_CASE(ttm_tt_fini_basic),
	KUNIT_CASE(ttm_tt_fini_sg),
	KUNIT_CASE(ttm_tt_fini_shmem),
	KUNIT_CASE(ttm_tt_create_basic),
	KUNIT_CASE(ttm_tt_create_invalid_bo_type),
	KUNIT_CASE(ttm_tt_create_ttm_exists),
	KUNIT_CASE(ttm_tt_create_failed),
	KUNIT_CASE(ttm_tt_destroy_basic),
	KUNIT_CASE(ttm_tt_populate_null_ttm),
	KUNIT_CASE(ttm_tt_populate_populated_ttm),
	KUNIT_CASE(ttm_tt_unpopulate_basic),
	KUNIT_CASE(ttm_tt_unpopulate_empty_ttm),
	KUNIT_CASE(ttm_tt_swapin_basic),
	{}
};

static struct kunit_suite ttm_tt_test_suite = {
	.name = "ttm_tt",
	.init = ttm_test_devices_all_init,
	.exit = ttm_test_devices_fini,
	.test_cases = ttm_tt_test_cases,
};

kunit_test_suites(&ttm_tt_test_suite);

MODULE_DESCRIPTION("KUnit tests for ttm_tt APIs");
MODULE_LICENSE("GPL and additional rights");
