// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_bo.h"

#include <linux/dma-buf.h>

#include <drm/drm_drv.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>
#include <drm/xe_drm.h>

#include "xe_device.h"
#include "xe_dma_buf.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_map.h"
#include "xe_migrate.h"
#include "xe_preempt_fence.h"
#include "xe_res_cursor.h"
#include "xe_trace.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_vm.h"

static const struct ttm_place sys_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = XE_PL_SYSTEM,
	.flags = 0,
};

static struct ttm_placement sys_placement = {
	.num_placement = 1,
	.placement = &sys_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags,
};

bool mem_type_is_vram(u32 mem_type)
{
	return mem_type >= XE_PL_VRAM0 && mem_type != XE_PL_STOLEN;
}

static bool resource_is_stolen_vram(struct xe_device *xe, struct ttm_resource *res)
{
	return res->mem_type == XE_PL_STOLEN && IS_DGFX(xe);
}

static bool resource_is_vram(struct ttm_resource *res)
{
	return mem_type_is_vram(res->mem_type);
}

bool xe_bo_is_vram(struct xe_bo *bo)
{
	return resource_is_vram(bo->ttm.resource) ||
		resource_is_stolen_vram(xe_bo_device(bo), bo->ttm.resource);
}

bool xe_bo_is_stolen(struct xe_bo *bo)
{
	return bo->ttm.resource->mem_type == XE_PL_STOLEN;
}

static bool xe_bo_is_user(struct xe_bo *bo)
{
	return bo->flags & XE_BO_CREATE_USER_BIT;
}

static struct xe_gt *
mem_type_to_gt(struct xe_device *xe, u32 mem_type)
{
	XE_BUG_ON(mem_type != XE_PL_STOLEN && !mem_type_is_vram(mem_type));

	return xe_device_get_gt(xe, mem_type == XE_PL_STOLEN ? 0 : (mem_type - XE_PL_VRAM0));
}

static void try_add_system(struct xe_bo *bo, struct ttm_place *places,
			   u32 bo_flags, u32 *c)
{
	if (bo_flags & XE_BO_CREATE_SYSTEM_BIT) {
		places[*c] = (struct ttm_place) {
			.mem_type = XE_PL_TT,
		};
		*c += 1;

		if (bo->props.preferred_mem_type == XE_BO_PROPS_INVALID)
			bo->props.preferred_mem_type = XE_PL_TT;
	}
}

static void try_add_vram0(struct xe_device *xe, struct xe_bo *bo,
			  struct ttm_place *places, u32 bo_flags, u32 *c)
{
	struct xe_gt *gt;

	if (bo_flags & XE_BO_CREATE_VRAM0_BIT) {
		gt = mem_type_to_gt(xe, XE_PL_VRAM0);
		XE_BUG_ON(!gt->mem.vram.size);

		places[*c] = (struct ttm_place) {
			.mem_type = XE_PL_VRAM0,
			/*
			 * For eviction / restore on suspend / resume objects
			 * pinned in VRAM must be contiguous
			 */
			.flags = bo_flags & (XE_BO_CREATE_PINNED_BIT |
					     XE_BO_CREATE_GGTT_BIT) ?
				TTM_PL_FLAG_CONTIGUOUS : 0,
		};
		*c += 1;

		if (bo->props.preferred_mem_type == XE_BO_PROPS_INVALID)
			bo->props.preferred_mem_type = XE_PL_VRAM0;
	}
}

static void try_add_vram1(struct xe_device *xe, struct xe_bo *bo,
			  struct ttm_place *places, u32 bo_flags, u32 *c)
{
	struct xe_gt *gt;

	if (bo_flags & XE_BO_CREATE_VRAM1_BIT) {
		gt = mem_type_to_gt(xe, XE_PL_VRAM1);
		XE_BUG_ON(!gt->mem.vram.size);

		places[*c] = (struct ttm_place) {
			.mem_type = XE_PL_VRAM1,
			/*
			 * For eviction / restore on suspend / resume objects
			 * pinned in VRAM must be contiguous
			 */
			.flags = bo_flags & (XE_BO_CREATE_PINNED_BIT |
					     XE_BO_CREATE_GGTT_BIT) ?
				TTM_PL_FLAG_CONTIGUOUS : 0,
		};
		*c += 1;

		if (bo->props.preferred_mem_type == XE_BO_PROPS_INVALID)
			bo->props.preferred_mem_type = XE_PL_VRAM1;
	}
}

static void try_add_stolen(struct xe_device *xe, struct xe_bo *bo,
			   struct ttm_place *places, u32 bo_flags, u32 *c)
{
	if (bo_flags & XE_BO_CREATE_STOLEN_BIT) {
		places[*c] = (struct ttm_place) {
			.mem_type = XE_PL_STOLEN,
			.flags = bo_flags & (XE_BO_CREATE_PINNED_BIT |
					     XE_BO_CREATE_GGTT_BIT) ?
				TTM_PL_FLAG_CONTIGUOUS : 0,
		};
		*c += 1;
	}
}

static int __xe_bo_placement_for_flags(struct xe_device *xe, struct xe_bo *bo,
				       u32 bo_flags)
{
	struct ttm_place *places = bo->placements;
	u32 c = 0;

	bo->props.preferred_mem_type = XE_BO_PROPS_INVALID;

	/* The order of placements should indicate preferred location */

	if (bo->props.preferred_mem_class == XE_MEM_REGION_CLASS_SYSMEM) {
		try_add_system(bo, places, bo_flags, &c);
		if (bo->props.preferred_gt == XE_GT1) {
			try_add_vram1(xe, bo, places, bo_flags, &c);
			try_add_vram0(xe, bo, places, bo_flags, &c);
		} else {
			try_add_vram0(xe, bo, places, bo_flags, &c);
			try_add_vram1(xe, bo, places, bo_flags, &c);
		}
	} else if (bo->props.preferred_gt == XE_GT1) {
		try_add_vram1(xe, bo, places, bo_flags, &c);
		try_add_vram0(xe, bo, places, bo_flags, &c);
		try_add_system(bo, places, bo_flags, &c);
	} else {
		try_add_vram0(xe, bo, places, bo_flags, &c);
		try_add_vram1(xe, bo, places, bo_flags, &c);
		try_add_system(bo, places, bo_flags, &c);
	}
	try_add_stolen(xe, bo, places, bo_flags, &c);

	if (!c)
		return -EINVAL;

	bo->placement = (struct ttm_placement) {
		.num_placement = c,
		.placement = places,
		.num_busy_placement = c,
		.busy_placement = places,
	};

	return 0;
}

int xe_bo_placement_for_flags(struct xe_device *xe, struct xe_bo *bo,
			      u32 bo_flags)
{
	xe_bo_assert_held(bo);
	return __xe_bo_placement_for_flags(xe, bo, bo_flags);
}

static void xe_evict_flags(struct ttm_buffer_object *tbo,
			   struct ttm_placement *placement)
{
	struct xe_bo *bo;

	if (!xe_bo_is_xe_bo(tbo)) {
		/* Don't handle scatter gather BOs */
		if (tbo->type == ttm_bo_type_sg) {
			placement->num_placement = 0;
			placement->num_busy_placement = 0;
			return;
		}

		*placement = sys_placement;
		return;
	}

	/*
	 * For xe, sg bos that are evicted to system just triggers a
	 * rebind of the sg list upon subsequent validation to XE_PL_TT.
	 */

