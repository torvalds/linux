// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <linux/delay.h>
#include <linux/kthread.h>

#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include "ttm_kunit_helpers.h"
#include "ttm_mock_manager.h"

#define BO_SIZE		SZ_4K
#define MANAGER_SIZE	SZ_1M

static struct spinlock fence_lock;

struct ttm_bo_validate_test_case {
	const char *description;
	enum ttm_bo_type bo_type;
	u32 mem_type;
	bool with_ttm;
	bool no_gpu_wait;
};

static struct ttm_placement *ttm_placement_kunit_init(struct kunit *test,
						      struct ttm_place *places,
						      unsigned int num_places)
{
	struct ttm_placement *placement;

	placement = kunit_kzalloc(test, sizeof(*placement), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, placement);

	placement->num_placement = num_places;
	placement->placement = places;

	return placement;
}

static const char *fence_name(struct dma_fence *f)
{
	return "ttm-bo-validate-fence";
}

static const struct dma_fence_ops fence_ops = {
	.get_driver_name = fence_name,
	.get_timeline_name = fence_name,
};

static struct dma_fence *alloc_mock_fence(struct kunit *test)
{
	struct dma_fence *fence;

	fence = kunit_kzalloc(test, sizeof(*fence), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, fence);

	dma_fence_init(fence, &fence_ops, &fence_lock, 0, 0);

	return fence;
}

static void dma_resv_kunit_active_fence_init(struct kunit *test,
					     struct dma_resv *resv,
					     enum dma_resv_usage usage)
{
	struct dma_fence *fence;

	fence = alloc_mock_fence(test);
	dma_fence_enable_sw_signaling(fence);

	dma_resv_lock(resv, NULL);
	dma_resv_reserve_fences(resv, 1);
	dma_resv_add_fence(resv, fence, usage);
	dma_resv_unlock(resv);
}

static void ttm_bo_validate_case_desc(const struct ttm_bo_validate_test_case *t,
				      char *desc)
{
	strscpy(desc, t->description, KUNIT_PARAM_DESC_SIZE);
}

static const struct ttm_bo_validate_test_case ttm_bo_type_cases[] = {
	{
		.description = "Buffer object for userspace",
		.bo_type = ttm_bo_type_device,
	},
	{
		.description = "Kernel buffer object",
		.bo_type = ttm_bo_type_kernel,
	},
	{
		.description = "Shared buffer object",
		.bo_type = ttm_bo_type_sg,
	},
};

KUNIT_ARRAY_PARAM(ttm_bo_types, ttm_bo_type_cases,
		  ttm_bo_validate_case_desc);

static void ttm_bo_init_reserved_sys_man(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	struct ttm_test_devices *priv = test->priv;
	enum ttm_bo_type bo_type = params->bo_type;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	place = ttm_place_kunit_init(test, TTM_PL_SYSTEM, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, bo_type, placement,
				   PAGE_SIZE, &ctx, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, kref_read(&bo->kref), 1);
	KUNIT_EXPECT_PTR_EQ(test, bo->bdev, priv->ttm_dev);
	KUNIT_EXPECT_EQ(test, bo->type, bo_type);
	KUNIT_EXPECT_EQ(test, bo->page_alignment, PAGE_SIZE);
	KUNIT_EXPECT_PTR_EQ(test, bo->destroy, &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, bo->pin_count, 0);
	KUNIT_EXPECT_NULL(test, bo->bulk_move);
	KUNIT_EXPECT_NOT_NULL(test, bo->ttm);
	KUNIT_EXPECT_FALSE(test, ttm_tt_is_populated(bo->ttm));
	KUNIT_EXPECT_NOT_NULL(test, (void *)bo->base.resv->fences);
	KUNIT_EXPECT_EQ(test, ctx.bytes_moved, size);

	if (bo_type != ttm_bo_type_kernel)
		KUNIT_EXPECT_TRUE(test,
				  drm_mm_node_allocated(&bo->base.vma_node.vm_node));

	ttm_resource_free(bo, &bo->resource);
	ttm_bo_put(bo);
}

static void ttm_bo_init_reserved_mock_man(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	enum ttm_bo_type bo_type = params->bo_type;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	struct ttm_placement *placement;
	u32 mem_type = TTM_PL_VRAM;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, bo_type, placement,
				   PAGE_SIZE, &ctx, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, kref_read(&bo->kref), 1);
	KUNIT_EXPECT_PTR_EQ(test, bo->bdev, priv->ttm_dev);
	KUNIT_EXPECT_EQ(test, bo->type, bo_type);
	KUNIT_EXPECT_EQ(test, ctx.bytes_moved, size);

	if (bo_type != ttm_bo_type_kernel)
		KUNIT_EXPECT_TRUE(test,
				  drm_mm_node_allocated(&bo->base.vma_node.vm_node));

	ttm_resource_free(bo, &bo->resource);
	ttm_bo_put(bo);
	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
}

