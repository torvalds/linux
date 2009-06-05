/*
 * Copyright 2009 Jerome Glisse.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/*
 * Authors:
 *    Jerome Glisse <glisse@freedesktop.org>
 *    Thomas Hellstrom <thomas-at-tungstengraphics-dot-com>
 *    Dave Airlie
 */
#include <ttm/ttm_bo_api.h>
#include <ttm/ttm_bo_driver.h>
#include <ttm/ttm_placement.h>
#include <ttm/ttm_module.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon_reg.h"
#include "radeon.h"

#define DRM_FILE_PAGE_OFFSET (0x100000000ULL >> PAGE_SHIFT)

static struct radeon_device *radeon_get_rdev(struct ttm_bo_device *bdev)
{
	struct radeon_mman *mman;
	struct radeon_device *rdev;

	mman = container_of(bdev, struct radeon_mman, bdev);
	rdev = container_of(mman, struct radeon_device, mman);
	return rdev;
}


/*
 * Global memory.
 */
static int radeon_ttm_mem_global_init(struct ttm_global_reference *ref)
{
	return ttm_mem_global_init(ref->object);
}

static void radeon_ttm_mem_global_release(struct ttm_global_reference *ref)
{
	ttm_mem_global_release(ref->object);
}

static int radeon_ttm_global_init(struct radeon_device *rdev)
{
	struct ttm_global_reference *global_ref;
	int r;

	rdev->mman.mem_global_referenced = false;
	global_ref = &rdev->mman.mem_global_ref;
	global_ref->global_type = TTM_GLOBAL_TTM_MEM;
	global_ref->size = sizeof(struct ttm_mem_global);
	global_ref->init = &radeon_ttm_mem_global_init;
	global_ref->release = &radeon_ttm_mem_global_release;
	r = ttm_global_item_ref(global_ref);
	if (r != 0) {
		DRM_ERROR("Failed referencing a global TTM memory object.\n");
		return r;
	}
	rdev->mman.mem_global_referenced = true;
	return 0;
}

static void radeon_ttm_global_fini(struct radeon_device *rdev)
{
	if (rdev->mman.mem_global_referenced) {
		ttm_global_item_unref(&rdev->mman.mem_global_ref);
		rdev->mman.mem_global_referenced = false;
	}
}

struct ttm_backend *radeon_ttm_backend_create(struct radeon_device *rdev);

static struct ttm_backend*
radeon_create_ttm_backend_entry(struct ttm_bo_device *bdev)
{
	struct radeon_device *rdev;

	rdev = radeon_get_rdev(bdev);
#if __OS_HAS_AGP
	if (rdev->flags & RADEON_IS_AGP) {
		return ttm_agp_backend_init(bdev, rdev->ddev->agp->bridge);
	} else
#endif
	{
		return radeon_ttm_backend_create(rdev);
	}
}

static int radeon_invalidate_caches(struct ttm_bo_device *bdev, uint32_t flags)
{
	return 0;
}

static int radeon_init_mem_type(struct ttm_bo_device *bdev, uint32_t type,
				struct ttm_mem_type_manager *man)
{
	struct radeon_device *rdev;

	rdev = radeon_get_rdev(bdev);

	switch (type) {
	case TTM_PL_SYSTEM:
		/* System memory */
		man->flags = TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
		break;
	case TTM_PL_TT:
		man->gpu_offset = 0;
		man->available_caching = TTM_PL_MASK_CACHING;
		man->default_caching = TTM_PL_FLAG_CACHED;
#if __OS_HAS_AGP
		if (rdev->flags & RADEON_IS_AGP) {
			if (!(drm_core_has_AGP(rdev->ddev) && rdev->ddev->agp)) {
				DRM_ERROR("AGP is not enabled for memory type %u\n",
					  (unsigned)type);
				return -EINVAL;
			}
			man->io_offset = rdev->mc.agp_base;
			man->io_size = rdev->mc.gtt_size;
			man->io_addr = NULL;
			man->flags = TTM_MEMTYPE_FLAG_NEEDS_IOREMAP |
				     TTM_MEMTYPE_FLAG_MAPPABLE;
			man->available_caching = TTM_PL_FLAG_UNCACHED |
						 TTM_PL_FLAG_WC;
			man->default_caching = TTM_PL_FLAG_WC;
		} else
#endif
		{
			man->io_offset = 0;
			man->io_size = 0;
			man->io_addr = NULL;
			man->flags = TTM_MEMTYPE_FLAG_MAPPABLE |
				     TTM_MEMTYPE_FLAG_CMA;
		}
		break;
	case TTM_PL_VRAM:
		/* "On-card" video ram */
		man->gpu_offset = 0;
		man->flags = TTM_MEMTYPE_FLAG_FIXED |
			     TTM_MEMTYPE_FLAG_NEEDS_IOREMAP |
			     TTM_MEMTYPE_FLAG_MAPPABLE;
		man->available_caching = TTM_PL_FLAG_UNCACHED | TTM_PL_FLAG_WC;
		man->default_caching = TTM_PL_FLAG_WC;
		man->io_addr = NULL;
		man->io_offset = rdev->mc.aper_base;
		man->io_size = rdev->mc.aper_size;
		break;
	default:
		DRM_ERROR("Unsupported memory type %u\n", (unsigned)type);
		return -EINVAL;
	}
	return 0;
}

