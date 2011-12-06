/**************************************************************************
 *
 * Copyright © 2009 VMware, Inc., Palo Alto, CA., USA
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "vmwgfx_drv.h"
#include "ttm/ttm_bo_driver.h"
#include "ttm/ttm_placement.h"
#include "ttm/ttm_page_alloc.h"

static uint32_t vram_placement_flags = TTM_PL_FLAG_VRAM |
	TTM_PL_FLAG_CACHED;

static uint32_t vram_ne_placement_flags = TTM_PL_FLAG_VRAM |
	TTM_PL_FLAG_CACHED |
	TTM_PL_FLAG_NO_EVICT;

static uint32_t sys_placement_flags = TTM_PL_FLAG_SYSTEM |
	TTM_PL_FLAG_CACHED;

static uint32_t gmr_placement_flags = VMW_PL_FLAG_GMR |
	TTM_PL_FLAG_CACHED;

static uint32_t gmr_ne_placement_flags = VMW_PL_FLAG_GMR |
	TTM_PL_FLAG_CACHED |
	TTM_PL_FLAG_NO_EVICT;

struct ttm_placement vmw_vram_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &vram_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &vram_placement_flags
};

static uint32_t vram_gmr_placement_flags[] = {
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED,
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
};

static uint32_t gmr_vram_placement_flags[] = {
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED,
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED
};

struct ttm_placement vmw_vram_gmr_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 2,
	.placement = vram_gmr_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &gmr_placement_flags
};

static uint32_t vram_gmr_ne_placement_flags[] = {
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT,
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED | TTM_PL_FLAG_NO_EVICT
};

struct ttm_placement vmw_vram_gmr_ne_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 2,
	.placement = vram_gmr_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &gmr_ne_placement_flags
};

struct ttm_placement vmw_vram_sys_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &vram_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_vram_ne_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &vram_ne_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &vram_ne_placement_flags
};

struct ttm_placement vmw_sys_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.placement = &sys_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

static uint32_t evictable_placement_flags[] = {
	TTM_PL_FLAG_SYSTEM | TTM_PL_FLAG_CACHED,
	TTM_PL_FLAG_VRAM | TTM_PL_FLAG_CACHED,
	VMW_PL_FLAG_GMR | TTM_PL_FLAG_CACHED
};

struct ttm_placement vmw_evictable_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 3,
	.placement = evictable_placement_flags,
	.num_busy_placement = 1,
	.busy_placement = &sys_placement_flags
};

struct ttm_placement vmw_srf_placement = {
	.fpfn = 0,
	.lpfn = 0,
	.num_placement = 1,
	.num_busy_placement = 2,
	.placement = &gmr_placement_flags,
	.busy_placement = gmr_vram_placement_flags
};

struct vmw_ttm_tt {
	struct ttm_tt ttm;
	struct vmw_private *dev_priv;
	int gmr_id;
};

static int vmw_ttm_bind(struct ttm_tt *ttm, struct ttm_mem_reg *bo_mem)
{
	struct vmw_ttm_tt *vmw_be = container_of(ttm, struct vmw_ttm_tt, ttm);

	vmw_be->gmr_id = bo_mem->start;

	return vmw_gmr_bind(vmw_be->dev_priv, ttm->pages,
			    ttm->num_pages, vmw_be->gmr_id);
}

static int vmw_ttm_unbind(struct ttm_tt *ttm)
{
	struct vmw_ttm_tt *vmw_be = container_of(ttm, struct vmw_ttm_tt, ttm);

	vmw_gmr_unbind(vmw_be->dev_priv, vmw_be->gmr_id);
	return 0;
}

static void vmw_ttm_destroy(struct ttm_tt *ttm)
{
	struct vmw_ttm_tt *vmw_be = container_of(ttm, struct vmw_ttm_tt, ttm);

	ttm_tt_fini(ttm);
	kfree(vmw_be);
}

static struct ttm_backend_func vmw_ttm_func = {
	.bind = vmw_ttm_bind,
	.unbind = vmw_ttm_unbind,
	.destroy = vmw_ttm_destroy,
};

struct ttm_tt *vmw_ttm_tt_create(struct ttm_bo_device *bdev,
				 unsigned long size, uint32_t page_flags,
				 struct page *dummy_read_page)
{
	struct vmw_ttm_tt *vmw_be;

	vmw_be = kmalloc(sizeof(*vmw_be), GFP_KERNEL);
	if (!vmw_be)
		return NULL;

	vmw_be->ttm.func = &vmw_ttm_func;
	vmw_be->dev_priv = container_of(bdev, struct vmw_private, bdev);

	if (ttm_tt_init(&vmw_be->ttm, bdev, size, page_flags, dummy_read_page)) {
		kfree(vmw_be);
		return NULL;
	}

	return &vmw_be->ttm;
}

int vmw_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

int vmw_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
		      struct ttm_mem_type_manager *man)
{
	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */

		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->func = &ttm_bo_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_FIXED | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case VMW_PL_GMR:
		/*
		 * "Guest Memory Regions" is an aperture like feature with
		 *  one slot per bo. There is an upper limit of the number of
		 *  slots as well as the bo size.
		 */
		man->func = &vmw_gmrid_manager_func;
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_CMA | TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_CACHED;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