static void ttm_bo_init_reserved_resv(struct kunit *test)
{
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct dma_resv resv;
	int err;

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	place = ttm_place_kunit_init(test, TTM_PL_SYSTEM, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	drm_gem_private_object_init(priv->drm, &bo->base, size);
	dma_resv_init(&resv);
	dma_resv_lock(&resv, NULL);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, bo_type, placement,
				   PAGE_SIZE, &ctx, NULL, &resv,
				   &dummy_ttm_bo_destroy);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_PTR_EQ(test, bo->base.resv, &resv);

	ttm_resource_free(bo, &bo->resource);
	ttm_bo_put(bo);
}

static void ttm_bo_validate_basic(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	u32 fst_mem = TTM_PL_SYSTEM, snd_mem = TTM_PL_VRAM;
	struct ttm_operation_ctx ctx_init = { }, ctx_val = { };
	struct ttm_placement *fst_placement, *snd_placement;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_place *fst_place, *snd_place;
	u32 size = ALIGN(SZ_8K, PAGE_SIZE);
	struct ttm_buffer_object *bo;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, snd_mem, MANAGER_SIZE);

	fst_place = ttm_place_kunit_init(test, fst_mem, 0);
	fst_placement = ttm_placement_kunit_init(test, fst_place, 1);

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, params->bo_type,
				   fst_placement, PAGE_SIZE, &ctx_init, NULL,
				   NULL, &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);

	snd_place = ttm_place_kunit_init(test, snd_mem, DRM_BUDDY_TOPDOWN_ALLOCATION);
	snd_placement = ttm_placement_kunit_init(test, snd_place, 1);

	err = ttm_bo_validate(bo, snd_placement, &ctx_val);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, bo->base.size);
	KUNIT_EXPECT_NOT_NULL(test, bo->ttm);
	KUNIT_EXPECT_TRUE(test, ttm_tt_is_populated(bo->ttm));
	KUNIT_EXPECT_EQ(test, bo->resource->mem_type, snd_mem);
	KUNIT_EXPECT_EQ(test, bo->resource->placement,
			DRM_BUDDY_TOPDOWN_ALLOCATION);

	ttm_bo_put(bo);
	ttm_mock_manager_fini(priv->ttm_dev, snd_mem);
}

static void ttm_bo_validate_invalid_placement(struct kunit *test)
{
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	u32 unknown_mem_type = TTM_PL_PRIV + 1;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	place = ttm_place_kunit_init(test, unknown_mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);
	bo->type = bo_type;

	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, -ENOMEM);

	ttm_bo_put(bo);
}

static void ttm_bo_validate_failed_alloc(struct kunit *test)
{
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	struct ttm_placement *placement;
	u32 mem_type = TTM_PL_VRAM;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);
	bo->type = bo_type;

	ttm_bad_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, -ENOMEM);

	ttm_bo_put(bo);
	ttm_bad_manager_fini(priv->ttm_dev, mem_type);
}

static void ttm_bo_validate_pinned(struct kunit *test)
{
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	u32 mem_type = TTM_PL_SYSTEM;
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);
	bo->type = bo_type;

	ttm_bo_reserve(bo, false, false, NULL);
	ttm_bo_pin(bo);
	err = ttm_bo_validate(bo, placement, &ctx);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, -EINVAL);

	ttm_bo_reserve(bo, false, false, NULL);
	ttm_bo_unpin(bo);
	dma_resv_unlock(bo->base.resv);

	ttm_bo_put(bo);
}

static const struct ttm_bo_validate_test_case ttm_mem_type_cases[] = {
	{
		.description = "System manager",
		.mem_type = TTM_PL_SYSTEM,
	},
	{
		.description = "VRAM manager",
		.mem_type = TTM_PL_VRAM,
	},
};

KUNIT_ARRAY_PARAM(ttm_bo_validate_mem, ttm_mem_type_cases,
		  ttm_bo_validate_case_desc);

static void ttm_bo_validate_same_placement(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	struct ttm_operation_ctx ctx_init = { }, ctx_val = { };
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	place = ttm_place_kunit_init(test, params->mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	if (params->mem_type != TTM_PL_SYSTEM)
		ttm_mock_manager_init(priv->ttm_dev, params->mem_type, MANAGER_SIZE);

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, params->bo_type,
				   placement, PAGE_SIZE, &ctx_init, NULL,
				   NULL, &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);

	err = ttm_bo_validate(bo, placement, &ctx_val);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, 0);

	ttm_bo_put(bo);

	if (params->mem_type != TTM_PL_SYSTEM)
		ttm_mock_manager_fini(priv->ttm_dev, params->mem_type);
}