	bo = ttm_to_xe_bo(tbo);
	switch (tbo->resource->mem_type) {
	case XE_PL_VRAM0:
	case XE_PL_VRAM1:
	case XE_PL_STOLEN:
	case XE_PL_TT:
	default:
		/* for now kick out to system */
		*placement = sys_placement;
		break;
	}
}

struct xe_ttm_tt {
	struct ttm_tt ttm;
	struct device *dev;
	struct sg_table sgt;
	struct sg_table *sg;
};

static int xe_tt_map_sg(struct ttm_tt *tt)
{
	struct xe_ttm_tt *xe_tt = container_of(tt, struct xe_ttm_tt, ttm);
	unsigned long num_pages = tt->num_pages;
	int ret;

	XE_BUG_ON(tt->page_flags & TTM_TT_FLAG_EXTERNAL);

	if (xe_tt->sg)
		return 0;

	ret = sg_alloc_table_from_pages(&xe_tt->sgt, tt->pages, num_pages,
					0, (u64)num_pages << PAGE_SHIFT,
					GFP_KERNEL);
	if (ret)
		return ret;

	xe_tt->sg = &xe_tt->sgt;
	ret = dma_map_sgtable(xe_tt->dev, xe_tt->sg, DMA_BIDIRECTIONAL,
			      DMA_ATTR_SKIP_CPU_SYNC);
	if (ret) {
		sg_free_table(xe_tt->sg);
		xe_tt->sg = NULL;
		return ret;
	}

	return 0;
}

struct sg_table *xe_bo_get_sg(struct xe_bo *bo)
{
	struct ttm_tt *tt = bo->ttm.ttm;
	struct xe_ttm_tt *xe_tt = container_of(tt, struct xe_ttm_tt, ttm);

	return xe_tt->sg;
}

static struct ttm_tt *xe_ttm_tt_create(struct ttm_buffer_object *ttm_bo,
				       u32 page_flags)
{
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_ttm_tt *tt;
	int err;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	tt->dev = xe->drm.dev;

	/* TODO: Select caching mode */
	err = ttm_tt_init(&tt->ttm, &bo->ttm, page_flags,
			  bo->flags & XE_BO_SCANOUT_BIT ? ttm_write_combined : ttm_cached,
			  DIV_ROUND_UP(xe_device_ccs_bytes(xe_bo_device(bo),
							   bo->ttm.base.size),
				       PAGE_SIZE));
	if (err) {
		kfree(tt);
		return NULL;
	}

	return &tt->ttm;
}

static int xe_ttm_tt_populate(struct ttm_device *ttm_dev, struct ttm_tt *tt,
			      struct ttm_operation_ctx *ctx)
{
	int err;

	/*
	 * dma-bufs are not populated with pages, and the dma-
	 * addresses are set up when moved to XE_PL_TT.
	 */
	if (tt->page_flags & TTM_TT_FLAG_EXTERNAL)
		return 0;

	err = ttm_pool_alloc(&ttm_dev->pool, tt, ctx);
	if (err)
		return err;

	/* A follow up may move this xe_bo_move when BO is moved to XE_PL_TT */
	err = xe_tt_map_sg(tt);
	if (err)
		ttm_pool_free(&ttm_dev->pool, tt);

	return err;
}

static void xe_ttm_tt_unpopulate(struct ttm_device *ttm_dev, struct ttm_tt *tt)
{
	struct xe_ttm_tt *xe_tt = container_of(tt, struct xe_ttm_tt, ttm);

	if (tt->page_flags & TTM_TT_FLAG_EXTERNAL)
		return;

	if (xe_tt->sg) {
		dma_unmap_sgtable(xe_tt->dev, xe_tt->sg,
				  DMA_BIDIRECTIONAL, 0);
		sg_free_table(xe_tt->sg);
		xe_tt->sg = NULL;
	}

	return ttm_pool_free(&ttm_dev->pool, tt);
}

static void xe_ttm_tt_destroy(struct ttm_device *ttm_dev, struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static int xe_ttm_io_mem_reserve(struct ttm_device *bdev,
				 struct ttm_resource *mem)
{
	struct xe_device *xe = ttm_to_xe_device(bdev);
	struct xe_gt *gt;

	switch (mem->mem_type) {
	case XE_PL_SYSTEM:
	case XE_PL_TT:
		return 0;
	case XE_PL_VRAM0:
	case XE_PL_VRAM1:
		gt = mem_type_to_gt(xe, mem->mem_type);
		mem->bus.offset = mem->start << PAGE_SHIFT;

		if (gt->mem.vram.mapping &&
		    mem->placement & TTM_PL_FLAG_CONTIGUOUS)
			mem->bus.addr = (u8 *)gt->mem.vram.mapping +
				mem->bus.offset;

		mem->bus.offset += gt->mem.vram.io_start;
		mem->bus.is_iomem = true;

#if  !defined(CONFIG_X86)
		mem->bus.caching = ttm_write_combined;
#endif
		return 0;
	case XE_PL_STOLEN:
		return xe_ttm_stolen_io_mem_reserve(xe, mem);
	default:
		return -EINVAL;
	}
}

static int xe_bo_trigger_rebind(struct xe_device *xe, struct xe_bo *bo,
				const struct ttm_operation_ctx *ctx)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	struct xe_vma *vma;
	int ret = 0;

	dma_resv_assert_held(bo->ttm.base.resv);

	if (!xe_device_in_fault_mode(xe) && !list_empty(&bo->vmas)) {
		dma_resv_iter_begin(&cursor, bo->ttm.base.resv,
				    DMA_RESV_USAGE_BOOKKEEP);
		dma_resv_for_each_fence_unlocked(&cursor, fence)
			dma_fence_enable_sw_signaling(fence);
		dma_resv_iter_end(&cursor);
	}

	list_for_each_entry(vma, &bo->vmas, bo_link) {
		struct xe_vm *vm = vma->vm;

		trace_xe_vma_evict(vma);

		if (xe_vm_in_fault_mode(vm)) {
			/* Wait for pending binds / unbinds. */
			long timeout;

			if (ctx->no_wait_gpu &&
			    !dma_resv_test_signaled(bo->ttm.base.resv,
						    DMA_RESV_USAGE_BOOKKEEP))
				return -EBUSY;

			timeout = dma_resv_wait_timeout(bo->ttm.base.resv,
							DMA_RESV_USAGE_BOOKKEEP,
							ctx->interruptible,
							MAX_SCHEDULE_TIMEOUT);
			if (timeout > 0) {
				ret = xe_vm_invalidate_vma(vma);
				XE_WARN_ON(ret);
			} else if (!timeout) {
				ret = -ETIME;
			} else {
				ret = timeout;
			}

		} else {
			bool vm_resv_locked = false;
			struct xe_vm *vm = vma->vm;

			/*
			 * We need to put the vma on the vm's rebind_list,
			 * but need the vm resv to do so. If we can't verify
			 * that we indeed have it locked, put the vma an the
			 * vm's notifier.rebind_list instead and scoop later.
			 */
			if (dma_resv_trylock(&vm->resv))
				vm_resv_locked = true;
			else if (ctx->resv != &vm->resv) {
				spin_lock(&vm->notifier.list_lock);
				list_move_tail(&vma->notifier.rebind_link,
					       &vm->notifier.rebind_list);
				spin_unlock(&vm->notifier.list_lock);
				continue;
			}

			xe_vm_assert_held(vm);
			if (list_empty(&vma->rebind_link) && vma->gt_present)
				list_add_tail(&vma->rebind_link, &vm->rebind_list);

			if (vm_resv_locked)
				dma_resv_unlock(&vm->resv);
		}
	}

	return ret;
}

