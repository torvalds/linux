/* SPDX-License-Identifier: GPL-2.0 OR MIT */

/*
 * Copyright (c) 2006-2009 VMware, Inc., Palo Alto, CA., USA
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

#define pr_fmt(fmt) "[TTM DEVICE] " fmt

#include <linux/mm.h>

#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_bo_api.h>

#include "ttm_module.h"

/*
 * ttm_global_mutex - protecting the global state
 */
static DEFINE_MUTEX(ttm_global_mutex);
static unsigned ttm_glob_use_count;
struct ttm_global ttm_glob;
EXPORT_SYMBOL(ttm_glob);

static void ttm_global_release(void)
{
	struct ttm_global *glob = &ttm_glob;

	mutex_lock(&ttm_global_mutex);
	if (--ttm_glob_use_count > 0)
		goto out;

	ttm_pool_mgr_fini();

	__free_page(glob->dummy_read_page);
	memset(glob, 0, sizeof(*glob));
out:
	mutex_unlock(&ttm_global_mutex);
}

static int ttm_global_init(void)
{
	struct ttm_global *glob = &ttm_glob;
	unsigned long num_pages, num_dma32;
	struct sysinfo si;
	int ret = 0;

	mutex_lock(&ttm_global_mutex);
	if (++ttm_glob_use_count > 1)
		goto out;

	si_meminfo(&si);

	/* Limit the number of pages in the pool to about 50% of the total
	 * system memory.
	 */
	num_pages = ((u64)si.totalram * si.mem_unit) >> PAGE_SHIFT;
	num_pages /= 2;

	/* But for DMA32 we limit ourself to only use 2GiB maximum. */
	num_dma32 = (u64)(si.totalram - si.totalhigh) * si.mem_unit
		>> PAGE_SHIFT;
	num_dma32 = min(num_dma32, 2UL << (30 - PAGE_SHIFT));

	ttm_pool_mgr_init(num_pages);
	ttm_tt_mgr_init(num_pages, num_dma32);

	glob->dummy_read_page = alloc_page(__GFP_ZERO | GFP_DMA32);

	if (unlikely(glob->dummy_read_page == NULL)) {
		ret = -ENOMEM;
		goto out;
	}

	INIT_LIST_HEAD(&glob->device_list);
	atomic_set(&glob->bo_count, 0);

	debugfs_create_atomic_t("buffer_objects", 0444, ttm_debugfs_root,
				&glob->bo_count);
out:
	if (ret)
		--ttm_glob_use_count;
	mutex_unlock(&ttm_global_mutex);
	return ret;
}

/*
 * A buffer object shrink method that tries to swap out the first
 * buffer object on the global::swap_lru list.
 */
int ttm_global_swapout(struct ttm_operation_ctx *ctx, gfp_t gfp_flags)
{
	struct ttm_global *glob = &ttm_glob;
	struct ttm_device *bdev;
	int ret = 0;

	mutex_lock(&ttm_global_mutex);
	list_for_each_entry(bdev, &glob->device_list, device_list) {
		ret = ttm_device_swapout(bdev, ctx, gfp_flags);
		if (ret > 0) {
			list_move_tail(&bdev->device_list, &glob->device_list);
			break;
		}
	}
	mutex_unlock(&ttm_global_mutex);
	return ret;
}
EXPORT_SYMBOL(ttm_global_swapout);

int ttm_device_swapout(struct ttm_device *bdev, struct ttm_operation_ctx *ctx,
		       gfp_t gfp_flags)
{
	struct ttm_resource_manager *man;
	struct ttm_buffer_object *bo;
	unsigned i, j;
	int ret;

	spin_lock(&bdev->lru_lock);
	for (i = TTM_PL_SYSTEM; i < TTM_NUM_MEM_TYPES; ++i) {
		man = ttm_manager_type(bdev, i);
		if (!man || !man->use_tt)
			continue;

		for (j = 0; j < TTM_MAX_BO_PRIORITY; ++j) {
			list_for_each_entry(bo, &man->lru[j], lru) {
				uint32_t num_pages = PFN_UP(bo->base.size);

				ret = ttm_bo_swapout(bo, ctx, gfp_flags);
				/* ttm_bo_swapout has dropped the lru_lock */
				if (!ret)
					return num_pages;
				if (ret != -EBUSY)
					return ret;
			}
		}
	}
	spin_unlock(&bdev->lru_lock);
	return 0;
}
EXPORT_SYMBOL(ttm_device_swapout);

static void ttm_device_delayed_workqueue(struct work_struct *work)
{
	struct ttm_device *bdev =
		container_of(work, struct ttm_device, wq.work);

	if (!ttm_bo_delayed_delete(bdev, false))
		schedule_delayed_work(&bdev->wq,
				      ((HZ / 100) < 1) ? 1 : HZ / 100);
}

/**
 * ttm_device_init
 *
 * @bdev: A pointer to a struct ttm_device to initialize.
 * @funcs: Function table for the device.
 * @dev: The core kernel device pointer for DMA mappings and allocations.
 * @mapping: The address space to use for this bo.
 * @vma_manager: A pointer to a vma manager.
 * @use_dma_alloc: If coherent DMA allocation API should be used.
 * @use_dma32: If we should use GFP_DMA32 for device memory allocations.
 *
 * Initializes a struct ttm_device:
 * Returns:
 * !0: Failure.
 */
int ttm_device_init(struct ttm_device *bdev, struct ttm_device_funcs *funcs,
		    struct device *dev, struct address_space *mapping,
		    struct drm_vma_offset_manager *vma_manager,
		    bool use_dma_alloc, bool use_dma32)
{
	struct ttm_global *glob = &ttm_glob;
	int ret;

	if (WARN_ON(vma_manager == NULL))
		return -EINVAL;

	ret = ttm_global_init();
	if (ret)
		return ret;

	bdev->funcs = funcs;

	ttm_sys_man_init(bdev);
	ttm_pool_init(&bdev->pool, dev, use_dma_alloc, use_dma32);

	bdev->vma_manager = vma_manager;
	INIT_DELAYED_WORK(&bdev->wq, ttm_device_delayed_workqueue);
	spin_lock_init(&bdev->lru_lock);
	INIT_LIST_HEAD(&bdev->ddestroy);
	bdev->dev_mapping = mapping;
	mutex_lock(&ttm_global_mutex);
	list_add_tail(&bdev->device_list, &glob->device_list);
	mutex_unlock(&ttm_global_mutex);

	return 0;
}
EXPORT_SYMBOL(ttm_device_init);

void ttm_device_fini(struct ttm_device *bdev)
{
	struct ttm_resource_manager *man;
	unsigned i;

	man = ttm_manager_type(bdev, TTM_PL_SYSTEM);
	ttm_resource_manager_set_used(man, false);
	ttm_set_driver_manager(bdev, TTM_PL_SYSTEM, NULL);

	mutex_lock(&ttm_global_mutex);
	list_del(&bdev->device_list);
	mutex_unlock(&ttm_global_mutex);

	cancel_delayed_work_sync(&bdev->wq);

	if (ttm_bo_delayed_delete(bdev, true))
		pr_debug("Delayed destroy list was clean\n");

	spin_lock(&bdev->lru_lock);
	for (i = 0; i < TTM_MAX_BO_PRIORITY; ++i)
		if (list_empty(&man->lru[0]))
			pr_debug("Swap list %d was clean\n", i);
	spin_unlock(&bdev->lru_lock);

	ttm_pool_fini(&bdev->pool);
	ttm_global_release();
}
EXPORT_SYMBOL(ttm_device_fini);