static void ttm_bo_validate_busy_placement(struct kunit *test)
{
	u32 fst_mem = TTM_PL_VRAM, snd_mem = TTM_PL_VRAM + 1;
	struct ttm_operation_ctx ctx_init = { }, ctx_val = { };
	struct ttm_placement *placement_init, *placement_val;
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_place *init_place, places[2];
	struct ttm_resource_manager *man;
	struct ttm_buffer_object *bo;
	int err;

	ttm_bad_manager_init(priv->ttm_dev, fst_mem, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, snd_mem, MANAGER_SIZE);

	init_place = ttm_place_kunit_init(test, TTM_PL_SYSTEM, 0);
	placement_init = ttm_placement_kunit_init(test, init_place, 1);

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, bo_type, placement_init,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);

	places[0] = (struct ttm_place){ .mem_type = fst_mem, .flags = TTM_PL_FLAG_DESIRED };
	places[1] = (struct ttm_place){ .mem_type = snd_mem, .flags = TTM_PL_FLAG_FALLBACK };
	placement_val = ttm_placement_kunit_init(test, places, 2);

	err = ttm_bo_validate(bo, placement_val, &ctx_val);
	dma_resv_unlock(bo->base.resv);

	man = ttm_manager_type(priv->ttm_dev, snd_mem);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, bo->base.size);
	KUNIT_EXPECT_EQ(test, bo->resource->mem_type, snd_mem);
	KUNIT_ASSERT_TRUE(test, list_is_singular(&man->lru[bo->priority]));

	ttm_bo_put(bo);
	ttm_bad_manager_fini(priv->ttm_dev, fst_mem);
	ttm_mock_manager_fini(priv->ttm_dev, snd_mem);
}

static void ttm_bo_validate_multihop(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	struct ttm_operation_ctx ctx_init = { }, ctx_val = { };
	struct ttm_placement *placement_init, *placement_val;
	u32 fst_mem = TTM_PL_VRAM, tmp_mem = TTM_PL_TT, final_mem = TTM_PL_SYSTEM;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_place *fst_place, *final_place;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_buffer_object *bo;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, fst_mem, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, tmp_mem, MANAGER_SIZE);

	fst_place = ttm_place_kunit_init(test, fst_mem, 0);
	placement_init = ttm_placement_kunit_init(test, fst_place, 1);

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, params->bo_type,
				   placement_init, PAGE_SIZE, &ctx_init, NULL,
				   NULL, &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);

	final_place = ttm_place_kunit_init(test, final_mem, 0);
	placement_val = ttm_placement_kunit_init(test, final_place, 1);

	err = ttm_bo_validate(bo, placement_val, &ctx_val);
	dma_resv_unlock(bo->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, size * 2);
	KUNIT_EXPECT_EQ(test, bo->resource->mem_type, final_mem);

	ttm_bo_put(bo);

	ttm_mock_manager_fini(priv->ttm_dev, fst_mem);
	ttm_mock_manager_fini(priv->ttm_dev, tmp_mem);
}

static const struct ttm_bo_validate_test_case ttm_bo_no_placement_cases[] = {
	{
		.description = "Buffer object in system domain, no page vector",
	},
	{
		.description = "Buffer object in system domain with an existing page vector",
		.with_ttm = true,
	},
};

KUNIT_ARRAY_PARAM(ttm_bo_no_placement, ttm_bo_no_placement_cases,
		  ttm_bo_validate_case_desc);

static void ttm_bo_validate_no_placement_signaled(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	u32 mem_type = TTM_PL_SYSTEM;
	struct ttm_resource_manager *man;
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	struct ttm_tt *old_tt;
	u32 flags;
	int err;

	place = ttm_place_kunit_init(test, mem_type, 0);
	man = ttm_manager_type(priv->ttm_dev, mem_type);

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);
	bo->type = bo_type;

	if (params->with_ttm) {
		old_tt = priv->ttm_dev->funcs->ttm_tt_create(bo, 0);
		ttm_pool_alloc(&priv->ttm_dev->pool, old_tt, &ctx);
		bo->ttm = old_tt;
	}

	err = ttm_resource_alloc(bo, place, &bo->resource, NULL);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, man->usage, size);

	placement = kunit_kzalloc(test, sizeof(*placement), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, placement);

	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx);
	ttm_bo_unreserve(bo);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_ASSERT_EQ(test, man->usage, 0);
	KUNIT_ASSERT_NOT_NULL(test, bo->ttm);
	KUNIT_EXPECT_EQ(test, ctx.bytes_moved, 0);

	if (params->with_ttm) {
		flags = bo->ttm->page_flags;

		KUNIT_ASSERT_PTR_EQ(test, bo->ttm, old_tt);
		KUNIT_ASSERT_FALSE(test, flags & TTM_TT_FLAG_PRIV_POPULATED);
		KUNIT_ASSERT_TRUE(test, flags & TTM_TT_FLAG_ZERO_ALLOC);
	}

	ttm_bo_put(bo);
}

