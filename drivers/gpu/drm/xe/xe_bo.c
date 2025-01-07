// SPDX-License-Identifier: MIT
/*
 * Copyright © 2021 Intel Corporation
 */

#include "xe_bo.h"

#include <linux/dma-buf.h>

#include <drm/drm_drv.h>
#include <drm/drm_gem_ttm_helper.h>
#include <drm/drm_managed.h>
#include <drm/ttm/ttm_device.h>
#include <drm/ttm/ttm_placement.h>
#include <drm/ttm/ttm_tt.h>
#include <uapi/drm/xe_drm.h>

#include "xe_device.h"
#include "xe_dma_buf.h"
#include "xe_drm_client.h"
#include "xe_ggtt.h"
#include "xe_gt.h"
#include "xe_map.h"
#include "xe_migrate.h"
#include "xe_pm.h"
#include "xe_preempt_fence.h"
#include "xe_res_cursor.h"
#include "xe_trace_bo.h"
#include "xe_ttm_stolen_mgr.h"
#include "xe_vm.h"

const char *const xe_mem_type_to_name[TTM_NUM_MEM_TYPES]  = {
	[XE_PL_SYSTEM] = "system",
	[XE_PL_TT] = "gtt",
	[XE_PL_VRAM0] = "vram0",
	[XE_PL_VRAM1] = "vram1",
	[XE_PL_STOLEN] = "stolen"
};

static const struct ttm_place sys_placement_flags = {
	.fpfn = 0,
	.lpfn = 0,
	.mem_type = XE_PL_SYSTEM,
	.flags = 0,
};

static struct ttm_placement sys_placement = {
	.num_placement = 1,
	.placement = &sys_placement_flags,
};

static const struct ttm_place tt_placement_flags[] = {
	{
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = XE_PL_TT,
		.flags = TTM_PL_FLAG_DESIRED,
	},
	{
		.fpfn = 0,
		.lpfn = 0,
		.mem_type = XE_PL_SYSTEM,
		.flags = TTM_PL_FLAG_FALLBACK,
	}
};

static struct ttm_placement tt_placement = {
	.num_placement = 2,
	.placement = tt_placement_flags,
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

/**
 * xe_bo_has_single_placement - check if BO is placed only in one memory location
 * @bo: The BO
 *
 * This function checks whether a given BO is placed in only one memory location.
 *
 * Returns: true if the BO is placed in a single memory location, false otherwise.
 *
 */
bool xe_bo_has_single_placement(struct xe_bo *bo)
{
	return bo->placement.num_placement == 1;
}

/**
 * xe_bo_is_stolen_devmem - check if BO is of stolen type accessed via PCI BAR
 * @bo: The BO
 *
 * The stolen memory is accessed through the PCI BAR for both DGFX and some
 * integrated platforms that have a dedicated bit in the PTE for devmem (DM).
 *
 * Returns: true if it's stolen memory accessed via PCI BAR, false otherwise.
 */
bool xe_bo_is_stolen_devmem(struct xe_bo *bo)
{
	return xe_bo_is_stolen(bo) &&
		GRAPHICS_VERx100(xe_bo_device(bo)) >= 1270;
}

static bool xe_bo_is_user(struct xe_bo *bo)
{
	return bo->flags & XE_BO_FLAG_USER;
}

static struct xe_migrate *
mem_type_to_migrate(struct xe_device *xe, u32 mem_type)
{
	struct xe_tile *tile;

	xe_assert(xe, mem_type == XE_PL_STOLEN || mem_type_is_vram(mem_type));
	tile = &xe->tiles[mem_type == XE_PL_STOLEN ? 0 : (mem_type - XE_PL_VRAM0)];
	return tile->migrate;
}

static struct xe_mem_region *res_to_mem_region(struct ttm_resource *res)
{
	struct xe_device *xe = ttm_to_xe_device(res->bo->bdev);
	struct ttm_resource_manager *mgr;

	xe_assert(xe, resource_is_vram(res));
	mgr = ttm_manager_type(&xe->ttm, res->mem_type);
	return to_xe_ttm_vram_mgr(mgr)->vram;
}

static void try_add_system(struct xe_device *xe, struct xe_bo *bo,
			   u32 bo_flags, u32 *c)
{
	if (bo_flags & XE_BO_FLAG_SYSTEM) {
		xe_assert(xe, *c < ARRAY_SIZE(bo->placements));

		bo->placements[*c] = (struct ttm_place) {
			.mem_type = XE_PL_TT,
		};
		*c += 1;
	}
}

static bool force_contiguous(u32 bo_flags)
{
	/*
	 * For eviction / restore on suspend / resume objects pinned in VRAM
	 * must be contiguous, also only contiguous BOs support xe_bo_vmap.
	 */
	return bo_flags & (XE_BO_FLAG_PINNED | XE_BO_FLAG_GGTT);
}

static void add_vram(struct xe_device *xe, struct xe_bo *bo,
		     struct ttm_place *places, u32 bo_flags, u32 mem_type, u32 *c)
{
	struct ttm_place place = { .mem_type = mem_type };
	struct xe_mem_region *vram;
	u64 io_size;

	xe_assert(xe, *c < ARRAY_SIZE(bo->placements));

	vram = to_xe_ttm_vram_mgr(ttm_manager_type(&xe->ttm, mem_type))->vram;
	xe_assert(xe, vram && vram->usable_size);
	io_size = vram->io_size;

	if (force_contiguous(bo_flags))
		place.flags |= TTM_PL_FLAG_CONTIGUOUS;