/*
 * The dma-buf map_attachment() / unmap_attachment() is hooked up here.
 * Note that unmapping the attachment is deferred to the next
 * map_attachment time, or to bo destroy (after idling) whichever comes first.
 * This is to avoid syncing before unmap_attachment(), assuming that the
 * caller relies on idling the reservation object before moving the
 * backing store out. Should that assumption not hold, then we will be able
 * to unconditionally call unmap_attachment() when moving out to system.
 */
static int xe_bo_move_dmabuf(struct ttm_buffer_object *ttm_bo,
			     struct ttm_resource *old_res,
			     struct ttm_resource *new_res)
{
	struct dma_buf_attachment *attach = ttm_bo->base.import_attach;
	struct xe_ttm_tt *xe_tt = container_of(ttm_bo->ttm, struct xe_ttm_tt,
					       ttm);
	struct sg_table *sg;

	XE_BUG_ON(!attach);
	XE_BUG_ON(!ttm_bo->ttm);

	if (new_res->mem_type == XE_PL_SYSTEM)
		goto out;

	if (ttm_bo->sg) {
		dma_buf_unmap_attachment(attach, ttm_bo->sg, DMA_BIDIRECTIONAL);
		ttm_bo->sg = NULL;
	}

	sg = dma_buf_map_attachment(attach, DMA_BIDIRECTIONAL);
	if (IS_ERR(sg))
		return PTR_ERR(sg);

	ttm_bo->sg = sg;
	xe_tt->sg = sg;

out:
	ttm_bo_move_null(ttm_bo, new_res);

	return 0;
}

/**
 * xe_bo_move_notify - Notify subsystems of a pending move
 * @bo: The buffer object
 * @ctx: The struct ttm_operation_ctx controlling locking and waits.
 *
 * This function notifies subsystems of an upcoming buffer move.
 * Upon receiving such a notification, subsystems should schedule
 * halting access to the underlying pages and optionally add a fence
 * to the buffer object's dma_resv object, that signals when access is
 * stopped. The caller will wait on all dma_resv fences before
 * starting the move.
 *
 * A subsystem may commence access to the object after obtaining
 * bindings to the new backing memory under the object lock.
 *
 * Return: 0 on success, -EINTR or -ERESTARTSYS if interrupted in fault mode,
 * negative error code on error.
 */
static int xe_bo_move_notify(struct xe_bo *bo,
			     const struct ttm_operation_ctx *ctx)
{
	struct ttm_buffer_object *ttm_bo = &bo->ttm;
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	int ret;

	/*
	 * If this starts to call into many components, consider
	 * using a notification chain here.
	 */

	if (xe_bo_is_pinned(bo))
		return -EINVAL;

	xe_bo_vunmap(bo);
	ret = xe_bo_trigger_rebind(xe, bo, ctx);
	if (ret)
		return ret;

	/* Don't call move_notify() for imported dma-bufs. */
	if (ttm_bo->base.dma_buf && !ttm_bo->base.import_attach)
		dma_buf_move_notify(ttm_bo->base.dma_buf);

	return 0;
}

static int xe_bo_move(struct ttm_buffer_object *ttm_bo, bool evict,
		      struct ttm_operation_ctx *ctx,
		      struct ttm_resource *new_mem,
		      struct ttm_place *hop)
{
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct ttm_resource *old_mem = ttm_bo->resource;
	struct ttm_tt *ttm = ttm_bo->ttm;
	struct xe_gt *gt = NULL;
	struct dma_fence *fence;
	bool move_lacks_source;
	bool needs_clear;
	int ret = 0;