static int threaded_dma_resv_signal(void *arg)
{
	struct ttm_buffer_object *bo = arg;
	struct dma_resv *resv = bo->base.resv;
	struct dma_resv_iter cursor;
	struct dma_fence *fence;

	dma_resv_iter_begin(&cursor, resv, DMA_RESV_USAGE_BOOKKEEP);
	dma_resv_for_each_fence_unlocked(&cursor, fence) {
		dma_fence_signal(fence);
	}
	dma_resv_iter_end(&cursor);

	return 0;
}

static void ttm_bo_validate_no_placement_not_signaled(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	enum dma_resv_usage usage = DMA_RESV_USAGE_BOOKKEEP;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	u32 mem_type = TTM_PL_SYSTEM;
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct task_struct *task;
	struct ttm_place *place;
	int err;

	place = ttm_place_kunit_init(test, mem_type, 0);

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);
	bo->type = params->bo_type;

	err = ttm_resource_alloc(bo, place, &bo->resource, NULL);
	KUNIT_EXPECT_EQ(test, err, 0);

	placement = kunit_kzalloc(test, sizeof(*placement), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, placement);

	/* Create an active fence to simulate a non-idle resv object */
	spin_lock_init(&fence_lock);
	dma_resv_kunit_active_fence_init(test, bo->base.resv, usage);

	task = kthread_create(threaded_dma_resv_signal, bo, "dma-resv-signal");
	if (IS_ERR(task))
		KUNIT_FAIL(test, "Couldn't create dma resv signal task\n");

	wake_up_process(task);
	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx);
	ttm_bo_unreserve(bo);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_ASSERT_NOT_NULL(test, bo->ttm);
	KUNIT_ASSERT_NULL(test, bo->resource);
	KUNIT_ASSERT_NULL(test, bo->bulk_move);
	KUNIT_EXPECT_EQ(test, ctx.bytes_moved, 0);

	if (bo->type != ttm_bo_type_sg)
		KUNIT_ASSERT_PTR_EQ(test, bo->base.resv, &bo->base._resv);

	/* Make sure we have an idle object at this point */
	dma_resv_wait_timeout(bo->base.resv, usage, false, MAX_SCHEDULE_TIMEOUT);

	ttm_bo_put(bo);
}

static void ttm_bo_validate_move_fence_signaled(struct kunit *test)
{
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_operation_ctx ctx = { };
	u32 mem_type = TTM_PL_SYSTEM;
	struct ttm_resource_manager *man;
	struct ttm_placement *placement;
	struct ttm_buffer_object *bo;
	struct ttm_place *place;
	int err;

	man = ttm_manager_type(priv->ttm_dev, mem_type);
	man->move = dma_fence_get_stub();

	bo = ttm_bo_kunit_init(test, test->priv, size, NULL);
	bo->type = bo_type;

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx);
	ttm_bo_unreserve(bo);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, bo->resource->mem_type, mem_type);
	KUNIT_EXPECT_EQ(test, ctx.bytes_moved, size);

	ttm_bo_put(bo);
	dma_fence_put(man->move);
}

static const struct ttm_bo_validate_test_case ttm_bo_validate_wait_cases[] = {
	{
		.description = "Waits for GPU",
		.no_gpu_wait = false,
	},
	{
		.description = "Tries to lock straight away",
		.no_gpu_wait = true,
	},
};

KUNIT_ARRAY_PARAM(ttm_bo_validate_wait, ttm_bo_validate_wait_cases,
		  ttm_bo_validate_case_desc);

static int threaded_fence_signal(void *arg)
{
	struct dma_fence *fence = arg;

	msleep(20);

	return dma_fence_signal(fence);
}

