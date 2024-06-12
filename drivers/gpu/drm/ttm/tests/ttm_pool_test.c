// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <linux/mm.h>

#include <drm/ttm/ttm_tt.h>
#include <drm/ttm/ttm_pool.h>

#include "ttm_kunit_helpers.h"

struct ttm_pool_test_case {
	const char *description;
	unsigned int order;
	bool use_dma_alloc;
};

struct ttm_pool_test_priv {
	struct ttm_test_devices *devs;

	/* Used to create mock ttm_tts */
	struct ttm_buffer_object *mock_bo;
};

static struct ttm_operation_ctx simple_ctx = {
	.interruptible = true,
	.no_wait_gpu = false,
};

static int ttm_pool_test_init(struct kunit *test)
{
	struct ttm_pool_test_priv *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	priv->devs = ttm_test_devices_basic(test);
	test->priv = priv;

	return 0;
}

static void ttm_pool_test_fini(struct kunit *test)
{
	struct ttm_pool_test_priv *priv = test->priv;

	ttm_test_devices_put(test, priv->devs);
}

static struct ttm_tt *ttm_tt_kunit_init(struct kunit *test,
					uint32_t page_flags,
					enum ttm_caching caching,
					size_t size)
{
	struct ttm_pool_test_priv *priv = test->priv;
	struct ttm_buffer_object *bo;
	struct ttm_tt *tt;
	int err;

	bo = ttm_bo_kunit_init(test, priv->devs, size, NULL);
	KUNIT_ASSERT_NOT_NULL(test, bo);
	priv->mock_bo = bo;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	err = ttm_tt_init(tt, priv->mock_bo, page_flags, caching, 0);
	KUNIT_ASSERT_EQ(test, err, 0);

	return tt;
}

static struct ttm_pool *ttm_pool_pre_populated(struct kunit *test,
					       size_t size,
					       enum ttm_caching caching)
{
	struct ttm_pool_test_priv *priv = test->priv;
	struct ttm_test_devices *devs = priv->devs;
	struct ttm_pool *pool;
	struct ttm_tt *tt;
	int err;

	tt = ttm_tt_kunit_init(test, 0, caching, size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);

	ttm_pool_init(pool, devs->dev, NUMA_NO_NODE, true, false);

	err = ttm_pool_alloc(pool, tt, &simple_ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);

	return pool;
}

static const struct ttm_pool_test_case ttm_pool_basic_cases[] = {
	{
		.description = "One page",
		.order = 0,
	},
	{
		.description = "More than one page",
		.order = 2,
	},
	{
		.description = "Above the allocation limit",
		.order = MAX_PAGE_ORDER + 1,
	},
	{
		.description = "One page, with coherent DMA mappings enabled",
		.order = 0,
		.use_dma_alloc = true,
	},
	{
		.description = "Above the allocation limit, with coherent DMA mappings enabled",
		.order = MAX_PAGE_ORDER + 1,
		.use_dma_alloc = true,
	},
};

static void ttm_pool_alloc_case_desc(const struct ttm_pool_test_case *t,
				     char *desc)
{
	strscpy(desc, t->description, KUNIT_PARAM_DESC_SIZE);
}

KUNIT_ARRAY_PARAM(ttm_pool_alloc_basic, ttm_pool_basic_cases,
		  ttm_pool_alloc_case_desc);

static void ttm_pool_alloc_basic(struct kunit *test)
{
	struct ttm_pool_test_priv *priv = test->priv;
	struct ttm_test_devices *devs = priv->devs;
	const struct ttm_pool_test_case *params = test->param_value;
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct page *fst_page, *last_page;
	enum ttm_caching caching = ttm_uncached;
	unsigned int expected_num_pages = 1 << params->order;
	size_t size = expected_num_pages * PAGE_SIZE;
	int err;

	tt = ttm_tt_kunit_init(test, 0, caching, size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);

	ttm_pool_init(pool, devs->dev, NUMA_NO_NODE, params->use_dma_alloc,
		      false);

	KUNIT_ASSERT_PTR_EQ(test, pool->dev, devs->dev);
	KUNIT_ASSERT_EQ(test, pool->nid, NUMA_NO_NODE);
	KUNIT_ASSERT_EQ(test, pool->use_dma_alloc, params->use_dma_alloc);

	err = ttm_pool_alloc(pool, tt, &simple_ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, tt->num_pages, expected_num_pages);

	fst_page = tt->pages[0];
	last_page = tt->pages[tt->num_pages - 1];

	if (params->order <= MAX_PAGE_ORDER) {
		if (params->use_dma_alloc) {
			KUNIT_ASSERT_NOT_NULL(test, (void *)fst_page->private);
			KUNIT_ASSERT_NOT_NULL(test, (void *)last_page->private);
		} else {
			KUNIT_ASSERT_EQ(test, fst_page->private, params->order);
		}
	} else {
		if (params->use_dma_alloc) {
			KUNIT_ASSERT_NOT_NULL(test, (void *)fst_page->private);
			KUNIT_ASSERT_NULL(test, (void *)last_page->private);
		} else {
			/*
			 * We expect to alloc one big block, followed by
			 * order 0 blocks
			 */
			KUNIT_ASSERT_EQ(test, fst_page->private,
					min_t(unsigned int, MAX_PAGE_ORDER,
					      params->order));
			KUNIT_ASSERT_EQ(test, last_page->private, 0);
		}
	}

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);
	ttm_pool_fini(pool);
}