	if (!old_mem) {
		if (new_mem->mem_type != TTM_PL_SYSTEM) {
			hop->mem_type = TTM_PL_SYSTEM;
			hop->flags = TTM_PL_FLAG_TEMPORARY;
			ret = -EMULTIHOP;
			goto out;
		}

		ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	if (ttm_bo->type == ttm_bo_type_sg) {
		ret = xe_bo_move_notify(bo, ctx);
		if (!ret)
			ret = xe_bo_move_dmabuf(ttm_bo, old_mem, new_mem);
		goto out;
	}

	move_lacks_source = !resource_is_vram(old_mem) &&
		(!ttm || !ttm_tt_is_populated(ttm));

	needs_clear = (ttm && ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC) ||
		(!ttm && ttm_bo->type == ttm_bo_type_device);

	if ((move_lacks_source && !needs_clear) ||
	    (old_mem->mem_type == XE_PL_SYSTEM &&
	     new_mem->mem_type == XE_PL_TT)) {
		ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	if (!move_lacks_source && !xe_bo_is_pinned(bo)) {
		ret = xe_bo_move_notify(bo, ctx);
		if (ret)
			goto out;
	}

	if (old_mem->mem_type == XE_PL_TT &&
	    new_mem->mem_type == XE_PL_SYSTEM) {
		long timeout = dma_resv_wait_timeout(ttm_bo->base.resv,
						     DMA_RESV_USAGE_BOOKKEEP,
						     true,
						     MAX_SCHEDULE_TIMEOUT);
		if (timeout < 0) {
			ret = timeout;
			goto out;
		}
		ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	if (!move_lacks_source &&
	    ((old_mem->mem_type == XE_PL_SYSTEM && resource_is_vram(new_mem)) ||
	     (resource_is_vram(old_mem) &&
	      new_mem->mem_type == XE_PL_SYSTEM))) {
		hop->fpfn = 0;
		hop->lpfn = 0;
		hop->mem_type = XE_PL_TT;
		hop->flags = TTM_PL_FLAG_TEMPORARY;
		ret = -EMULTIHOP;
		goto out;
	}

	if (bo->gt)
		gt = bo->gt;
	else if (resource_is_vram(new_mem))
		gt = mem_type_to_gt(xe, new_mem->mem_type);
	else if (resource_is_vram(old_mem))
		gt = mem_type_to_gt(xe, old_mem->mem_type);

	XE_BUG_ON(!gt);
	XE_BUG_ON(!gt->migrate);

	trace_xe_bo_move(bo);
	xe_device_mem_access_get(xe);

	if (xe_bo_is_pinned(bo) && !xe_bo_is_user(bo)) {
		/*
		 * Kernel memory that is pinned should only be moved on suspend
		 * / resume, some of the pinned memory is required for the
		 * device to resume / use the GPU to move other evicted memory
		 * (user memory) around. This likely could be optimized a bit
		 * futher where we find the minimum set of pinned memory
		 * required for resume but for simplity doing a memcpy for all
		 * pinned memory.
		 */
		ret = xe_bo_vmap(bo);
		if (!ret) {
			ret = ttm_bo_move_memcpy(ttm_bo, ctx, new_mem);

			/* Create a new VMAP once kernel BO back in VRAM */
			if (!ret && resource_is_vram(new_mem)) {
				void *new_addr = gt->mem.vram.mapping +
					(new_mem->start << PAGE_SHIFT);

				XE_BUG_ON(new_mem->start !=
					  bo->placements->fpfn);

				iosys_map_set_vaddr_iomem(&bo->vmap, new_addr);
			}
		}
	} else {
		if (move_lacks_source)
			fence = xe_migrate_clear(gt->migrate, bo, new_mem, 0);
		else
			fence = xe_migrate_copy(gt->migrate, bo, old_mem, new_mem);
		if (IS_ERR(fence)) {
			ret = PTR_ERR(fence);
			xe_device_mem_access_put(xe);
			goto out;
		}
		ret = ttm_bo_move_accel_cleanup(ttm_bo, fence, evict, true,
						new_mem);
		dma_fence_put(fence);
	}

	xe_device_mem_access_put(xe);
	trace_printk("new_mem->mem_type=%d\n", new_mem->mem_type);

out:
	return ret;

}

static unsigned long xe_ttm_io_mem_pfn(struct ttm_buffer_object *ttm_bo,
				       unsigned long page_offset)
{
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct xe_gt *gt = mem_type_to_gt(xe, ttm_bo->resource->mem_type);
	struct xe_res_cursor cursor;

	if (ttm_bo->resource->mem_type == XE_PL_STOLEN)
		return xe_ttm_stolen_io_offset(bo, page_offset << PAGE_SHIFT) >> PAGE_SHIFT;

	xe_res_first(ttm_bo->resource, (u64)page_offset << PAGE_SHIFT, 0, &cursor);
	return (gt->mem.vram.io_start + cursor.start) >> PAGE_SHIFT;
}

static void __xe_bo_vunmap(struct xe_bo *bo);

/*
 * TODO: Move this function to TTM so we don't rely on how TTM does its
 * locking, thereby abusing TTM internals.
 */
static bool xe_ttm_bo_lock_in_destructor(struct ttm_buffer_object *ttm_bo)
{
	bool locked;

	XE_WARN_ON(kref_read(&ttm_bo->kref));

	/*
	 * We can typically only race with TTM trylocking under the
	 * lru_lock, which will immediately be unlocked again since
	 * the ttm_bo refcount is zero at this point. So trylocking *should*
	 * always succeed here, as long as we hold the lru lock.
	 */
	spin_lock(&ttm_bo->bdev->lru_lock);
	locked = dma_resv_trylock(ttm_bo->base.resv);
	spin_unlock(&ttm_bo->bdev->lru_lock);
	XE_WARN_ON(!locked);

	return locked;
}

static void xe_ttm_bo_release_notify(struct ttm_buffer_object *ttm_bo)
{
	struct dma_resv_iter cursor;
	struct dma_fence *fence;
	struct dma_fence *replacement = NULL;
	struct xe_bo *bo;

	if (!xe_bo_is_xe_bo(ttm_bo))
		return;

	bo = ttm_to_xe_bo(ttm_bo);
	XE_WARN_ON(bo->created && kref_read(&ttm_bo->base.refcount));

	/*
	 * Corner case where TTM fails to allocate memory and this BOs resv
	 * still points the VMs resv
	 */
	if (ttm_bo->base.resv != &ttm_bo->base._resv)
		return;

	if (!xe_ttm_bo_lock_in_destructor(ttm_bo))
		return;

	/*
	 * Scrub the preempt fences if any. The unbind fence is already
	 * attached to the resv.
	 * TODO: Don't do this for external bos once we scrub them after
	 * unbind.
	 */
	dma_resv_for_each_fence(&cursor, ttm_bo->base.resv,
				DMA_RESV_USAGE_BOOKKEEP, fence) {
		if (xe_fence_is_xe_preempt(fence) &&
		    !dma_fence_is_signaled(fence)) {
			if (!replacement)
				replacement = dma_fence_get_stub();

			dma_resv_replace_fences(ttm_bo->base.resv,
						fence->context,
						replacement,
						DMA_RESV_USAGE_BOOKKEEP);
		}
	}
	dma_fence_put(replacement);

	dma_resv_unlock(ttm_bo->base.resv);
}

static void xe_ttm_bo_delete_mem_notify(struct ttm_buffer_object *ttm_bo)
{
	if (!xe_bo_is_xe_bo(ttm_bo))
		return;

	/*
	 * Object is idle and about to be destroyed. Release the
	 * dma-buf attachment.
	 */
	if (ttm_bo->type == ttm_bo_type_sg && ttm_bo->sg) {
		struct xe_ttm_tt *xe_tt = container_of(ttm_bo->ttm,
						       struct xe_ttm_tt, ttm);

		dma_buf_unmap_attachment(ttm_bo->base.import_attach, ttm_bo->sg,
					 DMA_BIDIRECTIONAL);
		ttm_bo->sg = NULL;
		xe_tt->sg = NULL;
	}
}

struct ttm_device_funcs xe_ttm_funcs = {
	.ttm_tt_create = xe_ttm_tt_create,
	.ttm_tt_populate = xe_ttm_tt_populate,
	.ttm_tt_unpopulate = xe_ttm_tt_unpopulate,
	.ttm_tt_destroy = xe_ttm_tt_destroy,
	.evict_flags = xe_evict_flags,
	.move = xe_bo_move,
	.io_mem_reserve = xe_ttm_io_mem_reserve,
	.io_mem_pfn = xe_ttm_io_mem_pfn,
	.release_notify = xe_ttm_bo_release_notify,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.delete_mem_notify = xe_ttm_bo_delete_mem_notify,
};

static void xe_ttm_bo_destroy(struct ttm_buffer_object *ttm_bo)
{
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);

	if (bo->ttm.base.import_attach)
		drm_prime_gem_destroy(&bo->ttm.base, NULL);
	drm_gem_object_release(&bo->ttm.base);

	WARN_ON(!list_empty(&bo->vmas));

	if (bo->ggtt_node.size)
		xe_ggtt_remove_bo(bo->gt->mem.ggtt, bo);

	if (bo->vm && xe_bo_is_user(bo))
		xe_vm_put(bo->vm);

	kfree(bo);
}

static void xe_gem_object_free(struct drm_gem_object *obj)
{
	/* Our BO reference counting scheme works as follows:
	 *
	 * The gem object kref is typically used throughout the driver,
	 * and the gem object holds a ttm_buffer_object refcount, so
	 * that when the last gem object reference is put, which is when
	 * we end up in this function, we put also that ttm_buffer_object
	 * refcount. Anything using gem interfaces is then no longer
	 * allowed to access the object in a way that requires a gem
	 * refcount, including locking the object.
	 *
	 * driver ttm callbacks is allowed to use the ttm_buffer_object
	 * refcount directly if needed.
	 */
	__xe_bo_vunmap(gem_to_xe_bo(obj));
	ttm_bo_put(container_of(obj, struct ttm_buffer_object, base));
}

static bool should_migrate_to_system(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);

	return xe_device_in_fault_mode(xe) && bo->props.cpu_atomic;
}

static vm_fault_t xe_gem_fault(struct vm_fault *vmf)
{
	struct ttm_buffer_object *tbo = vmf->vma->vm_private_data;
	struct drm_device *ddev = tbo->base.dev;
	vm_fault_t ret;
	int idx, r = 0;

	ret = ttm_bo_vm_reserve(tbo, vmf);
	if (ret)
		return ret;

	if (drm_dev_enter(ddev, &idx)) {
		struct xe_bo *bo = ttm_to_xe_bo(tbo);

		trace_xe_bo_cpu_fault(bo);

		if (should_migrate_to_system(bo)) {
			r = xe_bo_migrate(bo, XE_PL_TT);
			if (r == -EBUSY || r == -ERESTARTSYS || r == -EINTR)
				ret = VM_FAULT_NOPAGE;
			else if (r)
				ret = VM_FAULT_SIGBUS;
		}
		if (!ret)
			ret = ttm_bo_vm_fault_reserved(vmf,
						       vmf->vma->vm_page_prot,
						       TTM_BO_VM_NUM_PREFAULT);

		drm_dev_exit(idx);
	} else {
		ret = ttm_bo_vm_dummy_page(vmf, vmf->vma->vm_page_prot);
	}
	if (ret == VM_FAULT_RETRY && !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT))
		return ret;