static void ttm_bo_validate_move_fence_not_signaled(struct kunit *test)
{
	const struct ttm_bo_validate_test_case *params = test->param_value;
	struct ttm_operation_ctx ctx_init = { },
				 ctx_val  = { .no_wait_gpu = params->no_gpu_wait };
	u32 fst_mem = TTM_PL_VRAM, snd_mem = TTM_PL_VRAM + 1;
	struct ttm_placement *placement_init, *placement_val;
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	u32 size = ALIGN(BO_SIZE, PAGE_SIZE);
	struct ttm_place *init_place, places[2];
	struct ttm_resource_manager *man;
	struct ttm_buffer_object *bo;
	struct task_struct *task;
	int err;

	init_place = ttm_place_kunit_init(test, TTM_PL_SYSTEM, 0);
	placement_init = ttm_placement_kunit_init(test, init_place, 1);

	bo = kunit_kzalloc(test, sizeof(*bo), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo);

	drm_gem_private_object_init(priv->drm, &bo->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo, bo_type, placement_init,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);

	ttm_mock_manager_init(priv->ttm_dev, fst_mem, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, snd_mem, MANAGER_SIZE);

	places[0] = (struct ttm_place){ .mem_type = fst_mem, .flags = TTM_PL_FLAG_DESIRED };
	places[1] = (struct ttm_place){ .mem_type = snd_mem, .flags = TTM_PL_FLAG_FALLBACK };
	placement_val = ttm_placement_kunit_init(test, places, 2);

	spin_lock_init(&fence_lock);
	man = ttm_manager_type(priv->ttm_dev, fst_mem);
	man->move = alloc_mock_fence(test);

	task = kthread_create(threaded_fence_signal, man->move, "move-fence-signal");
	if (IS_ERR(task))
		KUNIT_FAIL(test, "Couldn't create move fence signal task\n");

	wake_up_process(task);
	err = ttm_bo_validate(bo, placement_val, &ctx_val);
	dma_resv_unlock(bo->base.resv);

	dma_fence_wait_timeout(man->move, false, MAX_SCHEDULE_TIMEOUT);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, size);

	if (params->no_gpu_wait)
		KUNIT_EXPECT_EQ(test, bo->resource->mem_type, snd_mem);
	else
		KUNIT_EXPECT_EQ(test, bo->resource->mem_type, fst_mem);

	ttm_bo_put(bo);
	ttm_mock_manager_fini(priv->ttm_dev, fst_mem);
	ttm_mock_manager_fini(priv->ttm_dev, snd_mem);
}

static void ttm_bo_validate_swapout(struct kunit *test)
{
	unsigned long size_big, size = ALIGN(BO_SIZE, PAGE_SIZE);
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_buffer_object *bo_small, *bo_big;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_operation_ctx ctx = { };
	struct ttm_placement *placement;
	u32 mem_type = TTM_PL_TT;
	struct ttm_place *place;
	struct sysinfo si;
	int err;

	si_meminfo(&si);
	size_big = ALIGN(((u64)si.totalram * si.mem_unit / 2), PAGE_SIZE);

	ttm_mock_manager_init(priv->ttm_dev, mem_type, size_big + size);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo_small = kunit_kzalloc(test, sizeof(*bo_small), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_small);

	drm_gem_private_object_init(priv->drm, &bo_small->base, size);

	err = ttm_bo_init_reserved(priv->ttm_dev, bo_small, bo_type, placement,
				   PAGE_SIZE, &ctx, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	dma_resv_unlock(bo_small->base.resv);

	bo_big = ttm_bo_kunit_init(test, priv, size_big, NULL);

	dma_resv_lock(bo_big->base.resv, NULL);
	err = ttm_bo_validate(bo_big, placement, &ctx);
	dma_resv_unlock(bo_big->base.resv);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_NOT_NULL(test, bo_big->resource);
	KUNIT_EXPECT_EQ(test, bo_big->resource->mem_type, mem_type);
	KUNIT_EXPECT_EQ(test, bo_small->resource->mem_type, TTM_PL_SYSTEM);
	KUNIT_EXPECT_TRUE(test, bo_small->ttm->page_flags & TTM_TT_FLAG_SWAPPED);

	ttm_bo_put(bo_big);
	ttm_bo_put(bo_small);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
}

static void ttm_bo_validate_happy_evict(struct kunit *test)
{
	u32 mem_type = TTM_PL_VRAM, mem_multihop = TTM_PL_TT,
	    mem_type_evict = TTM_PL_SYSTEM;
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	u32 small = SZ_8K, medium = SZ_512K,
	    big = MANAGER_SIZE - (small + medium);
	u32 bo_sizes[] = { small, medium, big };
	struct ttm_test_devices *priv = test->priv;
	struct ttm_buffer_object *bos, *bo_val;
	struct ttm_placement *placement;
	struct ttm_place *place;
	u32 bo_no = 3;
	int i, err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, mem_multihop, MANAGER_SIZE);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bos = kunit_kmalloc_array(test, bo_no, sizeof(*bos), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bos);

	memset(bos, 0, sizeof(*bos) * bo_no);
	for (i = 0; i < bo_no; i++) {
		drm_gem_private_object_init(priv->drm, &bos[i].base, bo_sizes[i]);
		err = ttm_bo_init_reserved(priv->ttm_dev, &bos[i], bo_type, placement,
					   PAGE_SIZE, &ctx_init, NULL, NULL,
					   &dummy_ttm_bo_destroy);
		dma_resv_unlock(bos[i].base.resv);
	}

	bo_val = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo_val->type = bo_type;

	ttm_bo_reserve(bo_val, false, false, NULL);
	err = ttm_bo_validate(bo_val, placement, &ctx_val);
	ttm_bo_unreserve(bo_val);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, bos[0].resource->mem_type, mem_type_evict);
	KUNIT_EXPECT_TRUE(test, bos[0].ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC);
	KUNIT_EXPECT_TRUE(test, bos[0].ttm->page_flags & TTM_TT_FLAG_PRIV_POPULATED);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, small * 2 + BO_SIZE);
	KUNIT_EXPECT_EQ(test, bos[1].resource->mem_type, mem_type);

	for (i = 0; i < bo_no; i++)
		ttm_bo_put(&bos[i]);
	ttm_bo_put(bo_val);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
	ttm_mock_manager_fini(priv->ttm_dev, mem_multihop);
}