	if (io_size < vram->usable_size) {
		if (bo_flags & XE_BO_FLAG_NEEDS_CPU_ACCESS) {
			place.fpfn = 0;
			place.lpfn = io_size >> PAGE_SHIFT;
		} else {
			place.flags |= TTM_PL_FLAG_TOPDOWN;
		}
	}
	places[*c] = place;
	*c += 1;
}

static void try_add_vram(struct xe_device *xe, struct xe_bo *bo,
			 u32 bo_flags, u32 *c)
{
	if (bo_flags & XE_BO_FLAG_VRAM0)
		add_vram(xe, bo, bo->placements, bo_flags, XE_PL_VRAM0, c);
	if (bo_flags & XE_BO_FLAG_VRAM1)
		add_vram(xe, bo, bo->placements, bo_flags, XE_PL_VRAM1, c);
}

static void try_add_stolen(struct xe_device *xe, struct xe_bo *bo,
			   u32 bo_flags, u32 *c)
{
	if (bo_flags & XE_BO_FLAG_STOLEN) {
		xe_assert(xe, *c < ARRAY_SIZE(bo->placements));

		bo->placements[*c] = (struct ttm_place) {
			.mem_type = XE_PL_STOLEN,
			.flags = force_contiguous(bo_flags) ?
				TTM_PL_FLAG_CONTIGUOUS : 0,
		};
		*c += 1;
	}
}

static int __xe_bo_placement_for_flags(struct xe_device *xe, struct xe_bo *bo,
				       u32 bo_flags)
{
	u32 c = 0;

	try_add_vram(xe, bo, bo_flags, &c);
	try_add_system(xe, bo, bo_flags, &c);
	try_add_stolen(xe, bo, bo_flags, &c);

	if (!c)
		return -EINVAL;

	bo->placement = (struct ttm_placement) {
		.num_placement = c,
		.placement = bo->placements,
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
	if (!xe_bo_is_xe_bo(tbo)) {
		/* Don't handle scatter gather BOs */
		if (tbo->type == ttm_bo_type_sg) {
			placement->num_placement = 0;
			return;
		}

		*placement = sys_placement;
		return;
	}

	/*
	 * For xe, sg bos that are evicted to system just triggers a
	 * rebind of the sg list upon subsequent validation to XE_PL_TT.
	 */
	switch (tbo->resource->mem_type) {
	case XE_PL_VRAM0:
	case XE_PL_VRAM1:
	case XE_PL_STOLEN:
		*placement = tt_placement;
		break;
	case XE_PL_TT:
	default:
		*placement = sys_placement;
		break;
	}
}

struct xe_ttm_tt {
	struct ttm_tt ttm;
	struct device *dev;
	struct sg_table sgt;
	struct sg_table *sg;
	/** @purgeable: Whether the content of the pages of @ttm is purgeable. */
	bool purgeable;
};

static int xe_tt_map_sg(struct ttm_tt *tt)
{
	struct xe_ttm_tt *xe_tt = container_of(tt, struct xe_ttm_tt, ttm);
	unsigned long num_pages = tt->num_pages;
	int ret;

	XE_WARN_ON(tt->page_flags & TTM_TT_FLAG_EXTERNAL);

	if (xe_tt->sg)
		return 0;

	ret = sg_alloc_table_from_pages_segment(&xe_tt->sgt, tt->pages,
						num_pages, 0,
						(u64)num_pages << PAGE_SHIFT,
						xe_sg_segment_size(xe_tt->dev),
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

static void xe_tt_unmap_sg(struct ttm_tt *tt)
{
	struct xe_ttm_tt *xe_tt = container_of(tt, struct xe_ttm_tt, ttm);

	if (xe_tt->sg) {
		dma_unmap_sgtable(xe_tt->dev, xe_tt->sg,
				  DMA_BIDIRECTIONAL, 0);
		sg_free_table(xe_tt->sg);
		xe_tt->sg = NULL;
	}
}

struct sg_table *xe_bo_sg(struct xe_bo *bo)
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
	unsigned long extra_pages;
	enum ttm_caching caching = ttm_cached;
	int err;

	tt = kzalloc(sizeof(*tt), GFP_KERNEL);
	if (!tt)
		return NULL;

	tt->dev = xe->drm.dev;

	extra_pages = 0;
	if (xe_bo_needs_ccs_pages(bo))
		extra_pages = DIV_ROUND_UP(xe_device_ccs_bytes(xe, bo->size),
					   PAGE_SIZE);

	/*
	 * DGFX system memory is always WB / ttm_cached, since
	 * other caching modes are only supported on x86. DGFX
	 * GPU system memory accesses are always coherent with the
	 * CPU.
	 */
	if (!IS_DGFX(xe)) {
		switch (bo->cpu_caching) {
		case DRM_XE_GEM_CPU_CACHING_WC:
			caching = ttm_write_combined;
			break;
		default:
			caching = ttm_cached;
			break;
		}

		WARN_ON((bo->flags & XE_BO_FLAG_USER) && !bo->cpu_caching);

		/*
		 * Display scanout is always non-coherent with the CPU cache.
		 *
		 * For Xe_LPG and beyond, PPGTT PTE lookups are also
		 * non-coherent and require a CPU:WC mapping.
		 */
		if ((!bo->cpu_caching && bo->flags & XE_BO_FLAG_SCANOUT) ||
		    (xe->info.graphics_verx100 >= 1270 &&
		     bo->flags & XE_BO_FLAG_PAGETABLE))
			caching = ttm_write_combined;
	}

	if (bo->flags & XE_BO_FLAG_NEEDS_UC) {
		/*
		 * Valid only for internally-created buffers only, for
		 * which cpu_caching is never initialized.
		 */
		xe_assert(xe, bo->cpu_caching == 0);
		caching = ttm_uncached;
	}

	err = ttm_tt_init(&tt->ttm, &bo->ttm, page_flags, caching, extra_pages);
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

	return err;
}

static void xe_ttm_tt_unpopulate(struct ttm_device *ttm_dev, struct ttm_tt *tt)
{
	if (tt->page_flags & TTM_TT_FLAG_EXTERNAL)
		return;

	xe_tt_unmap_sg(tt);

	return ttm_pool_free(&ttm_dev->pool, tt);
}

static void xe_ttm_tt_destroy(struct ttm_device *ttm_dev, struct ttm_tt *tt)
{
	ttm_tt_fini(tt);
	kfree(tt);
}

static bool xe_ttm_resource_visible(struct ttm_resource *mem)
{
	struct xe_ttm_vram_mgr_resource *vres =
		to_xe_ttm_vram_mgr_resource(mem);

	return vres->used_visible_size == mem->size;
}

static int xe_ttm_io_mem_reserve(struct ttm_device *bdev,
				 struct ttm_resource *mem)
{
	struct xe_device *xe = ttm_to_xe_device(bdev);

	switch (mem->mem_type) {
	case XE_PL_SYSTEM:
	case XE_PL_TT:
		return 0;
	case XE_PL_VRAM0:
	case XE_PL_VRAM1: {
		struct xe_mem_region *vram = res_to_mem_region(mem);

		if (!xe_ttm_resource_visible(mem))
			return -EINVAL;

		mem->bus.offset = mem->start << PAGE_SHIFT;

		if (vram->mapping &&
		    mem->placement & TTM_PL_FLAG_CONTIGUOUS)
			mem->bus.addr = (u8 __force *)vram->mapping +
				mem->bus.offset;

		mem->bus.offset += vram->io_start;
		mem->bus.is_iomem = true;

#if  !IS_ENABLED(CONFIG_X86)
		mem->bus.caching = ttm_write_combined;
#endif
		return 0;
	} case XE_PL_STOLEN:
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
	struct drm_gem_object *obj = &bo->ttm.base;
	struct drm_gpuvm_bo *vm_bo;
	bool idle = false;
	int ret = 0;

	dma_resv_assert_held(bo->ttm.base.resv);

	if (!list_empty(&bo->ttm.base.gpuva.list)) {
		dma_resv_iter_begin(&cursor, bo->ttm.base.resv,
				    DMA_RESV_USAGE_BOOKKEEP);
		dma_resv_for_each_fence_unlocked(&cursor, fence)
			dma_fence_enable_sw_signaling(fence);
		dma_resv_iter_end(&cursor);
	}

	drm_gem_for_each_gpuvm_bo(vm_bo, obj) {
		struct xe_vm *vm = gpuvm_to_vm(vm_bo->vm);
		struct drm_gpuva *gpuva;

		if (!xe_vm_in_fault_mode(vm)) {
			drm_gpuvm_bo_evict(vm_bo, true);
			continue;
		}

		if (!idle) {
			long timeout;

			if (ctx->no_wait_gpu &&
			    !dma_resv_test_signaled(bo->ttm.base.resv,
						    DMA_RESV_USAGE_BOOKKEEP))
				return -EBUSY;

			timeout = dma_resv_wait_timeout(bo->ttm.base.resv,
							DMA_RESV_USAGE_BOOKKEEP,
							ctx->interruptible,
							MAX_SCHEDULE_TIMEOUT);
			if (!timeout)
				return -ETIME;
			if (timeout < 0)
				return timeout;

			idle = true;
		}

		drm_gpuvm_bo_for_each_va(gpuva, vm_bo) {
			struct xe_vma *vma = gpuva_to_vma(gpuva);

			trace_xe_vma_evict(vma);
			ret = xe_vm_invalidate_vma(vma);
			if (XE_WARN_ON(ret))
				return ret;
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
			     struct ttm_resource *new_res)
{
	struct dma_buf_attachment *attach = ttm_bo->base.import_attach;
	struct xe_ttm_tt *xe_tt = container_of(ttm_bo->ttm, struct xe_ttm_tt,
					       ttm);
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	struct sg_table *sg;

	xe_assert(xe, attach);
	xe_assert(xe, ttm_bo->ttm);

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
	struct ttm_resource *old_mem = ttm_bo->resource;
	u32 old_mem_type = old_mem ? old_mem->mem_type : XE_PL_SYSTEM;
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

	/*
	 * TTM has already nuked the mmap for us (see ttm_bo_unmap_virtual),
	 * so if we moved from VRAM make sure to unlink this from the userfault
	 * tracking.
	 */
	if (mem_type_is_vram(old_mem_type)) {
		mutex_lock(&xe->mem_access.vram_userfault.lock);
		if (!list_empty(&bo->vram_userfault_link))
			list_del_init(&bo->vram_userfault_link);
		mutex_unlock(&xe->mem_access.vram_userfault.lock);
	}

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
	u32 old_mem_type = old_mem ? old_mem->mem_type : XE_PL_SYSTEM;
	struct ttm_tt *ttm = ttm_bo->ttm;
	struct xe_migrate *migrate = NULL;
	struct dma_fence *fence;
	bool move_lacks_source;
	bool tt_has_data;
	bool needs_clear;
	bool handle_system_ccs = (!IS_DGFX(xe) && xe_bo_needs_ccs_pages(bo) &&
				  ttm && ttm_tt_is_populated(ttm)) ? true : false;
	int ret = 0;

	/* Bo creation path, moving to system or TT. */
	if ((!old_mem && ttm) && !handle_system_ccs) {
		if (new_mem->mem_type == XE_PL_TT)
			ret = xe_tt_map_sg(ttm);
		if (!ret)
			ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	if (ttm_bo->type == ttm_bo_type_sg) {
		ret = xe_bo_move_notify(bo, ctx);
		if (!ret)
			ret = xe_bo_move_dmabuf(ttm_bo, new_mem);
		return ret;
	}

	tt_has_data = ttm && (ttm_tt_is_populated(ttm) ||
			      (ttm->page_flags & TTM_TT_FLAG_SWAPPED));

	move_lacks_source = !old_mem || (handle_system_ccs ? (!bo->ccs_cleared) :
					 (!mem_type_is_vram(old_mem_type) && !tt_has_data));

	needs_clear = (ttm && ttm->page_flags & TTM_TT_FLAG_ZERO_ALLOC) ||
		(!ttm && ttm_bo->type == ttm_bo_type_device);

	if (new_mem->mem_type == XE_PL_TT) {
		ret = xe_tt_map_sg(ttm);
		if (ret)
			goto out;
	}

	if ((move_lacks_source && !needs_clear)) {
		ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	if (old_mem_type == XE_PL_SYSTEM && new_mem->mem_type == XE_PL_TT && !handle_system_ccs) {
		ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	/*
	 * Failed multi-hop where the old_mem is still marked as
	 * TTM_PL_FLAG_TEMPORARY, should just be a dummy move.
	 */
	if (old_mem_type == XE_PL_TT &&
	    new_mem->mem_type == XE_PL_TT) {
		ttm_bo_move_null(ttm_bo, new_mem);
		goto out;
	}

	if (!move_lacks_source && !xe_bo_is_pinned(bo)) {
		ret = xe_bo_move_notify(bo, ctx);
		if (ret)
			goto out;
	}

	if (old_mem_type == XE_PL_TT &&
	    new_mem->mem_type == XE_PL_SYSTEM) {
		long timeout = dma_resv_wait_timeout(ttm_bo->base.resv,
						     DMA_RESV_USAGE_BOOKKEEP,
						     true,
						     MAX_SCHEDULE_TIMEOUT);
		if (timeout < 0) {
			ret = timeout;
			goto out;
		}

		if (!handle_system_ccs) {
			ttm_bo_move_null(ttm_bo, new_mem);
			goto out;
		}
	}

	if (!move_lacks_source &&
	    ((old_mem_type == XE_PL_SYSTEM && resource_is_vram(new_mem)) ||
	     (mem_type_is_vram(old_mem_type) &&
	      new_mem->mem_type == XE_PL_SYSTEM))) {
		hop->fpfn = 0;
		hop->lpfn = 0;
		hop->mem_type = XE_PL_TT;
		hop->flags = TTM_PL_FLAG_TEMPORARY;
		ret = -EMULTIHOP;
		goto out;
	}

	if (bo->tile)
		migrate = bo->tile->migrate;
	else if (resource_is_vram(new_mem))
		migrate = mem_type_to_migrate(xe, new_mem->mem_type);
	else if (mem_type_is_vram(old_mem_type))
		migrate = mem_type_to_migrate(xe, old_mem_type);
	else
		migrate = xe->tiles[0].migrate;

	xe_assert(xe, migrate);
	trace_xe_bo_move(bo, new_mem->mem_type, old_mem_type, move_lacks_source);
	if (xe_rpm_reclaim_safe(xe)) {
		/*
		 * We might be called through swapout in the validation path of
		 * another TTM device, so acquire rpm here.
		 */
		xe_pm_runtime_get(xe);
	} else {
		drm_WARN_ON(&xe->drm, handle_system_ccs);
		xe_pm_runtime_get_noresume(xe);
	}

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
				struct xe_mem_region *vram = res_to_mem_region(new_mem);
				void __iomem *new_addr = vram->mapping +
					(new_mem->start << PAGE_SHIFT);

				if (XE_WARN_ON(new_mem->start == XE_BO_INVALID_OFFSET)) {
					ret = -EINVAL;
					xe_pm_runtime_put(xe);
					goto out;
				}

				xe_assert(xe, new_mem->start ==
					  bo->placements->fpfn);

				iosys_map_set_vaddr_iomem(&bo->vmap, new_addr);
			}
		}
	} else {
		if (move_lacks_source) {
			u32 flags = 0;

			if (mem_type_is_vram(new_mem->mem_type))
				flags |= XE_MIGRATE_CLEAR_FLAG_FULL;
			else if (handle_system_ccs)
				flags |= XE_MIGRATE_CLEAR_FLAG_CCS_DATA;

			fence = xe_migrate_clear(migrate, bo, new_mem, flags);
		}
		else
			fence = xe_migrate_copy(migrate, bo, bo, old_mem,
						new_mem, handle_system_ccs);
		if (IS_ERR(fence)) {
			ret = PTR_ERR(fence);
			xe_pm_runtime_put(xe);
			goto out;
		}
		if (!move_lacks_source) {
			ret = ttm_bo_move_accel_cleanup(ttm_bo, fence, evict,
							true, new_mem);
			if (ret) {
				dma_fence_wait(fence, false);
				ttm_bo_move_null(ttm_bo, new_mem);
				ret = 0;
			}
		} else {
			/*
			 * ttm_bo_move_accel_cleanup() may blow up if
			 * bo->resource == NULL, so just attach the
			 * fence and set the new resource.
			 */
			dma_resv_add_fence(ttm_bo->base.resv, fence,
					   DMA_RESV_USAGE_KERNEL);
			ttm_bo_move_null(ttm_bo, new_mem);
		}

		dma_fence_put(fence);
	}

	xe_pm_runtime_put(xe);

out:
	if ((!ttm_bo->resource || ttm_bo->resource->mem_type == XE_PL_SYSTEM) &&
	    ttm_bo->ttm)
		xe_tt_unmap_sg(ttm_bo->ttm);

	return ret;
}

/**
 * xe_bo_evict_pinned() - Evict a pinned VRAM object to system memory
 * @bo: The buffer object to move.
 *
 * On successful completion, the object memory will be moved to sytem memory.
 *
 * This is needed to for special handling of pinned VRAM object during
 * suspend-resume.
 *
 * Return: 0 on success. Negative error code on failure.
 */
int xe_bo_evict_pinned(struct xe_bo *bo)
{
	struct ttm_place place = {
		.mem_type = XE_PL_TT,
	};
	struct ttm_placement placement = {
		.placement = &place,
		.num_placement = 1,
	};
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.gfp_retry_mayfail = true,
	};
	struct ttm_resource *new_mem;
	int ret;

	xe_bo_assert_held(bo);

	if (WARN_ON(!bo->ttm.resource))
		return -EINVAL;

	if (WARN_ON(!xe_bo_is_pinned(bo)))
		return -EINVAL;

	if (!xe_bo_is_vram(bo))
		return 0;

	ret = ttm_bo_mem_space(&bo->ttm, &placement, &new_mem, &ctx);
	if (ret)
		return ret;

	if (!bo->ttm.ttm) {
		bo->ttm.ttm = xe_ttm_tt_create(&bo->ttm, 0);
		if (!bo->ttm.ttm) {
			ret = -ENOMEM;
			goto err_res_free;
		}
	}

	ret = ttm_bo_populate(&bo->ttm, &ctx);
	if (ret)
		goto err_res_free;

	ret = dma_resv_reserve_fences(bo->ttm.base.resv, 1);
	if (ret)
		goto err_res_free;

	ret = xe_bo_move(&bo->ttm, false, &ctx, new_mem, NULL);
	if (ret)
		goto err_res_free;

	return 0;

err_res_free:
	ttm_resource_free(&bo->ttm, &new_mem);
	return ret;
}

/**
 * xe_bo_restore_pinned() - Restore a pinned VRAM object
 * @bo: The buffer object to move.
 *
 * On successful completion, the object memory will be moved back to VRAM.
 *
 * This is needed to for special handling of pinned VRAM object during
 * suspend-resume.
 *
 * Return: 0 on success. Negative error code on failure.
 */
int xe_bo_restore_pinned(struct xe_bo *bo)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.gfp_retry_mayfail = false,
	};
	struct ttm_resource *new_mem;
	struct ttm_place *place = &bo->placements[0];
	int ret;

	xe_bo_assert_held(bo);

	if (WARN_ON(!bo->ttm.resource))
		return -EINVAL;

	if (WARN_ON(!xe_bo_is_pinned(bo)))
		return -EINVAL;

	if (WARN_ON(xe_bo_is_vram(bo)))
		return -EINVAL;

	if (WARN_ON(!bo->ttm.ttm && !xe_bo_is_stolen(bo)))
		return -EINVAL;

	if (!mem_type_is_vram(place->mem_type))
		return 0;

	ret = ttm_bo_mem_space(&bo->ttm, &bo->placement, &new_mem, &ctx);
	if (ret)
		return ret;

	ret = ttm_bo_populate(&bo->ttm, &ctx);
	if (ret)
		goto err_res_free;

	ret = dma_resv_reserve_fences(bo->ttm.base.resv, 1);
	if (ret)
		goto err_res_free;

	ret = xe_bo_move(&bo->ttm, false, &ctx, new_mem, NULL);
	if (ret)
		goto err_res_free;

	return 0;

err_res_free:
	ttm_resource_free(&bo->ttm, &new_mem);
	return ret;
}

static unsigned long xe_ttm_io_mem_pfn(struct ttm_buffer_object *ttm_bo,
				       unsigned long page_offset)
{
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct xe_res_cursor cursor;
	struct xe_mem_region *vram;

	if (ttm_bo->resource->mem_type == XE_PL_STOLEN)
		return xe_ttm_stolen_io_offset(bo, page_offset << PAGE_SHIFT) >> PAGE_SHIFT;

	vram = res_to_mem_region(ttm_bo->resource);
	xe_res_first(ttm_bo->resource, (u64)page_offset << PAGE_SHIFT, 0, &cursor);
	return (vram->io_start + cursor.start) >> PAGE_SHIFT;
}

static void __xe_bo_vunmap(struct xe_bo *bo);

/*
 * TODO: Move this function to TTM so we don't rely on how TTM does its
 * locking, thereby abusing TTM internals.
 */
static bool xe_ttm_bo_lock_in_destructor(struct ttm_buffer_object *ttm_bo)
{
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	bool locked;

	xe_assert(xe, !kref_read(&ttm_bo->kref));

	/*
	 * We can typically only race with TTM trylocking under the
	 * lru_lock, which will immediately be unlocked again since
	 * the ttm_bo refcount is zero at this point. So trylocking *should*
	 * always succeed here, as long as we hold the lru lock.
	 */
	spin_lock(&ttm_bo->bdev->lru_lock);
	locked = dma_resv_trylock(ttm_bo->base.resv);
	spin_unlock(&ttm_bo->bdev->lru_lock);
	xe_assert(xe, locked);

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
	xe_assert(xe_bo_device(bo), !(bo->created && kref_read(&ttm_bo->base.refcount)));

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

static void xe_ttm_bo_purge(struct ttm_buffer_object *ttm_bo, struct ttm_operation_ctx *ctx)
{
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);

	if (ttm_bo->ttm) {
		struct ttm_placement place = {};
		int ret = ttm_bo_validate(ttm_bo, &place, ctx);

		drm_WARN_ON(&xe->drm, ret);
	}
}

static void xe_ttm_bo_swap_notify(struct ttm_buffer_object *ttm_bo)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = false,
		.gfp_retry_mayfail = false,
	};

	if (ttm_bo->ttm) {
		struct xe_ttm_tt *xe_tt =
			container_of(ttm_bo->ttm, struct xe_ttm_tt, ttm);

		if (xe_tt->purgeable)
			xe_ttm_bo_purge(ttm_bo, &ctx);
	}
}

static int xe_ttm_access_memory(struct ttm_buffer_object *ttm_bo,
				unsigned long offset, void *buf, int len,
				int write)
{
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	struct iosys_map vmap;
	struct xe_res_cursor cursor;
	struct xe_mem_region *vram;
	int bytes_left = len;

	xe_bo_assert_held(bo);
	xe_device_assert_mem_access(xe);

	if (!mem_type_is_vram(ttm_bo->resource->mem_type))
		return -EIO;

	/* FIXME: Use GPU for non-visible VRAM */
	if (!xe_ttm_resource_visible(ttm_bo->resource))
		return -EIO;

	vram = res_to_mem_region(ttm_bo->resource);
	xe_res_first(ttm_bo->resource, offset & PAGE_MASK,
		     bo->size - (offset & PAGE_MASK), &cursor);

	do {
		unsigned long page_offset = (offset & ~PAGE_MASK);
		int byte_count = min((int)(PAGE_SIZE - page_offset), bytes_left);

		iosys_map_set_vaddr_iomem(&vmap, (u8 __iomem *)vram->mapping +
					  cursor.start);
		if (write)
			xe_map_memcpy_to(xe, &vmap, page_offset, buf, byte_count);
		else
			xe_map_memcpy_from(xe, buf, &vmap, page_offset, byte_count);

		buf += byte_count;
		offset += byte_count;
		bytes_left -= byte_count;
		if (bytes_left)
			xe_res_next(&cursor, PAGE_SIZE);
	} while (bytes_left);

	return len;
}

const struct ttm_device_funcs xe_ttm_funcs = {
	.ttm_tt_create = xe_ttm_tt_create,
	.ttm_tt_populate = xe_ttm_tt_populate,
	.ttm_tt_unpopulate = xe_ttm_tt_unpopulate,
	.ttm_tt_destroy = xe_ttm_tt_destroy,
	.evict_flags = xe_evict_flags,
	.move = xe_bo_move,
	.io_mem_reserve = xe_ttm_io_mem_reserve,
	.io_mem_pfn = xe_ttm_io_mem_pfn,
	.access_memory = xe_ttm_access_memory,
	.release_notify = xe_ttm_bo_release_notify,
	.eviction_valuable = ttm_bo_eviction_valuable,
	.delete_mem_notify = xe_ttm_bo_delete_mem_notify,
	.swap_notify = xe_ttm_bo_swap_notify,
};

static void xe_ttm_bo_destroy(struct ttm_buffer_object *ttm_bo)
{
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct xe_device *xe = ttm_to_xe_device(ttm_bo->bdev);
	struct xe_tile *tile;
	u8 id;

	if (bo->ttm.base.import_attach)
		drm_prime_gem_destroy(&bo->ttm.base, NULL);
	drm_gem_object_release(&bo->ttm.base);

	xe_assert(xe, list_empty(&ttm_bo->base.gpuva.list));

	for_each_tile(tile, xe, id)
		if (bo->ggtt_node[id] && bo->ggtt_node[id]->base.size)
			xe_ggtt_remove_bo(tile->mem.ggtt, bo);

#ifdef CONFIG_PROC_FS
	if (bo->client)
		xe_drm_client_remove_bo(bo);
#endif

	if (bo->vm && xe_bo_is_user(bo))
		xe_vm_put(bo->vm);

	mutex_lock(&xe->mem_access.vram_userfault.lock);
	if (!list_empty(&bo->vram_userfault_link))
		list_del(&bo->vram_userfault_link);
	mutex_unlock(&xe->mem_access.vram_userfault.lock);

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

static void xe_gem_object_close(struct drm_gem_object *obj,
				struct drm_file *file_priv)
{
	struct xe_bo *bo = gem_to_xe_bo(obj);

	if (bo->vm && !xe_vm_in_fault_mode(bo->vm)) {
		xe_assert(xe_bo_device(bo), xe_bo_is_user(bo));

		xe_bo_lock(bo, false);
		ttm_bo_set_bulk_move(&bo->ttm, NULL);
		xe_bo_unlock(bo);
	}
}

static vm_fault_t xe_gem_fault(struct vm_fault *vmf)
{
	struct ttm_buffer_object *tbo = vmf->vma->vm_private_data;
	struct drm_device *ddev = tbo->base.dev;
	struct xe_device *xe = to_xe_device(ddev);
	struct xe_bo *bo = ttm_to_xe_bo(tbo);
	bool needs_rpm = bo->flags & XE_BO_FLAG_VRAM_MASK;
	vm_fault_t ret;
	int idx;

	if (needs_rpm)
		xe_pm_runtime_get(xe);

	ret = ttm_bo_vm_reserve(tbo, vmf);
	if (ret)
		goto out;

	if (drm_dev_enter(ddev, &idx)) {
		trace_xe_bo_cpu_fault(bo);

		ret = ttm_bo_vm_fault_reserved(vmf, vmf->vma->vm_page_prot,
					       TTM_BO_VM_NUM_PREFAULT);
		drm_dev_exit(idx);
	} else {
		ret = ttm_bo_vm_dummy_page(vmf, vmf->vma->vm_page_prot);
	}

	if (ret == VM_FAULT_RETRY && !(vmf->flags & FAULT_FLAG_RETRY_NOWAIT))
		goto out;
	/*
	 * ttm_bo_vm_reserve() already has dma_resv_lock.
	 */
	if (ret == VM_FAULT_NOPAGE && mem_type_is_vram(tbo->resource->mem_type)) {
		mutex_lock(&xe->mem_access.vram_userfault.lock);
		if (list_empty(&bo->vram_userfault_link))
			list_add(&bo->vram_userfault_link, &xe->mem_access.vram_userfault.list);
		mutex_unlock(&xe->mem_access.vram_userfault.lock);
	}

	dma_resv_unlock(tbo->base.resv);
out:
	if (needs_rpm)
		xe_pm_runtime_put(xe);

	return ret;
}

static int xe_bo_vm_access(struct vm_area_struct *vma, unsigned long addr,
			   void *buf, int len, int write)
{
	struct ttm_buffer_object *ttm_bo = vma->vm_private_data;
	struct xe_bo *bo = ttm_to_xe_bo(ttm_bo);
	struct xe_device *xe = xe_bo_device(bo);
	int ret;

	xe_pm_runtime_get(xe);
	ret = ttm_bo_vm_access(vma, addr, buf, len, write);
	xe_pm_runtime_put(xe);

	return ret;
}

/**
 * xe_bo_read() - Read from an xe_bo
 * @bo: The buffer object to read from.
 * @offset: The byte offset to start reading from.
 * @dst: Location to store the read.
 * @size: Size in bytes for the read.
 *
 * Read @size bytes from the @bo, starting from @offset, storing into @dst.
 *
 * Return: Zero on success, or negative error.
 */
int xe_bo_read(struct xe_bo *bo, u64 offset, void *dst, int size)
{
	int ret;

	ret = ttm_bo_access(&bo->ttm, offset, dst, size, 0);
	if (ret >= 0 && ret != size)
		ret = -EIO;
	else if (ret == size)
		ret = 0;

	return ret;
}

static const struct vm_operations_struct xe_gem_vm_ops = {
	.fault = xe_gem_fault,
	.open = ttm_bo_vm_open,
	.close = ttm_bo_vm_close,
	.access = xe_bo_vm_access,
};

static const struct drm_gem_object_funcs xe_gem_object_funcs = {
	.free = xe_gem_object_free,
	.close = xe_gem_object_close,
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

struct xe_bo *___xe_bo_create_locked(struct xe_device *xe, struct xe_bo *bo,
				     struct xe_tile *tile, struct dma_resv *resv,
				     struct ttm_lru_bulk_move *bulk, size_t size,
				     u16 cpu_caching, enum ttm_bo_type type,
				     u32 flags)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
		.gfp_retry_mayfail = true,
	};
	struct ttm_placement *placement;
	uint32_t alignment;
	size_t aligned_size;
	int err;

	/* Only kernel objects should set GT */
	xe_assert(xe, !tile || type == ttm_bo_type_kernel);

	if (XE_WARN_ON(!size)) {
		xe_bo_free(bo);
		return ERR_PTR(-EINVAL);
	}

	/* XE_BO_FLAG_GGTTx requires XE_BO_FLAG_GGTT also be set */
	if ((flags & XE_BO_FLAG_GGTT_ALL) && !(flags & XE_BO_FLAG_GGTT))
		return ERR_PTR(-EINVAL);

	if (flags & (XE_BO_FLAG_VRAM_MASK | XE_BO_FLAG_STOLEN) &&
	    !(flags & XE_BO_FLAG_IGNORE_MIN_PAGE_SIZE) &&
	    ((xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K) ||
	     (flags & (XE_BO_FLAG_NEEDS_64K | XE_BO_FLAG_NEEDS_2M)))) {
		size_t align = flags & XE_BO_FLAG_NEEDS_2M ? SZ_2M : SZ_64K;

		aligned_size = ALIGN(size, align);
		if (type != ttm_bo_type_device)
			size = ALIGN(size, align);
		flags |= XE_BO_FLAG_INTERNAL_64K;
		alignment = align >> PAGE_SHIFT;
	} else {
		aligned_size = ALIGN(size, SZ_4K);
		flags &= ~XE_BO_FLAG_INTERNAL_64K;
		alignment = SZ_4K >> PAGE_SHIFT;
	}

	if (type == ttm_bo_type_device && aligned_size != size)
		return ERR_PTR(-EINVAL);

	if (!bo) {
		bo = xe_bo_alloc();
		if (IS_ERR(bo))
			return bo;
	}

	bo->ccs_cleared = false;
	bo->tile = tile;
	bo->size = size;
	bo->flags = flags;
	bo->cpu_caching = cpu_caching;
	bo->ttm.base.funcs = &xe_gem_object_funcs;
	bo->ttm.priority = XE_BO_PRIORITY_NORMAL;
	INIT_LIST_HEAD(&bo->pinned_link);
#ifdef CONFIG_PROC_FS
	INIT_LIST_HEAD(&bo->client_link);
#endif
	INIT_LIST_HEAD(&bo->vram_userfault_link);

	drm_gem_private_object_init(&xe->drm, &bo->ttm.base, size);

	if (resv) {
		ctx.allow_res_evict = !(flags & XE_BO_FLAG_NO_RESV_EVICT);
		ctx.resv = resv;
	}

	if (!(flags & XE_BO_FLAG_FIXED_PLACEMENT)) {
		err = __xe_bo_placement_for_flags(xe, bo, bo->flags);
		if (WARN_ON(err)) {
			xe_ttm_bo_destroy(&bo->ttm);
			return ERR_PTR(err);
		}
	}

	/* Defer populating type_sg bos */
	placement = (type == ttm_bo_type_sg ||
		     bo->flags & XE_BO_FLAG_DEFER_BACKING) ? &sys_placement :
		&bo->placement;
	err = ttm_bo_init_reserved(&xe->ttm, &bo->ttm, type,
				   placement, alignment,
				   &ctx, NULL, resv, xe_ttm_bo_destroy);
	if (err)
		return ERR_PTR(err);

	/*
	 * The VRAM pages underneath are potentially still being accessed by the
	 * GPU, as per async GPU clearing and async evictions. However TTM makes
	 * sure to add any corresponding move/clear fences into the objects
	 * dma-resv using the DMA_RESV_USAGE_KERNEL slot.
	 *
	 * For KMD internal buffers we don't care about GPU clearing, however we
	 * still need to handle async evictions, where the VRAM is still being
	 * accessed by the GPU. Most internal callers are not expecting this,
	 * since they are missing the required synchronisation before accessing
	 * the memory. To keep things simple just sync wait any kernel fences
	 * here, if the buffer is designated KMD internal.
	 *
	 * For normal userspace objects we should already have the required
	 * pipelining or sync waiting elsewhere, since we already have to deal
	 * with things like async GPU clearing.
	 */
	if (type == ttm_bo_type_kernel) {
		long timeout = dma_resv_wait_timeout(bo->ttm.base.resv,
						     DMA_RESV_USAGE_KERNEL,
						     ctx.interruptible,
						     MAX_SCHEDULE_TIMEOUT);

		if (timeout < 0) {
			if (!resv)
				dma_resv_unlock(bo->ttm.base.resv);
			xe_bo_put(bo);
			return ERR_PTR(timeout);
		}
	}

	bo->created = true;
	if (bulk)
		ttm_bo_set_bulk_move(&bo->ttm, bulk);
	else
		ttm_bo_move_to_lru_tail_unlocked(&bo->ttm);

	return bo;
}

static int __xe_bo_fixed_placement(struct xe_device *xe,
				   struct xe_bo *bo,
				   u32 flags,
				   u64 start, u64 end, u64 size)
{
	struct ttm_place *place = bo->placements;

	if (flags & (XE_BO_FLAG_USER | XE_BO_FLAG_SYSTEM))
		return -EINVAL;

	place->flags = TTM_PL_FLAG_CONTIGUOUS;
	place->fpfn = start >> PAGE_SHIFT;
	place->lpfn = end >> PAGE_SHIFT;

	switch (flags & (XE_BO_FLAG_STOLEN | XE_BO_FLAG_VRAM_MASK)) {
	case XE_BO_FLAG_VRAM0:
		place->mem_type = XE_PL_VRAM0;
		break;
	case XE_BO_FLAG_VRAM1:
		place->mem_type = XE_PL_VRAM1;
		break;
	case XE_BO_FLAG_STOLEN:
		place->mem_type = XE_PL_STOLEN;
		break;

	default:
		/* 0 or multiple of the above set */
		return -EINVAL;
	}

	bo->placement = (struct ttm_placement) {
		.num_placement = 1,
		.placement = place,
	};

	return 0;
}

static struct xe_bo *
__xe_bo_create_locked(struct xe_device *xe,
		      struct xe_tile *tile, struct xe_vm *vm,
		      size_t size, u64 start, u64 end,
		      u16 cpu_caching, enum ttm_bo_type type, u32 flags,
		      u64 alignment)
{
	struct xe_bo *bo = NULL;
	int err;

	if (vm)
		xe_vm_assert_held(vm);

	if (start || end != ~0ULL) {
		bo = xe_bo_alloc();
		if (IS_ERR(bo))
			return bo;

		flags |= XE_BO_FLAG_FIXED_PLACEMENT;
		err = __xe_bo_fixed_placement(xe, bo, flags, start, end, size);
		if (err) {
			xe_bo_free(bo);
			return ERR_PTR(err);
		}
	}

	bo = ___xe_bo_create_locked(xe, bo, tile, vm ? xe_vm_resv(vm) : NULL,
				    vm && !xe_vm_in_fault_mode(vm) &&
				    flags & XE_BO_FLAG_USER ?
				    &vm->lru_bulk_move : NULL, size,
				    cpu_caching, type, flags);
	if (IS_ERR(bo))
		return bo;

	bo->min_align = alignment;

	/*
	 * Note that instead of taking a reference no the drm_gpuvm_resv_bo(),
	 * to ensure the shared resv doesn't disappear under the bo, the bo
	 * will keep a reference to the vm, and avoid circular references
	 * by having all the vm's bo refereferences released at vm close
	 * time.
	 */
	if (vm && xe_bo_is_user(bo))
		xe_vm_get(vm);
	bo->vm = vm;

	if (bo->flags & XE_BO_FLAG_GGTT) {
		struct xe_tile *t;
		u8 id;

		if (!(bo->flags & XE_BO_FLAG_GGTT_ALL)) {
			if (!tile && flags & XE_BO_FLAG_STOLEN)
				tile = xe_device_get_root_tile(xe);

			xe_assert(xe, tile);
		}

		for_each_tile(t, xe, id) {
			if (t != tile && !(bo->flags & XE_BO_FLAG_GGTTx(t)))
				continue;

			if (flags & XE_BO_FLAG_FIXED_PLACEMENT) {
				err = xe_ggtt_insert_bo_at(t->mem.ggtt, bo,
							   start + bo->size, U64_MAX);
			} else {
				err = xe_ggtt_insert_bo(t->mem.ggtt, bo);
			}
			if (err)
				goto err_unlock_put_bo;
		}
	}

	return bo;

err_unlock_put_bo:
	__xe_bo_unset_bulk_move(bo);
	xe_bo_unlock_vm_held(bo);
	xe_bo_put(bo);
	return ERR_PTR(err);
}

struct xe_bo *
xe_bo_create_locked_range(struct xe_device *xe,
			  struct xe_tile *tile, struct xe_vm *vm,
			  size_t size, u64 start, u64 end,
			  enum ttm_bo_type type, u32 flags, u64 alignment)
{
	return __xe_bo_create_locked(xe, tile, vm, size, start, end, 0, type,
				     flags, alignment);
}

struct xe_bo *xe_bo_create_locked(struct xe_device *xe, struct xe_tile *tile,
				  struct xe_vm *vm, size_t size,
				  enum ttm_bo_type type, u32 flags)
{
	return __xe_bo_create_locked(xe, tile, vm, size, 0, ~0ULL, 0, type,
				     flags, 0);
}

struct xe_bo *xe_bo_create_user(struct xe_device *xe, struct xe_tile *tile,
				struct xe_vm *vm, size_t size,
				u16 cpu_caching,
				u32 flags)
{
	struct xe_bo *bo = __xe_bo_create_locked(xe, tile, vm, size, 0, ~0ULL,
						 cpu_caching, ttm_bo_type_device,
						 flags | XE_BO_FLAG_USER, 0);
	if (!IS_ERR(bo))
		xe_bo_unlock_vm_held(bo);

	return bo;
}

struct xe_bo *xe_bo_create(struct xe_device *xe, struct xe_tile *tile,
			   struct xe_vm *vm, size_t size,
			   enum ttm_bo_type type, u32 flags)
{
	struct xe_bo *bo = xe_bo_create_locked(xe, tile, vm, size, type, flags);