	dma_resv_unlock(tbo->base.resv);
	return ret;
}

static const struct vm_operations_struct xe_gem_vm_ops = {
	.fault = xe_gem_fault,
	.open = ttm_bo_vm_open,
	.close = ttm_bo_vm_close,
	.access = ttm_bo_vm_access
};

static const struct drm_gem_object_funcs xe_gem_object_funcs = {
	.free = xe_gem_object_free,
	.mmap = drm_gem_ttm_mmap,
	.export = xe_gem_prime_export,
	.vm_ops = &xe_gem_vm_ops,
};

/**
 * xe_bo_alloc - Allocate storage for a struct xe_bo
 *
 * This funcition is intended to allocate storage to be used for input
 * to __xe_bo_create_locked(), in the case a pointer to the bo to be
 * created is needed before the call to __xe_bo_create_locked().
 * If __xe_bo_create_locked ends up never to be called, then the
 * storage allocated with this function needs to be freed using
 * xe_bo_free().
 *
 * Return: A pointer to an uninitialized struct xe_bo on success,
 * ERR_PTR(-ENOMEM) on error.
 */
struct xe_bo *xe_bo_alloc(void)
{
	struct xe_bo *bo = kzalloc(sizeof(*bo), GFP_KERNEL);

	if (!bo)
		return ERR_PTR(-ENOMEM);

	return bo;
}

/**
 * xe_bo_free - Free storage allocated using xe_bo_alloc()
 * @bo: The buffer object storage.
 *
 * Refer to xe_bo_alloc() documentation for valid use-cases.
 */
void xe_bo_free(struct xe_bo *bo)
{
	kfree(bo);
}

struct xe_bo *__xe_bo_create_locked(struct xe_device *xe, struct xe_bo *bo,
				    struct xe_gt *gt, struct dma_resv *resv,
				    size_t size, enum ttm_bo_type type,
				    u32 flags)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	struct ttm_placement *placement;
	uint32_t alignment;
	int err;

	/* Only kernel objects should set GT */
	XE_BUG_ON(gt && type != ttm_bo_type_kernel);

	if (XE_WARN_ON(!size))
		return ERR_PTR(-EINVAL);

	if (!bo) {
		bo = xe_bo_alloc();
		if (IS_ERR(bo))
			return bo;
	}

	if (flags & (XE_BO_CREATE_VRAM0_BIT | XE_BO_CREATE_VRAM1_BIT |
		     XE_BO_CREATE_STOLEN_BIT) &&
	    !(flags & XE_BO_CREATE_IGNORE_MIN_PAGE_SIZE_BIT) &&
	    xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K) {
		size = ALIGN(size, SZ_64K);
		flags |= XE_BO_INTERNAL_64K;
		alignment = SZ_64K >> PAGE_SHIFT;
	} else {
		alignment = SZ_4K >> PAGE_SHIFT;
	}

	bo->gt = gt;
	bo->size = size;
	bo->flags = flags;
	bo->ttm.base.funcs = &xe_gem_object_funcs;
	bo->props.preferred_mem_class = XE_BO_PROPS_INVALID;
	bo->props.preferred_gt = XE_BO_PROPS_INVALID;
	bo->props.preferred_mem_type = XE_BO_PROPS_INVALID;
	bo->ttm.priority = DRM_XE_VMA_PRIORITY_NORMAL;
	INIT_LIST_HEAD(&bo->vmas);
	INIT_LIST_HEAD(&bo->pinned_link);

	drm_gem_private_object_init(&xe->drm, &bo->ttm.base, size);

	if (resv) {
		ctx.allow_res_evict = true;
		ctx.resv = resv;
	}

	if (!(flags & XE_BO_FIXED_PLACEMENT_BIT)) {
		err = __xe_bo_placement_for_flags(xe, bo, bo->flags);
		if (WARN_ON(err))
			return ERR_PTR(err);
	}

	/* Defer populating type_sg bos */
	placement = (type == ttm_bo_type_sg ||
		     bo->flags & XE_BO_DEFER_BACKING) ? &sys_placement :
		&bo->placement;
	err = ttm_bo_init_reserved(&xe->ttm, &bo->ttm, type,
				   placement, alignment,
				   &ctx, NULL, resv, xe_ttm_bo_destroy);
	if (err)
		return ERR_PTR(err);

	bo->created = true;
	ttm_bo_move_to_lru_tail_unlocked(&bo->ttm);

	return bo;
}

static int __xe_bo_fixed_placement(struct xe_device *xe,
				   struct xe_bo *bo,
				   u32 flags,
				   u64 start, u64 end, u64 size)
{
	struct ttm_place *place = bo->placements;

	if (flags & (XE_BO_CREATE_USER_BIT|XE_BO_CREATE_SYSTEM_BIT))
		return -EINVAL;

	place->flags = TTM_PL_FLAG_CONTIGUOUS;
	place->fpfn = start >> PAGE_SHIFT;
	place->lpfn = end >> PAGE_SHIFT;

	switch (flags & (XE_BO_CREATE_STOLEN_BIT |
		XE_BO_CREATE_VRAM0_BIT |XE_BO_CREATE_VRAM1_BIT)) {
	case XE_BO_CREATE_VRAM0_BIT:
		place->mem_type = XE_PL_VRAM0;
		break;
	case XE_BO_CREATE_VRAM1_BIT:
		place->mem_type = XE_PL_VRAM1;
		break;
	case XE_BO_CREATE_STOLEN_BIT:
		place->mem_type = XE_PL_STOLEN;
		break;

	default:
		/* 0 or multiple of the above set */
		return -EINVAL;
	}

	bo->placement = (struct ttm_placement) {
		.num_placement = 1,
		.placement = place,
		.num_busy_placement = 1,
		.busy_placement = place,
	};

	return 0;
}

struct xe_bo *
xe_bo_create_locked_range(struct xe_device *xe,
			  struct xe_gt *gt, struct xe_vm *vm,
			  size_t size, u64 start, u64 end,
			  enum ttm_bo_type type, u32 flags)
{
	struct xe_bo *bo = NULL;
	int err;

	if (vm)
		xe_vm_assert_held(vm);

	if (start || end != ~0ULL) {
		bo = xe_bo_alloc();
		if (IS_ERR(bo))
			return bo;

		flags |= XE_BO_FIXED_PLACEMENT_BIT;
		err = __xe_bo_fixed_placement(xe, bo, flags, start, end, size);
		if (err) {
			xe_bo_free(bo);
			return ERR_PTR(err);
		}
	}

	bo = __xe_bo_create_locked(xe, bo, gt, vm ? &vm->resv : NULL, size,
				   type, flags);
	if (IS_ERR(bo))
		return bo;

	if (vm && xe_bo_is_user(bo))
		xe_vm_get(vm);
	bo->vm = vm;