void vmw_evict_flags(struct ttm_buffer_object *bo,
		     struct ttm_placement *placement)
{
	*placement = vmw_sys_placement;
}

/**
 * FIXME: Proper access checks on buffers.
 */

static int vmw_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	return 0;
}

static int vmw_ttm_io_mem_reserve(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
	struct ttm_mem_type_manager *man = &bdev->man[mem->mem_type];
	struct vmw_private *dev_priv = container_of(bdev, struct vmw_private, bdev);

	mem->bus.addr = NULL;
	mem->bus.is_iomem = false;
	mem->bus.offset = 0;
	mem->bus.size = mem->num_pages << PAGE_SHIFT;
	mem->bus.base = 0;
	if (!(man->flags & TTM_MEMTYPE_FLAG_MAPPABLE))
		return -EINVAL;
	switch (mem->mem_type) {
	case TTM_PL_SYSTEM:
	case VMW_PL_GMR:
		return 0;
	case TTM_PL_VRAM:
		mem->bus.offset = mem->start << PAGE_SHIFT;
		mem->bus.base = dev_priv->vram_start;
		mem->bus.is_iomem = true;
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

static void vmw_ttm_io_mem_free(struct ttm_bo_device *bdev, struct ttm_mem_reg *mem)
{
}

static int vmw_ttm_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	return 0;
}

/**
 * FIXME: We're using the old vmware polling method to sync.
 * Do this with fences instead.
 */

static void *vmw_sync_obj_ref(void *sync_obj)
{

	return (void *)
		vmw_fence_obj_reference((struct vmw_fence_obj *) sync_obj);
}

static void vmw_sync_obj_unref(void **sync_obj)
{
	vmw_fence_obj_unreference((struct vmw_fence_obj **) sync_obj);
}

static int vmw_sync_obj_flush(void *sync_obj, void *sync_arg)
{
	vmw_fence_obj_flush((struct vmw_fence_obj *) sync_obj);
	return 0;
}

static bool vmw_sync_obj_signaled(void *sync_obj, void *sync_arg)
{
	unsigned long flags = (unsigned long) sync_arg;
	return	vmw_fence_obj_signaled((struct vmw_fence_obj *) sync_obj,
				       (uint32_t) flags);

}

static int vmw_sync_obj_wait(void *sync_obj, void *sync_arg,
			     bool lazy, bool interruptible)
{
	unsigned long flags = (unsigned long) sync_arg;

	return vmw_fence_obj_wait((struct vmw_fence_obj *) sync_obj,
				  (uint32_t) flags,
				  lazy, interruptible,
				  VMW_FENCE_WAIT_TIMEOUT);
}

struct ttm_bo_driver vmw_bo_driver = {
	.ttm_tt_create = &vmw_ttm_tt_create,
	.ttm_tt_populate = &ttm_pool_populate,
	.ttm_tt_unpopulate = &ttm_pool_unpopulate,
	.invalidate_caches = vmw_invalidate_caches,
	.init_mem_type = vmw_init_mem_type,
	.evict_flags = vmw_evict_flags,
	.move = NULL,
	.verify_access = vmw_verify_access,
	.sync_obj_signaled = vmw_sync_obj_signaled,
	.sync_obj_wait = vmw_sync_obj_wait,
	.sync_obj_flush = vmw_sync_obj_flush,
	.sync_obj_unref = vmw_sync_obj_unref,
	.sync_obj_ref = vmw_sync_obj_ref,
	.move_notify = NULL,
	.swap_notify = NULL,
	.fault_reserve_notify = &vmw_ttm_fault_reserve_notify,
	.io_mem_reserve = &vmw_ttm_io_mem_reserve,
	.io_mem_free = &vmw_ttm_io_mem_free,
};