	if (!IS_ERR(bo))
		xe_bo_unlock_vm_held(bo);

	return bo;
}

struct xe_bo *xe_bo_create_pin_map_at(struct xe_device *xe, struct xe_tile *tile,
				      struct xe_vm *vm,
				      size_t size, u64 offset,
				      enum ttm_bo_type type, u32 flags)
{
	return xe_bo_create_pin_map_at_aligned(xe, tile, vm, size, offset,
					       type, flags, 0);
}

struct xe_bo *xe_bo_create_pin_map_at_aligned(struct xe_device *xe,
					      struct xe_tile *tile,
					      struct xe_vm *vm,
					      size_t size, u64 offset,
					      enum ttm_bo_type type, u32 flags,
					      u64 alignment)
{
	struct xe_bo *bo;
	int err;
	u64 start = offset == ~0ull ? 0 : offset;
	u64 end = offset == ~0ull ? offset : start + size;

	if (flags & XE_BO_FLAG_STOLEN &&
	    xe_ttm_stolen_cpu_access_needs_ggtt(xe))
		flags |= XE_BO_FLAG_GGTT;

	bo = xe_bo_create_locked_range(xe, tile, vm, size, start, end, type,
				       flags | XE_BO_FLAG_NEEDS_CPU_ACCESS,
				       alignment);
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

struct xe_bo *xe_bo_create_pin_map(struct xe_device *xe, struct xe_tile *tile,
				   struct xe_vm *vm, size_t size,
				   enum ttm_bo_type type, u32 flags)
{
	return xe_bo_create_pin_map_at(xe, tile, vm, size, ~0ull, type, flags);
}

struct xe_bo *xe_bo_create_from_data(struct xe_device *xe, struct xe_tile *tile,
				     const void *data, size_t size,
				     enum ttm_bo_type type, u32 flags)
{
	struct xe_bo *bo = xe_bo_create_pin_map(xe, tile, NULL,
						ALIGN(size, PAGE_SIZE),
						type, flags);
	if (IS_ERR(bo))
		return bo;

	xe_map_memcpy_to(xe, &bo->vmap, 0, data, size);

	return bo;
}

static void __xe_bo_unpin_map_no_vm(void *arg)
{
	xe_bo_unpin_map_no_vm(arg);
}

struct xe_bo *xe_managed_bo_create_pin_map(struct xe_device *xe, struct xe_tile *tile,
					   size_t size, u32 flags)
{
	struct xe_bo *bo;
	int ret;

	bo = xe_bo_create_pin_map(xe, tile, NULL, size, ttm_bo_type_kernel, flags);
	if (IS_ERR(bo))
		return bo;

	ret = devm_add_action_or_reset(xe->drm.dev, __xe_bo_unpin_map_no_vm, bo);
	if (ret)
		return ERR_PTR(ret);

	return bo;
}

struct xe_bo *xe_managed_bo_create_from_data(struct xe_device *xe, struct xe_tile *tile,
					     const void *data, size_t size, u32 flags)
{
	struct xe_bo *bo = xe_managed_bo_create_pin_map(xe, tile, ALIGN(size, PAGE_SIZE), flags);

	if (IS_ERR(bo))
		return bo;

	xe_map_memcpy_to(xe, &bo->vmap, 0, data, size);

	return bo;
}

/**
 * xe_managed_bo_reinit_in_vram
 * @xe: xe device
 * @tile: Tile where the new buffer will be created
 * @src: Managed buffer object allocated in system memory
 *
 * Replace a managed src buffer object allocated in system memory with a new
 * one allocated in vram, copying the data between them.
 * Buffer object in VRAM is not going to have the same GGTT address, the caller
 * is responsible for making sure that any old references to it are updated.
 *
 * Returns 0 for success, negative error code otherwise.
 */
int xe_managed_bo_reinit_in_vram(struct xe_device *xe, struct xe_tile *tile, struct xe_bo **src)
{
	struct xe_bo *bo;
	u32 dst_flags = XE_BO_FLAG_VRAM_IF_DGFX(tile) | XE_BO_FLAG_GGTT;

	dst_flags |= (*src)->flags & XE_BO_FLAG_GGTT_INVALIDATE;

	xe_assert(xe, IS_DGFX(xe));
	xe_assert(xe, !(*src)->vmap.is_iomem);

	bo = xe_managed_bo_create_from_data(xe, tile, (*src)->vmap.vaddr,
					    (*src)->size, dst_flags);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	devm_release_action(xe->drm.dev, __xe_bo_unpin_map_no_vm, *src);
	*src = bo;

	return 0;
}

/*
 * XXX: This is in the VM bind data path, likely should calculate this once and
 * store, with a recalculation if the BO is moved.
 */
uint64_t vram_region_gpu_offset(struct ttm_resource *res)
{
	struct xe_device *xe = ttm_to_xe_device(res->bo->bdev);

	if (res->mem_type == XE_PL_STOLEN)
		return xe_ttm_stolen_gpu_offset(xe);

	return res_to_mem_region(res)->dpa_base;
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

	xe_assert(xe, !bo->vm);
	xe_assert(xe, xe_bo_is_user(bo));

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
	struct ttm_place *place = &bo->placements[0];
	struct xe_device *xe = xe_bo_device(bo);
	int err;

	/* We currently don't expect user BO to be pinned */
	xe_assert(xe, !xe_bo_is_user(bo));

	/* Pinned object must be in GGTT or have pinned flag */
	xe_assert(xe, bo->flags & (XE_BO_FLAG_PINNED |
				   XE_BO_FLAG_GGTT));

	/*
	 * No reason we can't support pinning imported dma-bufs we just don't
	 * expect to pin an imported dma-buf.
	 */
	xe_assert(xe, !bo->ttm.base.import_attach);

	/* We only expect at most 1 pin */
	xe_assert(xe, !xe_bo_is_pinned(bo));

	err = xe_bo_validate(bo, NULL, false);
	if (err)
		return err;

	/*
	 * For pinned objects in on DGFX, which are also in vram, we expect
	 * these to be in contiguous VRAM memory. Required eviction / restore
	 * during suspend / resume (force restore to same physical address).
	 */
	if (IS_DGFX(xe) && !(IS_ENABLED(CONFIG_DRM_XE_DEBUG) &&
	    bo->flags & XE_BO_FLAG_INTERNAL_TEST)) {
		if (mem_type_is_vram(place->mem_type)) {
			xe_assert(xe, place->flags & TTM_PL_FLAG_CONTIGUOUS);

			place->fpfn = (xe_bo_addr(bo, 0, PAGE_SIZE) -
				       vram_region_gpu_offset(bo->ttm.resource)) >> PAGE_SHIFT;
			place->lpfn = place->fpfn + (bo->size >> PAGE_SHIFT);
		}
	}

	if (mem_type_is_vram(place->mem_type) || bo->flags & XE_BO_FLAG_GGTT) {
		spin_lock(&xe->pinned.lock);
		list_add_tail(&bo->pinned_link, &xe->pinned.kernel_bo_present);
		spin_unlock(&xe->pinned.lock);
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

	xe_assert(xe, !bo->vm);
	xe_assert(xe, xe_bo_is_pinned(bo));
	xe_assert(xe, xe_bo_is_user(bo));

	spin_lock(&xe->pinned.lock);
	if (bo->ttm.pin_count == 1 && !list_empty(&bo->pinned_link))
		list_del_init(&bo->pinned_link);
	spin_unlock(&xe->pinned.lock);

	ttm_bo_unpin(&bo->ttm);

	/*
	 * FIXME: If we always use the reserve / unreserve functions for locking
	 * we do not need this.
	 */
	ttm_bo_move_to_lru_tail_unlocked(&bo->ttm);
}

void xe_bo_unpin(struct xe_bo *bo)
{
	struct ttm_place *place = &bo->placements[0];
	struct xe_device *xe = xe_bo_device(bo);

	xe_assert(xe, !bo->ttm.base.import_attach);
	xe_assert(xe, xe_bo_is_pinned(bo));

	if (mem_type_is_vram(place->mem_type) || bo->flags & XE_BO_FLAG_GGTT) {
		spin_lock(&xe->pinned.lock);
		xe_assert(xe, !list_empty(&bo->pinned_link));
		list_del_init(&bo->pinned_link);
		spin_unlock(&xe->pinned.lock);
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
		.gfp_retry_mayfail = true,
	};

	if (vm) {
		lockdep_assert_held(&vm->lock);
		xe_vm_assert_held(vm);

		ctx.allow_res_evict = allow_res_evict;
		ctx.resv = xe_vm_resv(vm);
	}

	trace_xe_bo_validate(bo);
	return ttm_bo_validate(&bo->ttm, &bo->placement, &ctx);
}

bool xe_bo_is_xe_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &xe_ttm_bo_destroy)
		return true;

	return false;
}

/*
 * Resolve a BO address. There is no assert to check if the proper lock is held
 * so it should only be used in cases where it is not fatal to get the wrong
 * address, such as printing debug information, but not in cases where memory is
 * written based on this result.
 */
dma_addr_t __xe_bo_addr(struct xe_bo *bo, u64 offset, size_t page_size)
{
	struct xe_device *xe = xe_bo_device(bo);
	struct xe_res_cursor cur;
	u64 page;

	xe_assert(xe, page_size <= PAGE_SIZE);
	page = offset >> PAGE_SHIFT;
	offset &= (PAGE_SIZE - 1);

	if (!xe_bo_is_vram(bo) && !xe_bo_is_stolen(bo)) {
		xe_assert(xe, bo->ttm.ttm);

		xe_res_first_sg(xe_bo_sg(bo), page << PAGE_SHIFT,
				page_size, &cur);
		return xe_res_dma(&cur) + offset;
	} else {
		struct xe_res_cursor cur;

		xe_res_first(bo->ttm.resource, page << PAGE_SHIFT,
			     page_size, &cur);
		return cur.start + offset + vram_region_gpu_offset(bo->ttm.resource);
	}
}

dma_addr_t xe_bo_addr(struct xe_bo *bo, u64 offset, size_t page_size)
{
	if (!READ_ONCE(bo->ttm.pin_count))
		xe_bo_assert_held(bo);
	return __xe_bo_addr(bo, offset, page_size);
}

int xe_bo_vmap(struct xe_bo *bo)
{
	struct xe_device *xe = ttm_to_xe_device(bo->ttm.bdev);
	void *virtual;
	bool is_iomem;
	int ret;

	xe_bo_assert_held(bo);

	if (drm_WARN_ON(&xe->drm, !(bo->flags & XE_BO_FLAG_NEEDS_CPU_ACCESS) ||
			!force_contiguous(bo->flags)))
		return -EINVAL;

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
	struct xe_vm *vm = NULL;
	struct xe_bo *bo;
	unsigned int bo_flags;
	u32 handle;
	int err;

	if (XE_IOCTL_DBG(xe, args->extensions) ||
	    XE_IOCTL_DBG(xe, args->pad[0] || args->pad[1] || args->pad[2]) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	/* at least one valid memory placement must be specified */
	if (XE_IOCTL_DBG(xe, (args->placement & ~xe->info.mem_region_mask) ||
			 !args->placement))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags &
			 ~(DRM_XE_GEM_CREATE_FLAG_DEFER_BACKING |
			   DRM_XE_GEM_CREATE_FLAG_SCANOUT |
			   DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM)))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->handle))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, !args->size))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->size > SIZE_MAX))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->size & ~PAGE_MASK))
		return -EINVAL;

	bo_flags = 0;
	if (args->flags & DRM_XE_GEM_CREATE_FLAG_DEFER_BACKING)
		bo_flags |= XE_BO_FLAG_DEFER_BACKING;

	if (args->flags & DRM_XE_GEM_CREATE_FLAG_SCANOUT)
		bo_flags |= XE_BO_FLAG_SCANOUT;

	bo_flags |= args->placement << (ffs(XE_BO_FLAG_SYSTEM) - 1);

	/* CCS formats need physical placement at a 64K alignment in VRAM. */
	if ((bo_flags & XE_BO_FLAG_VRAM_MASK) &&
	    (bo_flags & XE_BO_FLAG_SCANOUT) &&
	    !(xe->info.vram_flags & XE_VRAM_FLAGS_NEED64K) &&
	    IS_ALIGNED(args->size, SZ_64K))
		bo_flags |= XE_BO_FLAG_NEEDS_64K;

	if (args->flags & DRM_XE_GEM_CREATE_FLAG_NEEDS_VISIBLE_VRAM) {
		if (XE_IOCTL_DBG(xe, !(bo_flags & XE_BO_FLAG_VRAM_MASK)))
			return -EINVAL;

		bo_flags |= XE_BO_FLAG_NEEDS_CPU_ACCESS;
	}

	if (XE_IOCTL_DBG(xe, !args->cpu_caching ||
			 args->cpu_caching > DRM_XE_GEM_CPU_CACHING_WC))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, bo_flags & XE_BO_FLAG_VRAM_MASK &&
			 args->cpu_caching != DRM_XE_GEM_CPU_CACHING_WC))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, bo_flags & XE_BO_FLAG_SCANOUT &&
			 args->cpu_caching == DRM_XE_GEM_CPU_CACHING_WB))
		return -EINVAL;

	if (args->vm_id) {
		vm = xe_vm_lookup(xef, args->vm_id);
		if (XE_IOCTL_DBG(xe, !vm))
			return -ENOENT;
		err = xe_vm_lock(vm, true);
		if (err)
			goto out_vm;
	}

	bo = xe_bo_create_user(xe, NULL, vm, args->size, args->cpu_caching,
			       bo_flags);

	if (vm)
		xe_vm_unlock(vm);

	if (IS_ERR(bo)) {
		err = PTR_ERR(bo);
		goto out_vm;
	}

	err = drm_gem_handle_create(file, &bo->ttm.base, &handle);
	if (err)
		goto out_bulk;

	args->handle = handle;
	goto out_put;

