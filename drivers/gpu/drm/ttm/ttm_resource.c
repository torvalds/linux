/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Christian König
 */

#include <drm/ttm/ttm_resource.h>
#include <drm/ttm/ttm_bo_driver.h>

int ttm_resource_alloc(struct ttm_buffer_object *bo,
		       const struct ttm_place *place,
		       struct ttm_resource *res)
{
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, res->mem_type);

	res->mm_node = NULL;
	if (!man->func || !man->func->alloc)
		return 0;

	return man->func->alloc(man, bo, place, res);
}

void ttm_resource_free(struct ttm_buffer_object *bo, struct ttm_resource *res)
{
	struct ttm_resource_manager *man =
		ttm_manager_type(bo->bdev, res->mem_type);

	if (man->func && man->func->free)
		man->func->free(man, res);

	res->mm_node = NULL;
	res->mem_type = TTM_PL_SYSTEM;
}
EXPORT_SYMBOL(ttm_resource_free);

/**
 * ttm_resource_manager_init
 *
 * @man: memory manager object to init
 * @p_size: size managed area in pages.
 *
 * Initialise core parts of a manager object.
 */
void ttm_resource_manager_init(struct ttm_resource_manager *man,
			       unsigned long p_size)
{
	unsigned i;

	spin_lock_init(&man->move_lock);
	man->size = p_size;

	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		INIT_LIST_HEAD(&man->lru[i]);
	man->move = NULL;
}
EXPORT_SYMBOL(ttm_resource_manager_init);

/*
 * ttm_resource_manager_force_list_clean
 *
 * @bdev - device to use
 * @man - manager to use
 *
 * Force all the objects out of a memory manager until clean.
 * Part of memory manager cleanup sequence.
 */
int ttm_resource_manager_force_list_clean(struct ttm_bo_device *bdev,
					  struct ttm_resource_manager *man)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false,
		.flags = TTM_OPT_FLAG_FORCE_ALLOC
	};
	struct ttm_bo_global *glob = &ttm_bo_glob;
	struct dma_fence *fence;
	int ret;
	unsigned i;

	/*
	 * Can't use standard list traversal since we're unlocking.
	 */

	spin_lock(&glob->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i) {
		while (!list_empty(&man->lru[i])) {
			spin_unlock(&glob->lru_lock);
			ret = ttm_mem_evict_first(bdev, man, NULL, &ctx,
						  NULL);
			if (ret)
				return ret;
			spin_lock(&glob->lru_lock);
		}
	}
	spin_unlock(&glob->lru_lock);

	spin_lock(&man->move_lock);
	fence = dma_fence_get(man->move);
	spin_unlock(&man->move_lock);

	if (fence) {
		ret = dma_fence_wait(fence, false);
		dma_fence_put(fence);
		if (ret)
			return ret;
	}

	return 0;
}
EXPORT_SYMBOL(ttm_resource_manager_force_list_clean);

/**
 * ttm_resource_manager_debug
 *
 * @man: manager type to dump.
 * @p: printer to use for debug.
 */
void ttm_resource_manager_debug(struct ttm_resource_manager *man,
				struct drm_printer *p)
{
	drm_printf(p, "  use_type: %d\n", man->use_type);
	drm_printf(p, "  use_tt: %d\n", man->use_tt);
	drm_printf(p, "  size: %llu\n", man->size);
	if (man->func && man->func->debug)
		(*man->func->debug)(man, p);
}
EXPORT_SYMBOL(ttm_resource_manager_debug);
