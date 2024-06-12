// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>

#include "ttm_kunit_helpers.h"
#include "ttm_mock_manager.h"

#define BO_SIZE		SZ_4K
#define MANAGER_SIZE	SZ_1M

struct ttm_bo_validate_test_case {
	const char *description;
	enum ttm_bo_type bo_type;
	u32 mem_type;
	bool with_ttm;
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
	{}
};

static struct kunit_suite ttm_bo_validate_test_suite = {
	.name = "ttm_bo_validate",
	.init = ttm_test_devices_all_init,
	.exit = ttm_test_devices_fini,
	.test_cases = ttm_bo_validate_test_cases,
};

kunit_test_suites(&ttm_bo_validate_test_suite);

MODULE_LICENSE("GPL and additional rights");