out_bulk:
	if (vm && !xe_vm_in_fault_mode(vm)) {
		xe_vm_lock(vm, false);
		__xe_bo_unset_bulk_move(bo);
		xe_vm_unlock(vm);
	}
out_put:
	xe_bo_put(bo);
out_vm:
	if (vm)
		xe_vm_put(vm);

	return err;
}

int xe_gem_mmap_offset_ioctl(struct drm_device *dev, void *data,
			     struct drm_file *file)
{
	struct xe_device *xe = to_xe_device(dev);
	struct drm_xe_gem_mmap_offset *args = data;
	struct drm_gem_object *gem_obj;

	if (XE_IOCTL_DBG(xe, args->extensions) ||
	    XE_IOCTL_DBG(xe, args->reserved[0] || args->reserved[1]))
		return -EINVAL;

	if (XE_IOCTL_DBG(xe, args->flags))
		return -EINVAL;

	gem_obj = drm_gem_object_lookup(file, args->handle);
	if (XE_IOCTL_DBG(xe, !gem_obj))
		return -ENOENT;

	/* The mmap offset was set up at BO allocation time. */
	args->offset = drm_vma_node_offset_addr(&gem_obj->vma_node);

	xe_bo_put(gem_to_xe_bo(gem_obj));
	return 0;
}