static void ttm_pool_alloc_basic_dma_addr(struct kunit *test)
{
	struct ttm_pool_test_priv *priv = test->priv;
	struct ttm_test_devices *devs = priv->devs;
	const struct ttm_pool_test_case *params = test->param_value;
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct ttm_buffer_object *bo;
	dma_addr_t dma1, dma2;
	enum ttm_caching caching = ttm_uncached;
	unsigned int expected_num_pages = 1 << params->order;
	size_t size = expected_num_pages * PAGE_SIZE;
	int err;

	tt = kunit_kzalloc(test, sizeof(*tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	bo = ttm_bo_kunit_init(test, devs, size, NULL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	err = ttm_sg_tt_init(tt, bo, 0, caching);
	KUNIT_ASSERT_EQ(test, err, 0);

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);

	ttm_pool_init(pool, devs->dev, NUMA_NO_NODE, true, false);

	err = ttm_pool_alloc(pool, tt, &simple_ctx);
	KUNIT_ASSERT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, tt->num_pages, expected_num_pages);

	dma1 = tt->dma_address[0];
	dma2 = tt->dma_address[tt->num_pages - 1];

	KUNIT_ASSERT_NOT_NULL(test, (void *)(uintptr_t)dma1);
	KUNIT_ASSERT_NOT_NULL(test, (void *)(uintptr_t)dma2);

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);
	ttm_pool_fini(pool);
}