static void ttm_bo_validate_all_pinned_evict(struct kunit *test)
{
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_buffer_object *bo_big, *bo_small;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_placement *placement;
	u32 mem_type = TTM_PL_VRAM, mem_multihop = TTM_PL_TT;
	struct ttm_place *place;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, mem_multihop, MANAGER_SIZE);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo_big = kunit_kzalloc(test, sizeof(*bo_big), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_big);

	drm_gem_private_object_init(priv->drm, &bo_big->base, MANAGER_SIZE);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_big, bo_type, placement,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);

	ttm_bo_pin(bo_big);
	dma_resv_unlock(bo_big->base.resv);

	bo_small = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo_small->type = bo_type;

	ttm_bo_reserve(bo_small, false, false, NULL);
	err = ttm_bo_validate(bo_small, placement, &ctx_val);
	ttm_bo_unreserve(bo_small);

	KUNIT_EXPECT_EQ(test, err, -ENOMEM);

	ttm_bo_put(bo_small);

	ttm_bo_reserve(bo_big, false, false, NULL);
	ttm_bo_unpin(bo_big);
	dma_resv_unlock(bo_big->base.resv);
	ttm_bo_put(bo_big);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
	ttm_mock_manager_fini(priv->ttm_dev, mem_multihop);
}

static void ttm_bo_validate_allowed_only_evict(struct kunit *test)
{
	u32 mem_type = TTM_PL_VRAM, mem_multihop = TTM_PL_TT,
	    mem_type_evict = TTM_PL_SYSTEM;
	struct ttm_buffer_object *bo, *bo_evictable, *bo_pinned;
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_placement *placement;
	struct ttm_place *place;
	u32 size = SZ_512K;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, mem_multihop, MANAGER_SIZE);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo_pinned = kunit_kzalloc(test, sizeof(*bo_pinned), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_pinned);

	drm_gem_private_object_init(priv->drm, &bo_pinned->base, size);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_pinned, bo_type, placement,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	ttm_bo_pin(bo_pinned);
	dma_resv_unlock(bo_pinned->base.resv);

	bo_evictable = kunit_kzalloc(test, sizeof(*bo_evictable), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_evictable);

	drm_gem_private_object_init(priv->drm, &bo_evictable->base, size);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_evictable, bo_type, placement,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	dma_resv_unlock(bo_evictable->base.resv);

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo->type = bo_type;

	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx_val);
	ttm_bo_unreserve(bo);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, bo->resource->mem_type, mem_type);
	KUNIT_EXPECT_EQ(test, bo_pinned->resource->mem_type, mem_type);
	KUNIT_EXPECT_EQ(test, bo_evictable->resource->mem_type, mem_type_evict);
	KUNIT_EXPECT_EQ(test, ctx_val.bytes_moved, size * 2 + BO_SIZE);

	ttm_bo_put(bo);
	ttm_bo_put(bo_evictable);

	ttm_bo_reserve(bo_pinned, false, false, NULL);
	ttm_bo_unpin(bo_pinned);
	dma_resv_unlock(bo_pinned->base.resv);
	ttm_bo_put(bo_pinned);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
	ttm_mock_manager_fini(priv->ttm_dev, mem_multihop);
}