static uint32_t radeon_evict_flags(struct ttm_buffer_object *bo)
{
	uint32_t cur_placement = bo->mem.placement & ~TTM_PL_MASK_MEMTYPE;

	switch (bo->mem.mem_type) {
	default:
		return (cur_placement & ~TTM_PL_MASK_CACHING) |
			TTM_PL_FLAG_SYSTEM |
			TTM_PL_FLAG_CACHED;
	}
}

static int radeon_verify_access(struct ttm_buffer_object *bo, struct file *filp)
{
	return 0;
}

static void radeon_move_null(struct ttm_buffer_object *bo,
			     struct ttm_mem_reg *new_mem)
{
	struct ttm_mem_reg *old_mem = &bo->mem;

	BUG_ON(old_mem->mm_node != NULL);
	*old_mem = *new_mem;
	new_mem->mm_node = NULL;
}

static int radeon_move_blit(struct ttm_buffer_object *bo,
			    bool evict, int no_wait,
			    struct ttm_mem_reg *new_mem,
			    struct ttm_mem_reg *old_mem)
{
	struct radeon_device *rdev;
	uint64_t old_start, new_start;
	struct radeon_fence *fence;
	int r;

	rdev = radeon_get_rdev(bo->bdev);
	r = radeon_fence_create(rdev, &fence);
	if (unlikely(r)) {
		return r;
	}
	old_start = old_mem->mm_node->start << PAGE_SHIFT;
	new_start = new_mem->mm_node->start << PAGE_SHIFT;

	switch (old_mem->mem_type) {
	case TTM_PL_VRAM:
		old_start += rdev->mc.vram_location;
		break;
	case TTM_PL_TT:
		old_start += rdev->mc.gtt_location;
		break;
	default:
		DRM_ERROR("Unknown placement %d\n", old_mem->mem_type);
		return -EINVAL;
	}
	switch (new_mem->mem_type) {
	case TTM_PL_VRAM:
		new_start += rdev->mc.vram_location;
		break;
	case TTM_PL_TT:
		new_start += rdev->mc.gtt_location;
		break;
	default:
		DRM_ERROR("Unknown placement %d\n", old_mem->mem_type);
		return -EINVAL;
	}
	if (!rdev->cp.ready) {
		DRM_ERROR("Trying to move memory with CP turned off.\n");
		return -EINVAL;
	}
	r = radeon_copy(rdev, old_start, new_start, new_mem->num_pages, fence);
	/* FIXME: handle copy error */
	r = ttm_bo_move_accel_cleanup(bo, (void *)fence, NULL,
				      evict, no_wait, new_mem);
	radeon_fence_unref(&fence);
	return r;
}

static int radeon_move_vram_ram(struct ttm_buffer_object *bo,
				bool evict, bool interruptible, bool no_wait,
				struct ttm_mem_reg *new_mem)
{
	struct radeon_device *rdev;
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct ttm_mem_reg tmp_mem;
	uint32_t proposed_placement;
	int r;

	rdev = radeon_get_rdev(bo->bdev);
	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;
	proposed_placement = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING;
	r = ttm_bo_mem_space(bo, proposed_placement, &tmp_mem,
			     interruptible, no_wait);
	if (unlikely(r)) {
		return r;
	}
	r = ttm_tt_bind(bo->ttm, &tmp_mem);
	if (unlikely(r)) {
		goto out_cleanup;
	}
	r = radeon_move_blit(bo, true, no_wait, &tmp_mem, old_mem);
	if (unlikely(r)) {
		goto out_cleanup;
	}
	r = ttm_bo_move_ttm(bo, true, no_wait, new_mem);
out_cleanup:
	if (tmp_mem.mm_node) {
		spin_lock(&rdev->mman.bdev.lru_lock);
		drm_mm_put_block(tmp_mem.mm_node);
		spin_unlock(&rdev->mman.bdev.lru_lock);
		return r;
	}
	return r;
}