	if (bo->flags & XE_BO_CREATE_GGTT_BIT) {
		if (!gt && flags & XE_BO_CREATE_STOLEN_BIT)
			gt = xe_device_get_gt(xe, 0);

		XE_BUG_ON(!gt);

		if (flags & XE_BO_CREATE_STOLEN_BIT &&
		    flags & XE_BO_FIXED_PLACEMENT_BIT) {
			err = xe_ggtt_insert_bo_at(gt->mem.ggtt, bo, start);
		} else {
			err = xe_ggtt_insert_bo(gt->mem.ggtt, bo);
		}
		if (err)
			goto err_unlock_put_bo;
	}

	return bo;

err_unlock_put_bo:
	xe_bo_unlock_vm_held(bo);
	xe_bo_put(bo);
	return ERR_PTR(err);
}

struct xe_bo *xe_bo_create_locked(struct xe_device *xe, struct xe_gt *gt,
				  struct xe_vm *vm, size_t size,
				  enum ttm_bo_type type, u32 flags)
{
	return xe_bo_create_locked_range(xe, gt, vm, size, 0, ~0ULL, type, flags);
}

struct xe_bo *xe_bo_create(struct xe_device *xe, struct xe_gt *gt,
			   struct xe_vm *vm, size_t size,
			   enum ttm_bo_type type, u32 flags)
{
	struct xe_bo *bo = xe_bo_create_locked(xe, gt, vm, size, type, flags);

	if (!IS_ERR(bo))
		xe_bo_unlock_vm_held(bo);

	return bo;
}

struct xe_bo *xe_bo_create_pin_map_at(struct xe_device *xe, struct xe_gt *gt,
				      struct xe_vm *vm,
				      size_t size, u64 offset,
				      enum ttm_bo_type type, u32 flags)
{
	struct xe_bo *bo;
	int err;
	u64 start = offset == ~0ull ? 0 : offset;
	u64 end = offset == ~0ull ? offset : start + size;

	if (flags & XE_BO_CREATE_STOLEN_BIT &&
	    xe_ttm_stolen_cpu_inaccessible(xe))
		flags |= XE_BO_CREATE_GGTT_BIT;

	bo = xe_bo_create_locked_range(xe, gt, vm, size, start, end, type, flags);
	if (IS_ERR(bo))
		return bo;

	err = xe_bo_pin(bo);
	if (err)
		goto err_put;

	err = xe_bo_vmap(bo);
	if (err)
		goto err_unpin;

	xe_bo_unlock_vm_held(bo);

	return bo;

err_unpin:
	xe_bo_unpin(bo);
err_put:
	xe_bo_unlock_vm_held(bo);
	xe_bo_put(bo);
	return ERR_PTR(err);
}

struct xe_bo *xe_bo_create_pin_map(struct xe_device *xe, struct xe_gt *gt,
				   struct xe_vm *vm, size_t size,
				   enum ttm_bo_type type, u32 flags)
{
	return xe_bo_create_pin_map_at(xe, gt, vm, size, ~0ull, type, flags);
}

struct xe_bo *xe_bo_create_from_data(struct xe_device *xe, struct xe_gt *gt,
				     const void *data, size_t size,
				     enum ttm_bo_type type, u32 flags)
{
	struct xe_bo *bo = xe_bo_create_pin_map(xe, gt, NULL,
						ALIGN(size, PAGE_SIZE),
						type, flags);
	if (IS_ERR(bo))
		return bo;

	xe_map_memcpy_to(xe, &bo->vmap, 0, data, size);

	return bo;
}

/*
 * XXX: This is in the VM bind data path, likely should calculate this once and
 * store, with a recalculation if the BO is moved.
 */
static uint64_t vram_region_io_offset(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_gt *gt = mem_type_to_gt(xe, bo->ttm.resource->mem_type);

	if (bo->ttm.resource->mem_type == XE_PL_STOLEN)
		return xe_ttm_stolen_gpu_offset(xe);

	return gt->mem.vram.io_start - xe->mem.vram.io_start;
}

/**
 * xe_bo_pin_external - pin an external BO
 * @bo: buffer object to be pinned
 *
 * Pin an external (not tied to a VM, can be exported via dma-buf / prime FD)
 * BO. Unique call compared to xe_bo_pin as this function has it own set of
 * asserts and code to ensure evict / restore on suspend / resume.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int xe_bo_pin_external(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);
	int err;

	XE_BUG_ON(bo->vm);
	XE_BUG_ON(!xe_bo_is_user(bo));

	if (!xe_bo_is_pinned(bo)) {
		err = xe_bo_validate(bo, NULL, false);
		if (err)
			return err;

		if (xe_bo_is_vram(bo)) {
			spin_lock(&xe->pinned.lock);
			list_add_tail(&bo->pinned_link,
				      &xe->pinned.external_vram);
			spin_unlock(&xe->pinned.lock);
		}
	}

	ttm_bo_pin(&bo->ttm);

	/*
	 * FIXME: If we always use the reserve / unreserve functions for locking
	 * we do not need this.
	 */
	ttm_bo_move_to_lru_tail_unlocked(&bo->ttm);

	return 0;
}

int xe_bo_pin(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);
	int err;

	/* We currently don't expect user BO to be pinned */
	XE_BUG_ON(xe_bo_is_user(bo));

	/* Pinned object must be in GGTT or have pinned flag */
	XE_BUG_ON(!(bo->flags & (XE_BO_CREATE_PINNED_BIT |
				 XE_BO_CREATE_GGTT_BIT)));

	/*
	 * No reason we can't support pinning imported dma-bufs we just don't
	 * expect to pin an imported dma-buf.
	 */
	XE_BUG_ON(bo->ttm.base.import_attach);

	/* We only expect at most 1 pin */
	XE_BUG_ON(xe_bo_is_pinned(bo));

	err = xe_bo_validate(bo, NULL, false);
	if (err)
		return err;

	/*
	 * For pinned objects in on DGFX, which are also in vram, we expect
	 * these to be in contiguous VRAM memory. Required eviction / restore
	 * during suspend / resume (force restore to same physical address).
	 */
	if (IS_DGFX(xe) && !(IS_ENABLED(CONFIG_DRM_XE_DEBUG) &&
	    bo->flags & XE_BO_INTERNAL_TEST)) {
		struct ttm_place *place = &(bo->placements[0]);
		bool lmem;

		if (mem_type_is_vram(place->mem_type)) {
			XE_BUG_ON(!(place->flags & TTM_PL_FLAG_CONTIGUOUS));

			place->fpfn = (xe_bo_addr(bo, 0, PAGE_SIZE, &lmem) -
				       vram_region_io_offset(bo)) >> PAGE_SHIFT;
			place->lpfn = place->fpfn + (bo->size >> PAGE_SHIFT);

			spin_lock(&xe->pinned.lock);
			list_add_tail(&bo->pinned_link, &xe->pinned.kernel_bo_present);
			spin_unlock(&xe->pinned.lock);
		}
	}

	ttm_bo_pin(&bo->ttm);

	/*
	 * FIXME: If we always use the reserve / unreserve functions for locking
	 * we do not need this.
	 */
	ttm_bo_move_to_lru_tail_unlocked(&bo->ttm);

	return 0;
}

/**
 * xe_bo_unpin_external - unpin an external BO
 * @bo: buffer object to be unpinned
 *
 * Unpin an external (not tied to a VM, can be exported via dma-buf / prime FD)
 * BO. Unique call compared to xe_bo_unpin as this function has it own set of
 * asserts and code to ensure evict / restore on suspend / resume.
 *
 * Returns 0 for success, negative error code otherwise.
 */