/**
 * xe_bo_lock() - Lock the buffer object's dma_resv object
 * @bo: The struct xe_bo whose lock is to be taken
 * @intr: Whether to perform any wait interruptible
 *
 * Locks the buffer object's dma_resv object. If the buffer object is
 * pointing to a shared dma_resv object, that shared lock is locked.
 *
 * Return: 0 on success, -EINTR if @intr is true and the wait for a
 * contended lock was interrupted. If @intr is set to false, the
 * function always returns 0.
 */
int xe_bo_lock(struct xe_bo *bo, bool intr)
{
	if (intr)
		return dma_resv_lock_interruptible(bo->ttm.base.resv, NULL);

	dma_resv_lock(bo->ttm.base.resv, NULL);

	return 0;
}

/**
 * xe_bo_unlock() - Unlock the buffer object's dma_resv object
 * @bo: The struct xe_bo whose lock is to be released.
 *
 * Unlock a buffer object lock that was locked by xe_bo_lock().
 */
void xe_bo_unlock(struct xe_bo *bo)
{
	dma_resv_unlock(bo->ttm.base.resv);
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
	struct xe_device *xe = ttm_to_xe_device(bo->ttm.bdev);
	struct ttm_operation_ctx ctx = {
		.interruptible = true,
		.no_wait_gpu = false,
		.gfp_retry_mayfail = true,
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
	placement.placement = &requested;

	/*
	 * Stolen needs to be handled like below VRAM handling if we ever need
	 * to support it.
	 */
	drm_WARN_ON(&xe->drm, mem_type == XE_PL_STOLEN);

	if (mem_type_is_vram(mem_type)) {
		u32 c = 0;

		add_vram(xe, bo, &requested, bo->flags, mem_type, &c);
	}

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
		.gfp_retry_mayfail = true,
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
 * Return: true if extra pages need to be allocated, false otherwise.
 */
bool xe_bo_needs_ccs_pages(struct xe_bo *bo)
{
	struct xe_device *xe = xe_bo_device(bo);

	if (GRAPHICS_VER(xe) >= 20 && IS_DGFX(xe))
		return false;

	if (!xe_device_has_flat_ccs(xe) || bo->ttm.type != ttm_bo_type_device)
		return false;

	/* On discrete GPUs, if the GPU can access this buffer from
	 * system memory (i.e., it allows XE_PL_TT placement), FlatCCS
	 * can't be used since there's no CCS storage associated with
	 * non-VRAM addresses.
	 */
	if (IS_DGFX(xe) && (bo->flags & XE_BO_FLAG_SYSTEM))
		return false;

	return true;
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

void xe_bo_put(struct xe_bo *bo)
{
	struct xe_tile *tile;
	u8 id;

	might_sleep();
	if (bo) {
#ifdef CONFIG_PROC_FS
		if (bo->client)
			might_lock(&bo->client->bos_lock);
#endif
		for_each_tile(tile, xe_bo_device(bo), id)
			if (bo->ggtt_node[id] && bo->ggtt_node[id]->ggtt)
				might_lock(&bo->ggtt_node[id]->ggtt->lock);
		drm_gem_object_put(&bo->ttm.base);
	}
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

	bo = xe_bo_create_user(xe, NULL, NULL, args->size,
			       DRM_XE_GEM_CPU_CACHING_WC,
			       XE_BO_FLAG_VRAM_IF_DGFX(xe_device_get_root_tile(xe)) |
			       XE_BO_FLAG_SCANOUT |
			       XE_BO_FLAG_NEEDS_CPU_ACCESS);
	if (IS_ERR(bo))
		return PTR_ERR(bo);

	err = drm_gem_handle_create(file_priv, &bo->ttm.base, &handle);
	/* drop reference from allocate - handle holds it now */
	drm_gem_object_put(&bo->ttm.base);
	if (!err)
		args->handle = handle;
	return err;
}

void xe_bo_runtime_pm_release_mmap_offset(struct xe_bo *bo)
{
	struct ttm_buffer_object *tbo = &bo->ttm;
	struct ttm_device *bdev = tbo->bdev;

	drm_vma_node_unmap(&tbo->base.vma_node, bdev->dev_mapping);

	list_del_init(&bo->vram_userfault_link);
}

#if IS_ENABLED(CONFIG_DRM_XE_KUNIT_TEST)
#include "tests/xe_bo.c"
#endif
