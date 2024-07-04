// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/ttm/ttm_tt.h>

#include "ttm_kunit_helpers.h"

static const struct ttm_place sys_place = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = TTM_PL_SYSTEM,
	.flags = TTM_PL_FLAG_FALLBACK,
};

static const struct ttm_place mock1_place = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = TTM_PL_MOCK1,
	.flags = TTM_PL_FLAG_FALLBACK,
};

static const struct ttm_place mock2_place = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = TTM_PL_MOCK2,
	.flags = TTM_PL_FLAG_FALLBACK,
};

static struct ttm_placement sys_placement = {
	.num_placement = 1,
	.placement = &sys_place,
};

static struct ttm_placement bad_placement = {
	.num_placement = 1,
	.placement = &mock1_place,
};

static struct ttm_placement mock_placement = {
	.num_placement = 1,
	.placement = &mock2_place,
};

static struct ttm_tt *ttm_tt_simple_create(struct ttm_buffer_object *bo, u32 page_flags)
{
	struct ttm_tt *tt;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	ttm_tt_init(tt, bo, page_flags, ttm_cached, 0);

	return tt;
}

static void ttm_tt_simple_destroy(struct ttm_device *bdev, struct ttm_tt *ttm)
{
	kfree(ttm);
}

static int mock_move(struct ttm_buffer_object *bo, bool evict,
		     struct ttm_operation_ctx *ctx,
		     struct ttm_resource *new_mem,
		     struct ttm_place *hop)
{
	struct ttm_resource *old_mem = bo->resource;

	if (!old_mem || (old_mem->mem_type == TTM_PL_SYSTEM && !bo->ttm)) {
		ttm_bo_move_null(bo, new_mem);
		return 0;
	}

	if (bo->resource->mem_type == TTM_PL_VRAM &&
	    new_mem->mem_type == TTM_PL_SYSTEM) {
		hop->mem_type = TTM_PL_TT;
		hop->flags = TTM_PL_FLAG_TEMPORARY;
		hop->fpfn = 0;
		hop->lpfn = 0;
		return -EMULTIHOP;
	}

	if ((old_mem->mem_type == TTM_PL_SYSTEM &&
	     new_mem->mem_type == TTM_PL_TT) ||
	    (old_mem->mem_type == TTM_PL_TT &&
	     new_mem->mem_type == TTM_PL_SYSTEM)) {
		ttm_bo_move_null(bo, new_mem);
		return 0;
	}

	return ttm_bo_move_memcpy(bo, ctx, new_mem);
}

static void mock_evict_flags(struct ttm_buffer_object *bo,
			     struct ttm_placement *placement)
{
	switch (bo->resource->mem_type) {
	case TTM_PL_VRAM:
	case TTM_PL_SYSTEM:
		*placement = sys_placement;
		break;
	case TTM_PL_TT:
		*placement = mock_placement;
		break;
	case TTM_PL_MOCK1:
		/* Purge objects coming from this domain */
		break;
	}
}

static void bad_evict_flags(struct ttm_buffer_object *bo,
			    struct ttm_placement *placement)
{
	*placement = bad_placement;
}

static int ttm_device_kunit_init_with_funcs(struct ttm_test_devices *priv,
					    struct ttm_device *ttm,
					    bool use_dma_alloc,
					    bool use_dma32,
					    struct ttm_device_funcs *funcs)
{
	struct drm_device *drm = priv->drm;
	int err;

	err = ttm_device_init(ttm, funcs, drm->dev,
			      drm->anon_inode->i_mapping,
			      drm->vma_offset_manager,
			      use_dma_alloc, use_dma32);

	return err;
}

struct ttm_device_funcs ttm_dev_funcs = {
	.ttm_tt_create = ttm_tt_simple_create,
	.ttm_tt_destroy = ttm_tt_simple_destroy,
	.move = mock_move,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = mock_evict_flags,
};
EXPORT_SYMBOL_GPL(ttm_dev_funcs);

int ttm_device_kunit_init(struct ttm_test_devices *priv,
			  struct ttm_device *ttm,
			  bool use_dma_alloc,
			  bool use_dma32)
{
	return ttm_device_kunit_init_with_funcs(priv, ttm, use_dma_alloc,
						use_dma32, &ttm_dev_funcs);
}
EXPORT_SYMBOL_GPL(ttm_device_kunit_init);