void xe_bo_unpin_external(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);

	XE_BUG_ON(bo->vm);
	XE_BUG_ON(!xe_bo_is_pinned(bo));
	XE_BUG_ON(!xe_bo_is_user(bo));

	if (bo->ttm.pin_count == 1 && !list_empty(&bo->pinned_link)) {
		spin_lock(&xe->pinned.lock);
		list_del_init(&bo->pinned_link);
		spin_unlock(&xe->pinned.lock);
	}

	ttm_bo_unpin(&bo->ttm);

	/*
	 * FIXME: If we always use the reserve / unreserve functions for locking
	 * we do not need this.
	 */
	ttm_bo_move_to_lru_tail_unlocked(&bo->ttm);
}

void xe_bo_unpin(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);

	XE_BUG_ON(bo->ttm.base.import_attach);
	XE_BUG_ON(!xe_bo_is_pinned(bo));

	if (IS_DGFX(xe) && !(IS_ENABLED(CONFIG_DRM_XE_DEBUG) &&
	    bo->flags & XE_BO_INTERNAL_TEST)) {
		struct ttm_place *place = &(bo->placements[0]);

		if (mem_type_is_vram(place->mem_type)) {
			XE_BUG_ON(list_empty(&bo->pinned_link));

			spin_lock(&xe->pinned.lock);
			list_del_init(&bo->pinned_link);
			spin_unlock(&xe->pinned.lock);
		}
	}

	ttm_bo_unpin(&bo->ttm);
}

/**
 * xe_bo_validate() - Make sure the bo is in an allowed placement
 * @bo: The bo,
 * @vm: Pointer to a the vm the bo shares a locked dma_resv object with, or
 *      NULL. Used together with @allow_res_evict.
 * @allow_res_evict: Whether it's allowed to evict bos sharing @vm's
 *                   reservation object.
 *
 * Make sure the bo is in allowed placement, migrating it if necessary. If
 * needed, other bos will be evicted. If bos selected for eviction shares
 * the @vm's reservation object, they can be evicted iff @allow_res_evict is
 * set to true, otherwise they will be bypassed.
 *
 * Return: 0 on success, negative error code on failure. May return
 * -EINTR or -ERESTARTSYS if internal waits are interrupted by a signal.
 */
int xe_bo_validate(struct xe_bo *bo, struct xe_vm *vm, bool allow_res_evict)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};

	if (vm) {
		lockdep_assert_held(&vm->lock);
		xe_vm_assert_held(vm);

		ctx.allow_res_evict = allow_res_evict;
		ctx.resv = &vm->resv;
	}

	return ttm_bo_validate(&bo->ttm, &bo->placement, &ctx);
}

bool xe_bo_is_xe_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &xe_ttm_bo_destroy)
		return true;

	return false;
}

dma_addr_t xe_bo_addr(struct xe_bo *bo, u64 offset,
		      size_t page_size, bool *is_lmem)
{
	struct xe_res_cursor cur;
	u64 page;

	if (!READ_ONCE(bo->ttm.pin_count))
		xe_bo_assert_held(bo);

	XE_BUG_ON(page_size > PAGE_SIZE);
	page = offset >> PAGE_SHIFT;
	offset &= (PAGE_SIZE - 1);

	*is_lmem = xe_bo_is_vram(bo);

	if (!*is_lmem && !xe_bo_is_stolen(bo)) {
		XE_BUG_ON(!bo->ttm.ttm);

		xe_res_first_sg(xe_bo_get_sg(bo), page << PAGE_SHIFT,
				page_size, &cur);
		return xe_res_dma(&cur) + offset;
	} else {
		struct xe_res_cursor cur;

		xe_res_first(bo->ttm.resource, page << PAGE_SHIFT,
			     page_size, &cur);
		return cur.start + offset + vram_region_io_offset(bo);
	}
}

int xe_bo_vmap(struct xe_bo *bo)
{
	void *virtual;
	bool is_iomem;
	int ret;

	xe_bo_assert_held(bo);

	if (!iosys_map_is_null(&bo->vmap))
		return 0;

	/*
	 * We use this more or less deprecated interface for now since
	 * ttm_bo_vmap() doesn't offer the optimization of kmapping
	 * single page bos, which is done here.
	 * TODO: Fix up ttm_bo_vmap to do that, or fix up ttm_bo_kmap
	 * to use struct iosys_map.
	 */
	ret = ttm_bo_kmap(&bo->ttm, 0, bo->size >> PAGE_SHIFT, &bo->kmap);
	if (ret)
		return ret;

	virtual = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	if (is_iomem)
		iosys_map_set_vaddr_iomem(&bo->vmap, (void __iomem *)virtual);
	else
		iosys_map_set_vaddr(&bo->vmap, virtual);

	return 0;
}

static void __xe_bo_vunmap(struct xe_bo *bo)
{
	if (!iosys_map_is_null(&bo->vmap)) {
		iosys_map_clear(&bo->vmap);
		ttm_bo_kunmap(&bo->kmap);
	}
}

void xe_bo_vunmap(struct xe_bo *bo)
{
	xe_bo_assert_held(bo);
	__xe_bo_vunmap(bo);
}

int xe_gem_create_ioctl(struct drm_device *dev, void *data,
			struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_file *xef = to_xe_file(file);
	struct drm_xe_gem_create *args = data;
	struct ww_acquire_ctx ww;
	struct xe_vm *vm = NULL;
	struct xe_bo *bo;
	unsigned bo_flags = XE_BO_CREATE_USER_BIT;
	u32 handle;
	int err;

	if (XE_IOCTL_ERR(xe, args->extensions))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags &
			 ~(XE_GEM_CREATE_FLAG_DEFER_BACKING |
			   XE_GEM_CREATE_FLAG_SCANOUT |
			   xe->info.mem_region_mask)))
		return -EINVAL;

	/* at least one memory type must be specified */
	if (XE_IOCTL_ERR(xe, !(args->flags & xe->info.mem_region_mask)))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->handle))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, !args->size))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->size > SIZE_MAX))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->size & ~PAGE_MASK))
		return -EINVAL;

	if (args->vm_id) {
		vm = xe_vm_lookup(xef, args->vm_id);
		if (XE_IOCTL_ERR(xe, !vm))
			return -ENOENT;
		err = xe_vm_lock(vm, &ww, 0, true);
		if (err) {
			xe_vm_put(vm);
			return err;
		}
	}

	if (args->flags & XE_GEM_CREATE_FLAG_DEFER_BACKING)
		bo_flags |= XE_BO_DEFER_BACKING;

	if (args->flags & XE_GEM_CREATE_FLAG_SCANOUT)
		bo_flags |= XE_BO_SCANOUT_BIT;

	bo_flags |= args->flags << (ffs(XE_BO_CREATE_SYSTEM_BIT) - 1);
	bo = xe_bo_create(xe, NULL, vm, args->size, ttm_bo_type_device,
			  bo_flags);
	if (vm) {
		xe_vm_unlock(vm, &ww);
		xe_vm_put(vm);
	}

	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = drm_gem_handle_create(file, &bo->ttm.base, &handle);
	xe_bo_put(bo);
	if (err)
		return err;

	args->handle = handle;

	return 0;
}

int xe_gem_mmap_offset_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct drm_xe_gem_mmap_offset *args = data;
	struct drm_gem_object *gem_obj;

	if (XE_IOCTL_ERR(xe, args->extensions))
		return -EINVAL;

	if (XE_IOCTL_ERR(xe, args->flags))
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (XE_IOCTL_ERR(xe, !gem_obj))
		return -ENOENT;

	/* The mmap offset was set up at BO allocation time. */
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

	xe_bo_put(gem_to_xe_bo(gem_obj));
	return 0;
}