static void ttm_bo_validate_deleted_evict(struct kunit *test)
{
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	u32 small = SZ_8K, big = MANAGER_SIZE - BO_SIZE;
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_buffer_object *bo_big, *bo_small;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_resource_manager *man;
	u32 mem_type = TTM_PL_VRAM;
	struct ttm_placement *placement;
	struct ttm_place *place;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);
	man = ttm_manager_type(priv->ttm_dev, mem_type);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo_big = kunit_kzalloc(test, sizeof(*bo_big), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_big);

	drm_gem_private_object_init(priv->drm, &bo_big->base, big);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_big, bo_type, placement,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, ttm_resource_manager_usage(man), big);

	dma_resv_unlock(bo_big->base.resv);
	bo_big->deleted = true;

	bo_small = ttm_bo_kunit_init(test, test->priv, small, NULL);
	bo_small->type = bo_type;

	ttm_bo_reserve(bo_small, false, false, NULL);
	err = ttm_bo_validate(bo_small, placement, &ctx_val);
	ttm_bo_unreserve(bo_small);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, bo_small->resource->mem_type, mem_type);
	KUNIT_EXPECT_EQ(test, ttm_resource_manager_usage(man), small);
	KUNIT_EXPECT_NULL(test, bo_big->ttm);
	KUNIT_EXPECT_NULL(test, bo_big->resource);

	ttm_bo_put(bo_small);
	ttm_bo_put(bo_big);
	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
}

static void ttm_bo_validate_busy_domain_evict(struct kunit *test)
{
	u32 mem_type = TTM_PL_VRAM, mem_type_evict = TTM_PL_MOCK1;
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_buffer_object *bo_init, *bo_val;
	struct ttm_placement *placement;
	struct ttm_place *place;
	int err;

	/*
	 * Drop the default device and setup a new one that points to busy
	 * thus unsuitable eviction domain
	 */
	ttm_device_fini(priv->ttm_dev);

	err = ttm_device_kunit_init_bad_evict(test->priv, priv->ttm_dev, false, false);
	KUNIT_ASSERT_EQ(test, err, 0);

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);
	ttm_busy_manager_init(priv->ttm_dev, mem_type_evict, MANAGER_SIZE);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo_init = kunit_kzalloc(test, sizeof(*bo_init), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_init);

	drm_gem_private_object_init(priv->drm, &bo_init->base, MANAGER_SIZE);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_init, bo_type, placement,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	dma_resv_unlock(bo_init->base.resv);

	bo_val = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo_val->type = bo_type;

	ttm_bo_reserve(bo_val, false, false, NULL);
	err = ttm_bo_validate(bo_val, placement, &ctx_val);
	ttm_bo_unreserve(bo_val);

	KUNIT_EXPECT_EQ(test, err, -ENOMEM);
	KUNIT_EXPECT_EQ(test, bo_init->resource->mem_type, mem_type);
	KUNIT_EXPECT_NULL(test, bo_val->resource);

	ttm_bo_put(bo_init);
	ttm_bo_put(bo_val);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
	ttm_bad_manager_fini(priv->ttm_dev, mem_type_evict);
}

static void ttm_bo_validate_evict_gutting(struct kunit *test)
{
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_buffer_object *bo, *bo_evict;
	u32 mem_type = TTM_PL_MOCK1;
	struct ttm_placement *placement;
	struct ttm_place *place;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);

	place = ttm_place_kunit_init(test, mem_type, 0);
	placement = ttm_placement_kunit_init(test, place, 1);

	bo_evict = kunit_kzalloc(test, sizeof(*bo_evict), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_evict);

	drm_gem_private_object_init(priv->drm, &bo_evict->base, MANAGER_SIZE);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_evict, bo_type, placement,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	dma_resv_unlock(bo_evict->base.resv);

	bo = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo->type = bo_type;

	ttm_bo_reserve(bo, false, false, NULL);
	err = ttm_bo_validate(bo, placement, &ctx_val);
	ttm_bo_unreserve(bo);

	KUNIT_EXPECT_EQ(test, err, 0);
	KUNIT_EXPECT_EQ(test, bo->resource->mem_type, mem_type);
	KUNIT_ASSERT_NULL(test, bo_evict->resource);
	KUNIT_ASSERT_TRUE(test, bo_evict->ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC);

	ttm_bo_put(bo_evict);
	ttm_bo_put(bo);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
}