static void ttm_pool_alloc_order_caching_match(struct kunit *test)
{
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct ttm_pool_type *pt;
	enum ttm_caching caching = ttm_uncached;
	unsigned int order = 0;
	size_t size = PAGE_SIZE;
	int err;

	pool = ttm_pool_pre_populated(test, size, caching);

	pt = &pool->caching[caching].orders[order];
	KUNIT_ASSERT_FALSE(test, list_empty(&pt->pages));

	tt = ttm_tt_kunit_init(test, 0, caching, size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	err = ttm_pool_alloc(pool, tt, &simple_ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	KUNIT_ASSERT_TRUE(test, list_empty(&pt->pages));

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);
	ttm_pool_fini(pool);
}

static void ttm_pool_alloc_caching_mismatch(struct kunit *test)
{
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct ttm_pool_type *pt_pool, *pt_tt;
	enum ttm_caching tt_caching = ttm_uncached;
	enum ttm_caching pool_caching = ttm_cached;
	size_t size = PAGE_SIZE;
	unsigned int order = 0;
	int err;

	pool = ttm_pool_pre_populated(test, size, pool_caching);

	pt_pool = &pool->caching[pool_caching].orders[order];
	pt_tt = &pool->caching[tt_caching].orders[order];

	tt = ttm_tt_kunit_init(test, 0, tt_caching, size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	KUNIT_ASSERT_FALSE(test, list_empty(&pt_pool->pages));
	KUNIT_ASSERT_TRUE(test, list_empty(&pt_tt->pages));

	err = ttm_pool_alloc(pool, tt, &simple_ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);

	KUNIT_ASSERT_FALSE(test, list_empty(&pt_pool->pages));
	KUNIT_ASSERT_FALSE(test, list_empty(&pt_tt->pages));

	ttm_pool_fini(pool);
}

static void ttm_pool_alloc_order_mismatch(struct kunit *test)
{
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct ttm_pool_type *pt_pool, *pt_tt;
	enum ttm_caching caching = ttm_uncached;
	unsigned int order = 2;
	size_t fst_size = (1 << order) * PAGE_SIZE;
	size_t snd_size = PAGE_SIZE;
	int err;

	pool = ttm_pool_pre_populated(test, fst_size, caching);

	pt_pool = &pool->caching[caching].orders[order];
	pt_tt = &pool->caching[caching].orders[0];

	tt = ttm_tt_kunit_init(test, 0, caching, snd_size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	KUNIT_ASSERT_FALSE(test, list_empty(&pt_pool->pages));
	KUNIT_ASSERT_TRUE(test, list_empty(&pt_tt->pages));

	err = ttm_pool_alloc(pool, tt, &simple_ctx);
	KUNIT_ASSERT_EQ(test, err, 0);

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);

	KUNIT_ASSERT_FALSE(test, list_empty(&pt_pool->pages));
	KUNIT_ASSERT_FALSE(test, list_empty(&pt_tt->pages));

	ttm_pool_fini(pool);
}

static void ttm_pool_free_dma_alloc(struct kunit *test)
{
	struct ttm_pool_test_priv *priv = test->priv;
	struct ttm_test_devices *devs = priv->devs;
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct ttm_pool_type *pt;
	enum ttm_caching caching = ttm_uncached;
	unsigned int order = 2;
	size_t size = (1 << order) * PAGE_SIZE;

	tt = ttm_tt_kunit_init(test, 0, caching, size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);

	ttm_pool_init(pool, devs->dev, NUMA_NO_NODE, true, false);
	ttm_pool_alloc(pool, tt, &simple_ctx);

	pt = &pool->caching[caching].orders[order];
	KUNIT_ASSERT_TRUE(test, list_empty(&pt->pages));

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);

	KUNIT_ASSERT_FALSE(test, list_empty(&pt->pages));

	ttm_pool_fini(pool);
}

static void ttm_pool_free_no_dma_alloc(struct kunit *test)
{
	struct ttm_pool_test_priv *priv = test->priv;
	struct ttm_test_devices *devs = priv->devs;
	struct ttm_tt *tt;
	struct ttm_pool *pool;
	struct ttm_pool_type *pt;
	enum ttm_caching caching = ttm_uncached;
	unsigned int order = 2;
	size_t size = (1 << order) * PAGE_SIZE;

	tt = ttm_tt_kunit_init(test, 0, caching, size);
	KUNIT_ASSERT_NOT_NULL(test, tt);

	pool = kunit_kzalloc(test, sizeof(*pool), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, pool);

	ttm_pool_init(pool, devs->dev, NUMA_NO_NODE, false, false);
	ttm_pool_alloc(pool, tt, &simple_ctx);

	pt = &pool->caching[caching].orders[order];
	KUNIT_ASSERT_TRUE(test, list_is_singular(&pt->pages));

	ttm_pool_free(pool, tt);
	ttm_tt_fini(tt);

	KUNIT_ASSERT_TRUE(test, list_is_singular(&pt->pages));

	ttm_pool_fini(pool);
}

static void ttm_pool_fini_basic(struct kunit *test)
{
	struct ttm_pool *pool;
	struct ttm_pool_type *pt;
	enum ttm_caching caching = ttm_uncached;
	unsigned int order = 0;
	size_t size = PAGE_SIZE;

	pool = ttm_pool_pre_populated(test, size, caching);
	pt = &pool->caching[caching].orders[order];

	KUNIT_ASSERT_FALSE(test, list_empty(&pt->pages));

	ttm_pool_fini(pool);

	KUNIT_ASSERT_TRUE(test, list_empty(&pt->pages));
}

static struct kunit_case ttm_pool_test_cases[] = {
	KUNIT_CASE_PARAM(ttm_pool_alloc_basic, ttm_pool_alloc_basic_gen_params),
	KUNIT_CASE_PARAM(ttm_pool_alloc_basic_dma_addr,
			 ttm_pool_alloc_basic_gen_params),
	KUNIT_CASE(ttm_pool_alloc_order_caching_match),
	KUNIT_CASE(ttm_pool_alloc_caching_mismatch),
	KUNIT_CASE(ttm_pool_alloc_order_mismatch),
	KUNIT_CASE(ttm_pool_free_dma_alloc),
	KUNIT_CASE(ttm_pool_free_no_dma_alloc),
	KUNIT_CASE(ttm_pool_fini_basic),
	{}
};

static struct kunit_suite ttm_pool_test_suite = {
	.name = "ttm_pool",
	.init = ttm_pool_test_init,
	.exit = ttm_pool_test_fini,
	.test_cases = ttm_pool_test_cases,
};

kunit_test_suites(&ttm_pool_test_suite);

MODULE_LICENSE("GPL");