int xe_bo_lock(struct xe_bo *bo, struct ww_acquire_ctx *ww,
	       int num_resv, bool intr)
{
	struct ttm_validate_buffer tv_bo;
	LIST_HEAD(objs);
	LIST_HEAD(dups);

	XE_BUG_ON(!ww);

	tv_bo.num_shared = num_resv;
	tv_bo.bo = &bo->ttm;;
	list_add_tail(&tv_bo.head, &objs);

	return ttm_eu_reserve_buffers(ww, &objs, intr, &dups);
}

void xe_bo_unlock(struct xe_bo *bo, struct ww_acquire_ctx *ww)
{
	dma_resv_unlock(bo->ttm.base.resv);
	ww_acquire_fini(ww);
}

/**
 * xe_bo_can_migrate - Whether a buffer object likely can be migrated
 * @bo: The buffer object to migrate
 * @mem_type: The TTM memory type intended to migrate to
 *
 * Check whether the buffer object supports migration to the
 * given memory type. Note that pinning may affect the ability to migrate as
 * returned by this function.
 *
 * This function is primarily intended as a helper for checking the
 * possibility to migrate buffer objects and can be called without
 * the object lock held.
 *
 * Return: true if migration is possible, false otherwise.
 */
bool xe_bo_can_migrate(struct xe_bo *bo, u32 mem_type)
{
	unsigned int cur_place;

	if (bo->ttm.type == ttm_bo_type_kernel)
		return true;

	if (bo->ttm.type == ttm_bo_type_sg)
		return false;

	for (cur_place = 0; cur_place < bo->placement.num_placement;
	     cur_place++) {
		if (bo->placements[cur_place].mem_type == mem_type)
			return true;
	}

	return false;
}

static void xe_place_from_ttm_type(u32 mem_type, struct ttm_place *place)
{
	memset(place, 0, sizeof(*place));
	place->mem_type = mem_type;
}

/**
 * xe_bo_migrate - Migrate an object to the desired region id
 * @bo: The buffer object to migrate.
 * @mem_type: The TTM region type to migrate to.
 *
 * Attempt to migrate the buffer object to the desired memory region. The
 * buffer object may not be pinned, and must be locked.
 * On successful completion, the object memory type will be updated,
 * but an async migration task may not have completed yet, and to
 * accomplish that, the object's kernel fences must be signaled with
 * the object lock held.
 *
 * Return: 0 on success. Negative error code on failure. In particular may
 * return -EINTR or -ERESTARTSYS if signal pending.
 */
int xe_bo_migrate(struct xe_bo *bo, u32 mem_type)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
	};
	struct ttm_placement placement;
	struct ttm_place requested;

	xe_bo_assert_held(bo);

	if (bo->ttm.resource->mem_type == mem_type)
		return 0;

	if (xe_bo_is_pinned(bo))
		return -EBUSY;

	if (!xe_bo_can_migrate(bo, mem_type))
		return -EINVAL;

	xe_place_from_ttm_type(mem_type, &requested);
	placement.num_placement = 1;
	placement.num_busy_placement = 1;
	placement.placement = &requested;
	placement.busy_placement = &requested;

	return ttm_bo_validate(&bo->ttm, &placement, &ctx);
}

/**
 * xe_bo_evict - Evict an object to evict placement
 * @bo: The buffer object to migrate.
 * @force_alloc: Set force_alloc in ttm_operation_ctx
 *
 * On successful completion, the object memory will be moved to evict
 * placement. Ths function blocks until the object has been fully moved.
 *
 * Return: 0 on success. Negative error code on failure.
 */
int xe_bo_evict(struct xe_bo *bo, bool force_alloc)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.no_wait_gpu = false,
		.force_alloc = force_alloc,
	};
	struct ttm_placement placement;
	int ret;

	xe_evict_flags(&bo->ttm, &placement);
	ret = ttm_bo_validate(&bo->ttm, &placement, &ctx);
	if (ret)
		return ret;

	dma_resv_wait_timeout(bo->ttm.base.resv, DMA_RESV_USAGE_KERNEL,
			      false, MAX_SCHEDULE_TIMEOUT);

	return 0;
}

/**
 * xe_bo_needs_ccs_pages - Whether a bo needs to back up CCS pages when
 * placed in system memory.
 * @bo: The xe_bo
 *
 * If a bo has an allowable placement in XE_PL_TT memory, it can't use
 * flat CCS compression, because the GPU then has no way to access the
 * CCS metadata using relevant commands. For the opposite case, we need to
 * allocate storage for the CCS metadata when the BO is not resident in
 * VRAM memory.
 *
 * Return: true if extra pages need to be allocated, false otherwise.
 */
bool xe_bo_needs_ccs_pages(struct xe_bo *bo)
{
	return bo->ttm.type == ttm_bo_type_device &&
		!(bo->flags & XE_BO_CREATE_SYSTEM_BIT) &&
		(bo->flags & (XE_BO_CREATE_VRAM0_BIT | XE_BO_CREATE_VRAM1_BIT));
}

/**
 * __xe_bo_release_dummy() - Dummy kref release function
 * @kref: The embedded struct kref.
 *
 * Dummy release function for xe_bo_put_deferred(). Keep off.
 */
void __xe_bo_release_dummy(struct kref *kref)
{
}

/**
 * xe_bo_put_commit() - Put bos whose put was deferred by xe_bo_put_deferred().
 * @deferred: The lockless list used for the call to xe_bo_put_deferred().
 *
 * Puts all bos whose put was deferred by xe_bo_put_deferred().
 * The @deferred list can be either an onstack local list or a global
 * shared list used by a workqueue.
 */
void xe_bo_put_commit(struct llist_head *deferred)
{
	struct llist_node *freed;
	struct xe_bo *bo, *next;

	if (!deferred)
		return;

	freed = llist_del_all(deferred);
	if (!freed)
		return;

	llist_for_each_entry_safe(bo, next, freed, freed)
		drm_gem_object_free(&bo->ttm.base.refcount);
}

/**
 * xe_bo_dumb_create - Create a dumb bo as backing for a fb
 * @file_priv: ...
 * @dev: ...
 * @args: ...
 *
 * See dumb_create() hook in include/drm/drm_drv.h
 *
 * Return: ...
 */
int xe_bo_dumb_create(struct drm_file *file_priv,
		      struct drm_device *dev,
		      struct drm_mode_create_dumb *args)
{
	struct xe_device *xe = to_xe_device(dev);
	struct xe_bo *bo;
	uint32_t handle;
	int cpp = DIV_ROUND_UP(args->bpp, 8);
	int err;
	u32 page_size = max_t(u32, PAGE_SIZE,
		xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K ? SZ_64K : SZ_4K);

	args->pitch = ALIGN(args->width * cpp, 64);
	args->size = ALIGN(mul_u32_u32(args->pitch, args->height),
			   page_size);

	bo = xe_bo_create(xe, NULL, NULL, args->size, ttm_bo_type_device,
			  XE_BO_CREATE_VRAM_IF_DGFX(to_gt(xe)) |
			  XE_BO_CREATE_USER_BIT | XE_BO_SCANOUT_BIT);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = drm_gem_handle_create(file_priv, &bo->ttm.base, &handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(&bo->ttm.base);
	if (!err)
		args->handle = handle;
	return err;
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_bo.c"
#endif