static int radeon_move_ram_vram(struct ttm_buffer_object *bo,
				bool evict, bool interruptible, bool no_wait,
				struct ttm_mem_reg *new_mem)
{
	struct radeon_device *rdev;
	struct ttm_mem_reg *old_mem = &bo->mem;
	struct ttm_mem_reg tmp_mem;
	uint32_t proposed_flags;
	int r;

	rdev = radeon_get_rdev(bo->bdev);
	tmp_mem = *new_mem;
	tmp_mem.mm_node = NULL;
	proposed_flags = TTM_PL_FLAG_TT | TTM_PL_MASK_CACHING;
	r = ttm_bo_mem_space(bo, proposed_flags, &tmp_mem,
			     interruptible, no_wait);
	if (unlikely(r)) {
		return r;
	}
	r = ttm_bo_move_ttm(bo, true, no_wait, &tmp_mem);
	if (unlikely(r)) {
		goto out_cleanup;
	}
	r = radeon_move_blit(bo, true, no_wait, new_mem, old_mem);
	if (unlikely(r)) {
		goto out_cleanup;
	}
out_cleanup:
	if (tmp_mem.mm_node) {
		spin_lock(&rdev->mman.bdev.lru_lock);
		drm_mm_put_block(tmp_mem.mm_node);
		spin_unlock(&rdev->mman.bdev.lru_lock);
		return r;
	}
	return r;
}

static int radeon_bo_move(struct ttm_buffer_object *bo,
			  bool evict, bool interruptible, bool no_wait,
			  struct ttm_mem_reg *new_mem)
{
	struct radeon_device *rdev;
	struct ttm_mem_reg *old_mem = &bo->mem;
	int r;

	rdev = radeon_get_rdev(bo->bdev);
	if (old_mem->mem_type == TTM_PL_SYSTEM && bo->ttm == NULL) {
		radeon_move_null(bo, new_mem);
		return 0;
	}
	if ((old_mem->mem_type == TTM_PL_TT &&
	     new_mem->mem_type == TTM_PL_SYSTEM) ||
	    (old_mem->mem_type == TTM_PL_SYSTEM &&
	     new_mem->mem_type == TTM_PL_TT)) {
		/* bind is enought */
		radeon_move_null(bo, new_mem);
		return 0;
	}
	if (!rdev->cp.ready) {
		/* use memcpy */
		DRM_ERROR("CP is not ready use memcpy.\n");
		return ttm_bo_move_memcpy(bo, evict, no_wait, new_mem);
	}

	if (old_mem->mem_type == TTM_PL_VRAM &&
	    new_mem->mem_type == TTM_PL_SYSTEM) {
		return radeon_move_vram_ram(bo, evict, interruptible,
					    no_wait, new_mem);
	} else if (old_mem->mem_type == TTM_PL_SYSTEM &&
		   new_mem->mem_type == TTM_PL_VRAM) {
		return radeon_move_ram_vram(bo, evict, interruptible,
					    no_wait, new_mem);
	} else {
		r = radeon_move_blit(bo, evict, no_wait, new_mem, old_mem);
		if (unlikely(r)) {
			return r;
		}
	}
	return r;
}

const uint32_t radeon_mem_prios[] = {
	TTM_PL_VRAM,
	TTM_PL_TT,
	TTM_PL_SYSTEM,
};

const uint32_t radeon_busy_prios[] = {
	TTM_PL_TT,
	TTM_PL_VRAM,
	TTM_PL_SYSTEM,
};

static int radeon_sync_obj_wait(void *sync_obj, void *sync_arg,
				bool lazy, bool interruptible)
{
	return radeon_fence_wait((struct radeon_fence *)sync_obj, interruptible);
}

static int radeon_sync_obj_flush(void *sync_obj, void *sync_arg)
{
	return 0;
}

static void radeon_sync_obj_unref(void **sync_obj)
{
	radeon_fence_unref((struct radeon_fence **)sync_obj);
}

static void *radeon_sync_obj_ref(void *sync_obj)
{
	return radeon_fence_ref((struct radeon_fence *)sync_obj);
}

