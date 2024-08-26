// SPDX-License-Identifier: GPL-2.0 AND MIT
/*
 * Copyright © 2023 Intel Corporation
 */
#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>

#include "ttm_mock_manager.h"

static inline struct ttm_mock_manager *
to_mock_mgr(struct ttm_resource_manager *man)
{
	return container_of(man, struct ttm_mock_manager, man);
}

static inline struct ttm_mock_resource *
to_mock_mgr_resource(struct ttm_resource *res)
{
	return container_of(res, struct ttm_mock_resource, base);
}

static int ttm_mock_manager_alloc(struct ttm_resource_manager *man,
				  struct ttm_buffer_object *bo,
				  const struct ttm_place *place,
				  struct ttm_resource **res)
{
	struct ttm_mock_manager *manager = to_mock_mgr(man);
	struct ttm_mock_resource *mock_res;
	struct drm_buddy *mm = &manager->mm;
	u64 lpfn, fpfn, alloc_size;
	int err;

	mock_res = kzalloc(sizeof(*mock_res), GFP_KERNEL);

	if (!mock_res)
		return -ENOMEM;

	fpfn = 0;
	lpfn = man->size;

	ttm_resource_init(bo, place, &mock_res->base);
	INIT_LIST_HEAD(&mock_res->blocks);

	if (place->flags & TTM_PL_FLAG_TOPDOWN)
		mock_res->flags |= DRM_BUDDY_TOPDOWN_ALLOCATION;

	if (place->flags & TTM_PL_FLAG_CONTIGUOUS)
		mock_res->flags |= DRM_BUDDY_CONTIGUOUS_ALLOCATION;

	alloc_size = (uint64_t)mock_res->base.size;
	mutex_lock(&manager->lock);
	err = drm_buddy_alloc_blocks(mm, fpfn, lpfn, alloc_size,
				     manager->default_page_size,
				     &mock_res->blocks,
				     mock_res->flags);

	if (err)
		goto error_free_blocks;
	mutex_unlock(&manager->lock);

	*res = &mock_res->base;
	return 0;

error_free_blocks:
	drm_buddy_free_list(mm, &mock_res->blocks, 0);
	ttm_resource_fini(man, &mock_res->base);
	mutex_unlock(&manager->lock);

	return err;
}

static void ttm_mock_manager_free(struct ttm_resource_manager *man,
				  struct ttm_resource *res)
{
	struct ttm_mock_manager *manager = to_mock_mgr(man);
	struct ttm_mock_resource *mock_res = to_mock_mgr_resource(res);
	struct drm_buddy *mm = &manager->mm;

	mutex_lock(&manager->lock);
	drm_buddy_free_list(mm, &mock_res->blocks, 0);
	mutex_unlock(&manager->lock);

	ttm_resource_fini(man, res);
	kfree(mock_res);
}

static const struct ttm_resource_manager_func ttm_mock_manager_funcs = {
	.alloc = ttm_mock_manager_alloc,
	.free = ttm_mock_manager_free,
};

int ttm_mock_manager_init(struct ttm_device *bdev, u32 mem_type, u32 size)
{
	struct ttm_mock_manager *manager;
	struct ttm_resource_manager *base;
	int err;

	manager = kzalloc(sizeof(*manager), GFP_KERNEL);
	if (!manager)
		return -ENOMEM;

	mutex_init(&manager->lock);

	err = drm_buddy_init(&manager->mm, size, PAGE_SIZE);

	if (err) {
		kfree(manager);
		return err;
	}

	manager->default_page_size = PAGE_SIZE;
	base = &manager->man;
	base->func = &ttm_mock_manager_funcs;
	base->use_tt = true;

	ttm_resource_manager_init(base, bdev, size);
	ttm_set_driver_manager(bdev, mem_type, base);
	ttm_resource_manager_set_used(base, true);

	return 0;
}
EXPORT_SYMBOL_GPL(ttm_mock_manager_init);

void ttm_mock_manager_fini(struct ttm_device *bdev, u32 mem_type)
{
	struct ttm_resource_manager *man;
	struct ttm_mock_manager *mock_man;
	int err;

	man = ttm_manager_type(bdev, mem_type);
	mock_man = to_mock_mgr(man);

	err = ttm_resource_manager_evict_all(bdev, man);
	if (err)
		return;

	ttm_resource_manager_set_used(man, false);

	mutex_lock(&mock_man->lock);
	drm_buddy_fini(&mock_man->mm);
	mutex_unlock(&mock_man->lock);

	ttm_set_driver_manager(bdev, mem_type, NULL);
}
EXPORT_SYMBOL_GPL(ttm_mock_manager_fini);

static int ttm_bad_manager_alloc(struct ttm_resource_manager *man,
				 struct ttm_buffer_object *bo,
				 const struct ttm_place *place,
				 struct ttm_resource **res)
{
	return -ENOSPC;
}

static int ttm_busy_manager_alloc(struct ttm_resource_manager *man,
				  struct ttm_buffer_object *bo,
				  const struct ttm_place *place,
				  struct ttm_resource **res)
{
	return -EBUSY;
}

static void ttm_bad_manager_free(struct ttm_resource_manager *man,
				 struct ttm_resource *res)
{
}

static bool ttm_bad_manager_compatible(struct ttm_resource_manager *man,
				       struct ttm_resource *res,
				       const struct ttm_place *place,
				       size_t size)
{
	return true;
}

static const struct ttm_resource_manager_func ttm_bad_manager_funcs = {
	.alloc = ttm_bad_manager_alloc,
	.free = ttm_bad_manager_free,
	.compatible = ttm_bad_manager_compatible
};

static const struct ttm_resource_manager_func ttm_bad_busy_manager_funcs = {
	.alloc = ttm_busy_manager_alloc,
	.free = ttm_bad_manager_free,
	.compatible = ttm_bad_manager_compatible
};

int ttm_bad_manager_init(struct ttm_device *bdev, u32 mem_type, u32 size)
{
	struct ttm_resource_manager *man;

	man = kzalloc(sizeof(*man), GFP_KERNEL);
	if (!man)
		return -ENOMEM;

	man->func = &ttm_bad_manager_funcs;

	ttm_resource_manager_init(man, bdev, size);
	ttm_set_driver_manager(bdev, mem_type, man);
	ttm_resource_manager_set_used(man, true);

	return 0;
}
EXPORT_SYMBOL_GPL(ttm_bad_manager_init);

int ttm_busy_manager_init(struct ttm_device *bdev, u32 mem_type, u32 size)
{
	struct ttm_resource_manager *man;

	ttm_bad_manager_init(bdev, mem_type, size);
	man = ttm_manager_type(bdev, mem_type);

	man->func = &ttm_bad_busy_manager_funcs;

	return 0;
}
EXPORT_SYMBOL_GPL(ttm_busy_manager_init);

void ttm_bad_manager_fini(struct ttm_device *bdev, uint32_t mem_type)
{
	struct ttm_resource_manager *man;

	man = ttm_manager_type(bdev, mem_type);

	ttm_resource_manager_set_used(man, false);
	ttm_set_driver_manager(bdev, mem_type, NULL);

	kfree(man);
}
EXPORT_SYMBOL_GPL(ttm_bad_manager_fini);

MODULE_DESCRIPTION("KUnit tests for ttm with mock resource managers");
MODULE_LICENSE("GPL and additional rights");