static void ttm_bo_validate_recrusive_evict(struct kunit *test)
{
	u32 mem_type = TTM_PL_TT, mem_type_evict = TTM_PL_MOCK2;
	struct ttm_operation_ctx ctx_init = { }, ctx_val  = { };
	struct ttm_placement *placement_tt, *placement_mock;
	struct ttm_buffer_object *bo_tt, *bo_mock, *bo_val;
	enum ttm_bo_type bo_type = ttm_bo_type_device;
	struct ttm_test_devices *priv = test->priv;
	struct ttm_place *place_tt, *place_mock;
	int err;

	ttm_mock_manager_init(priv->ttm_dev, mem_type, MANAGER_SIZE);
	ttm_mock_manager_init(priv->ttm_dev, mem_type_evict, MANAGER_SIZE);

	place_tt = ttm_place_kunit_init(test, mem_type, 0);
	place_mock = ttm_place_kunit_init(test, mem_type_evict, 0);

	placement_tt = ttm_placement_kunit_init(test, place_tt, 1);
	placement_mock = ttm_placement_kunit_init(test, place_mock, 1);

	bo_tt = kunit_kzalloc(test, sizeof(*bo_tt), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_tt);

	bo_mock = kunit_kzalloc(test, sizeof(*bo_mock), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, bo_mock);

	drm_gem_private_object_init(priv->drm, &bo_tt->base, MANAGER_SIZE);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_tt, bo_type, placement_tt,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	dma_resv_unlock(bo_tt->base.resv);

	drm_gem_private_object_init(priv->drm, &bo_mock->base, MANAGER_SIZE);
	err = ttm_bo_init_reserved(priv->ttm_dev, bo_mock, bo_type, placement_mock,
				   PAGE_SIZE, &ctx_init, NULL, NULL,
				   &dummy_ttm_bo_destroy);
	KUNIT_EXPECT_EQ(test, err, 0);
	dma_resv_unlock(bo_mock->base.resv);

	bo_val = ttm_bo_kunit_init(test, test->priv, BO_SIZE, NULL);
	bo_val->type = bo_type;

	ttm_bo_reserve(bo_val, false, false, NULL);
	err = ttm_bo_validate(bo_val, placement_tt, &ctx_val);
	ttm_bo_unreserve(bo_val);

	KUNIT_EXPECT_EQ(test, err, 0);

	ttm_mock_manager_fini(priv->ttm_dev, mem_type);
	ttm_mock_manager_fini(priv->ttm_dev, mem_type_evict);

	ttm_bo_put(bo_val);
	ttm_bo_put(bo_tt);
	ttm_bo_put(bo_mock);
}

static struct kunit_case ttm_bo_validate_test_cases[] = {
	KUNIT_CASE_PARAM(ttm_bo_init_reserved_sys_man, ttm_bo_types_gen_params),
	KUNIT_CASE_PARAM(ttm_bo_init_reserved_mock_man, ttm_bo_types_gen_params),
	KUNIT_CASE(ttm_bo_init_reserved_resv),
	KUNIT_CASE_PARAM(ttm_bo_validate_basic, ttm_bo_types_gen_params),
	KUNIT_CASE(ttm_bo_validate_invalid_placement),
	KUNIT_CASE_PARAM(ttm_bo_validate_same_placement,
			 ttm_bo_validate_mem_gen_params),
	KUNIT_CASE(ttm_bo_validate_failed_alloc),
	KUNIT_CASE(ttm_bo_validate_pinned),
	KUNIT_CASE(ttm_bo_validate_busy_placement),
	KUNIT_CASE_PARAM(ttm_bo_validate_multihop, ttm_bo_types_gen_params),
	KUNIT_CASE_PARAM(ttm_bo_validate_no_placement_signaled,
			 ttm_bo_no_placement_gen_params),
	KUNIT_CASE_PARAM(ttm_bo_validate_no_placement_not_signaled,
			 ttm_bo_types_gen_params),
	KUNIT_CASE(ttm_bo_validate_move_fence_signaled),
	KUNIT_CASE_PARAM(ttm_bo_validate_move_fence_not_signaled,
			 ttm_bo_validate_wait_gen_params),
	KUNIT_CASE(ttm_bo_validate_swapout),
	KUNIT_CASE(ttm_bo_validate_happy_evict),
	KUNIT_CASE(ttm_bo_validate_all_pinned_evict),
	KUNIT_CASE(ttm_bo_validate_allowed_only_evict),
	KUNIT_CASE(ttm_bo_validate_deleted_evict),
	KUNIT_CASE(ttm_bo_validate_busy_domain_evict),
	KUNIT_CASE(ttm_bo_validate_evict_gutting),
	KUNIT_CASE(ttm_bo_validate_recrusive_evict),
	{}
};

static struct kunit_suite ttm_bo_validate_test_suite = {
	.name = "ttm_bo_validate",
	.init = ttm_test_devices_all_init,
	.exit = ttm_test_devices_fini,
	.test_cases = ttm_bo_validate_test_cases,
};

kunit_test_suites(&ttm_bo_validate_test_suite);

MODULE_DESCRIPTION("KUnit tests for ttm_bo APIs");
MODULE_LICENSE("GPL and additional rights");