static bool radeon_sync_obj_signaled(void *sync_obj, void *sync_arg)
{
	return radeon_fence_signaled((struct radeon_fence *)sync_obj);
}

static struct ttm_bo_driver radeon_bo_driver = {
	.mem_type_prio = radeon_mem_prios,
	.mem_busy_prio = radeon_busy_prios,
	.num_mem_type_prio = ARRAY_SIZE(radeon_mem_prios),
	.num_mem_busy_prio = ARRAY_SIZE(radeon_busy_prios),
	.create_ttm_backend_entry = &radeon_create_ttm_backend_entry,
	.invalidate_caches = &radeon_invalidate_caches,
	.init_mem_type = &radeon_init_mem_type,
	.evict_flags = &radeon_evict_flags,
	.move = &radeon_bo_move,
	.verify_access = &radeon_verify_access,
	.sync_obj_signaled = &radeon_sync_obj_signaled,
	.sync_obj_wait = &radeon_sync_obj_wait,
	.sync_obj_flush = &radeon_sync_obj_flush,
	.sync_obj_unref = &radeon_sync_obj_unref,
	.sync_obj_ref = &radeon_sync_obj_ref,
};

int radeon_ttm_init(struct radeon_device *rdev)
{
	int r;

	r = radeon_ttm_global_init(rdev);
	if (r) {
		return r;
	}
	/* No others user of address space so set it to 0 */
	r = ttm_bo_device_init(&rdev->mman.bdev,
			       rdev->mman.mem_global_ref.object,
			       &radeon_bo_driver, DRM_FILE_PAGE_OFFSET);
	if (r) {
		DRM_ERROR("failed initializing buffer object driver(%d).\n", r);
		return r;
	}
	r = ttm_bo_init_mm(&rdev->mman.bdev, TTM_PL_VRAM, 0,
			   ((rdev->mc.aper_size) >> PAGE_SHIFT));
	if (r) {
		DRM_ERROR("Failed initializing VRAM heap.\n");
		return r;
	}
	r = radeon_object_create(rdev, NULL, 256 * 1024, true,
				 RADEON_GEM_DOMAIN_VRAM, false,
				 &rdev->stollen_vga_memory);
	if (r) {
		return r;
	}
	r = radeon_object_pin(rdev->stollen_vga_memory, RADEON_GEM_DOMAIN_VRAM, NULL);
	if (r) {
		radeon_object_unref(&rdev->stollen_vga_memory);
		return r;
	}
	DRM_INFO("radeon: %uM of VRAM memory ready\n",
		 rdev->mc.vram_size / (1024 * 1024));
	r = ttm_bo_init_mm(&rdev->mman.bdev, TTM_PL_TT, 0,
			   ((rdev->mc.gtt_size) >> PAGE_SHIFT));
	if (r) {
		DRM_ERROR("Failed initializing GTT heap.\n");
		return r;
	}
	DRM_INFO("radeon: %uM of GTT memory ready.\n",
		 rdev->mc.gtt_size / (1024 * 1024));
	if (unlikely(rdev->mman.bdev.dev_mapping == NULL)) {
		rdev->mman.bdev.dev_mapping = rdev->ddev->dev_mapping;
	}
	return 0;
}

void radeon_ttm_fini(struct radeon_device *rdev)
{
	if (rdev->stollen_vga_memory) {
		radeon_object_unpin(rdev->stollen_vga_memory);
		radeon_object_unref(&rdev->stollen_vga_memory);
	}
	ttm_bo_clean_mm(&rdev->mman.bdev, TTM_PL_VRAM);
	ttm_bo_clean_mm(&rdev->mman.bdev, TTM_PL_TT);
	ttm_bo_device_release(&rdev->mman.bdev);
	radeon_gart_fini(rdev);
	radeon_ttm_global_fini(rdev);
	DRM_INFO("radeon: ttm finalized\n");
}

static struct vm_operations_struct radeon_ttm_vm_ops;
static struct vm_operations_struct *ttm_vm_ops = NULL;

static int radeon_ttm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
	struct ttm_buffer_object *bo;
	int r;

	bo = (struct ttm_buffer_object *)vma->vm_private_data;
	if (bo == NULL) {
		return VM_FAULT_NOPAGE;
	}
	r = ttm_vm_ops->fault(vma, vmf);
	return r;
}