struct ttm_device_funcs ttm_dev_funcs_bad_evict = {
	.ttm_tt_create = ttm_tt_simple_create,
	.ttm_tt_destroy = ttm_tt_simple_destroy,
	.move = mock_move,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.evict_flags = bad_evict_flags,
};
EXPORT_SYMBOL_GPL(ttm_dev_funcs_bad_evict);

int ttm_device_kunit_init_bad_evict(struct ttm_test_devices *priv,
				    struct ttm_device *ttm,
				    bool use_dma_alloc,
				    bool use_dma32)
{
	return ttm_device_kunit_init_with_funcs(priv, ttm, use_dma_alloc,
						use_dma32, &ttm_dev_funcs_bad_evict);
}
EXPORT_SYMBOL_GPL(ttm_device_kunit_init_bad_evict);

struct ttm_buffer_object *ttm_bo_kunit_init(struct kunit *test,
					    struct ttm_test_devices *devs,
					    size_t size,
					    struct dma_resv *obj)
{
	struct drm_gem_object gem_obj = { };
	struct ttm_buffer_object *bo;
	int err;

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	bo->base = gem_obj;

	if (obj)
		bo->base.resv = obj;

	err = drm_gem_object_init(devs->drm, &bo->base, size);
	KUNIT_ASSERT_EQ(test, err, 0);

	bo->bdev = devs->ttm_dev;
	bo->destroy = dummy_ttm_bo_destroy;

	kref_init(&bo->kref);

	return bo;
}
EXPORT_SYMBOL_GPL(ttm_bo_kunit_init);

struct ttm_place *ttm_place_kunit_init(struct kunit *test, u32 mem_type, u32 flags)
{
	struct ttm_place *place;

	place = kunit_kzalloc(test, sizeof(*place), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, place);

	place->mem_type = mem_type;
	place->flags = flags;

	return place;
}
EXPORT_SYMBOL_GPL(ttm_place_kunit_init);

void dummy_ttm_bo_destroy(struct ttm_buffer_object *bo)
{
	drm_gem_object_release(&bo->base);
}
EXPORT_SYMBOL_GPL(dummy_ttm_bo_destroy);

struct ttm_test_devices *ttm_test_devices_basic(struct kunit *test)
{
	struct ttm_test_devices *devs;

	devs = kunit_kzalloc(test, sizeof(*devs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, devs);

	devs->dev = drm_kunit_helper_alloc_device(test);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, devs->dev);

	/* Set mask for alloc_coherent mappings to enable ttm_pool_alloc testing */
	devs->dev->coherent_dma_mask = -1;

	devs->drm = __drm_kunit_helper_alloc_drm_device(test, devs->dev,
							sizeof(*devs->drm), 0,
							DRIVER_GEM);
	KUNIT_ASSERT_NOT_ERR_OR_NULL(test, devs->drm);

	return devs;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_basic);

struct ttm_test_devices *ttm_test_devices_all(struct kunit *test)
{
	struct ttm_test_devices *devs;
	struct ttm_device *ttm_dev;
	int err;

	devs = ttm_test_devices_basic(test);

	ttm_dev = kunit_kzalloc(test, sizeof(*ttm_dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, ttm_dev);

	err = ttm_device_kunit_init(devs, ttm_dev, false, false);
	KUNIT_ASSERT_EQ(test, err, 0);

	devs->ttm_dev = ttm_dev;

	return devs;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_all);

void ttm_test_devices_put(struct kunit *test, struct ttm_test_devices *devs)
{
	if (devs->ttm_dev)
		ttm_device_fini(devs->ttm_dev);

	drm_kunit_helper_free_device(test, devs->dev);
}
EXPORT_SYMBOL_GPL(ttm_test_devices_put);

int ttm_test_devices_init(struct kunit *test)
{
	struct ttm_test_devices *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	priv = ttm_test_devices_basic(test);
	test->priv = priv;

	return 0;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_init);

int ttm_test_devices_all_init(struct kunit *test)
{
	struct ttm_test_devices *priv;

	priv = kunit_kzalloc(test, sizeof(*priv), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, priv);

	priv = ttm_test_devices_all(test);
	test->priv = priv;

	return 0;
}
EXPORT_SYMBOL_GPL(ttm_test_devices_all_init);

void ttm_test_devices_fini(struct kunit *test)
{
	ttm_test_devices_put(test, test->priv);
}
EXPORT_SYMBOL_GPL(ttm_test_devices_fini);

MODULE_DESCRIPTION("TTM KUnit test helper functions");
MODULE_LICENSE("GPL and additional rights");