int radeon_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *file_priv;
	struct radeon_device *rdev;
	int r;

	if (unlikely(vma->vm_pgoff < DRM_FILE_PAGE_OFFSET)) {
		return drm_mmap(filp, vma);
	}

	file_priv = (struct drm_file *)filp->private_data;
	rdev = file_priv->minor->dev->dev_private;
	if (rdev == NULL) {
		return -EINVAL;
	}
	r = ttm_bo_mmap(filp, vma, &rdev->mman.bdev);
	if (unlikely(r != 0)) {
		return r;
	}
	if (unlikely(ttm_vm_ops == NULL)) {
		ttm_vm_ops = vma->vm_ops;
		radeon_ttm_vm_ops = *ttm_vm_ops;
		radeon_ttm_vm_ops.fault = &radeon_ttm_fault;
	}
	vma->vm_ops = &radeon_ttm_vm_ops;
	return 0;
}


/*
 * TTM backend functions.
 */
struct radeon_ttm_backend {
	struct ttm_backend		backend;
	struct radeon_device		*rdev;
	unsigned long			num_pages;
	struct page			**pages;
	struct page			*dummy_read_page;
	bool				populated;
	bool				bound;
	unsigned			offset;
};

static int radeon_ttm_backend_populate(struct ttm_backend *backend,
				       unsigned long num_pages,
				       struct page **pages,
				       struct page *dummy_read_page)
{
	struct radeon_ttm_backend *gtt;

	gtt = container_of(backend, struct radeon_ttm_backend, backend);
	gtt->pages = pages;
	gtt->num_pages = num_pages;
	gtt->dummy_read_page = dummy_read_page;
	gtt->populated = true;
	return 0;
}

static void radeon_ttm_backend_clear(struct ttm_backend *backend)
{
	struct radeon_ttm_backend *gtt;

	gtt = container_of(backend, struct radeon_ttm_backend, backend);
	gtt->pages = NULL;
	gtt->num_pages = 0;
	gtt->dummy_read_page = NULL;
	gtt->populated = false;
	gtt->bound = false;
}


static int radeon_ttm_backend_bind(struct ttm_backend *backend,
				   struct ttm_mem_reg *bo_mem)
{
	struct radeon_ttm_backend *gtt;
	int r;

	gtt = container_of(backend, struct radeon_ttm_backend, backend);
	gtt->offset = bo_mem->mm_node->start << PAGE_SHIFT;
	if (!gtt->num_pages) {
		WARN(1, "nothing to bind %lu pages for mreg %p back %p!\n", gtt->num_pages, bo_mem, backend);
	}
	r = radeon_gart_bind(gtt->rdev, gtt->offset,
			     gtt->num_pages, gtt->pages);
	if (r) {
		DRM_ERROR("failed to bind %lu pages at 0x%08X\n",
			  gtt->num_pages, gtt->offset);
		return r;
	}
	gtt->bound = true;
	return 0;
}

static int radeon_ttm_backend_unbind(struct ttm_backend *backend)
{
	struct radeon_ttm_backend *gtt;

	gtt = container_of(backend, struct radeon_ttm_backend, backend);
	radeon_gart_unbind(gtt->rdev, gtt->offset, gtt->num_pages);
	gtt->bound = false;
	return 0;
}

static void radeon_ttm_backend_destroy(struct ttm_backend *backend)
{
	struct radeon_ttm_backend *gtt;

	gtt = container_of(backend, struct radeon_ttm_backend, backend);
	if (gtt->bound) {
		radeon_ttm_backend_unbind(backend);
	}
	kfree(gtt);
}

static struct ttm_backend_func radeon_backend_func = {
	.populate = &radeon_ttm_backend_populate,
	.clear = &radeon_ttm_backend_clear,
	.bind = &radeon_ttm_backend_bind,
	.unbind = &radeon_ttm_backend_unbind,
	.destroy = &radeon_ttm_backend_destroy,
};

struct ttm_backend *radeon_ttm_backend_create(struct radeon_device *rdev)
{
	struct radeon_ttm_backend *gtt;

	gtt = kzalloc(sizeof(struct radeon_ttm_backend), GFP_KERNEL);
	if (gtt == NULL) {
		return NULL;
	}
	gtt->backend.bdev = &rdev->mman.bdev;
	gtt->backend.flags = 0;
	gtt->backend.func = &radeon_backend_func;
	gtt->rdev = rdev;
	gtt->pages = NULL;
	gtt->num_pages = 0;
	gtt->dummy_read_page = NULL;
	gtt->populated = false;
	gtt->bound = false;
	return &gtt->backend;
}
