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
#include <linux/list.h>
#include <linux/slab.h>
#include <drm/drmP.h>
#include <drm/radeon_drm.h>
#include "radeon.h"
#include "radeon_trace.h"


int radeon_ttm_init(struct radeon_device *rdev);
void radeon_ttm_fini(struct radeon_device *rdev);
static void radeon_bo_clear_surface_reg(struct radeon_bo *bo);

/*
 * To exclude mutual BO access we rely on bo_reserve exclusion, as all
 * function are calling it.
 */

void radeon_bo_clear_va(struct radeon_bo *bo)
{
	struct radeon_bo_va *bo_va, *tmp;

	list_for_each_entry_safe(bo_va, tmp, &bo->va, bo_list) {
		/* remove from all vm address space */
		radeon_vm_bo_rmv(bo->rdev, bo_va);
	}
}

static void radeon_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct radeon_bo *bo;

	bo = container_of(tbo, struct radeon_bo, tbo);
	mutex_lock(&bo->rdev->gem.mutex);
	list_del_init(&bo->list);
	mutex_unlock(&bo->rdev->gem.mutex);
	radeon_bo_clear_surface_reg(bo);
	radeon_bo_clear_va(bo);
	drm_gem_object_release(&bo->gem_base);
	kfree(bo);
}

bool radeon_ttm_bo_is_radeon_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &radeon_ttm_bo_destroy)
		return true;
	return false;
}

void radeon_ttm_placement_from_domain(struct radeon_bo *rbo, u32 domain)
{
	u32 c = 0;

	rbo->placement.fpfn = 0;
	rbo->placement.lpfn = 0;
	rbo->placement.placement = rbo->placements;
	rbo->placement.busy_placement = rbo->placements;
	if (domain & RADEON_GEM_DOMAIN_VRAM)
		rbo->placements[c++] = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
					TTM_PL_FLAG_VRAM;
	if (domain & RADEON_GEM_DOMAIN_GTT) {
		if (rbo->rdev->flags & RADEON_IS_AGP) {
			rbo->placements[c++] = TTM_PL_FLAG_WC | TTM_PL_FLAG_TT;
		} else {
			rbo->placements[c++] = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_TT;
		}
	}
	if (domain & RADEON_GEM_DOMAIN_CPU) {
		if (rbo->rdev->flags & RADEON_IS_AGP) {
			rbo->placements[c++] = TTM_PL_FLAG_WC | TTM_PL_FLAG_SYSTEM;
		} else {
			rbo->placements[c++] = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_SYSTEM;
		}
	}
	if (!c)
		rbo->placements[c++] = TTM_PL_MASK_CACHING | TTM_PL_FLAG_SYSTEM;
	rbo->placement.num_placement = c;
	rbo->placement.num_busy_placement = c;
}

int radeon_bo_create(struct radeon_device *rdev,
		     unsigned long size, int byte_align, bool kernel, u32 domain,
		     struct sg_table *sg, struct radeon_bo **bo_ptr)
{
	struct radeon_bo *bo;
	enum ttm_bo_type type;
	unsigned long page_align = roundup(byte_align, PAGE_SIZE) >> PAGE_SHIFT;
	size_t acc_size;
	int r;

	size = ALIGN(size, PAGE_SIZE);

	rdev->mman.bdev.dev_mapping = rdev->ddev->dev_mapping;
	if (kernel) {
		type = ttm_bo_type_kernel;
	} else if (sg) {
		type = ttm_bo_type_sg;
	} else {
		type = ttm_bo_type_device;
	}
	*bo_ptr = NULL;

	acc_size = ttm_bo_dma_acc_size(&rdev->mman.bdev, size,
				       sizeof(struct radeon_bo));

	bo = kzalloc(sizeof(struct radeon_bo), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	r = drm_gem_object_init(rdev->ddev, &bo->gem_base, size);
	if (unlikely(r)) {
		kfree(bo);
		return r;
	}
	bo->rdev = rdev;
	bo->gem_base.driver_private = NULL;
	bo->surface_reg = -1;
	INIT_LIST_HEAD(&bo->list);
	INIT_LIST_HEAD(&bo->va);
	radeon_ttm_placement_from_domain(bo, domain);
	/* Kernel allocation are uninterruptible */
	down_read(&rdev->pm.mclk_lock);
	r = ttm_bo_init(&rdev->mman.bdev, &bo->tbo, size, type,
			&bo->placement, page_align, !kernel, NULL,
			acc_size, sg, &radeon_ttm_bo_destroy);
	up_read(&rdev->pm.mclk_lock);
	if (unlikely(r != 0)) {
		return r;
	}
	*bo_ptr = bo;

	trace_radeon_bo_create(bo);

	return 0;
}

int radeon_bo_kmap(struct radeon_bo *bo, void **ptr)
{
	bool is_iomem;
	int r;

	if (bo->kptr) {
		if (ptr) {
			*ptr = bo->kptr;
		}
		return 0;
	}
	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r) {
		return r;
	}
	bo->kptr = ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
	if (ptr) {
		*ptr = bo->kptr;
	}
	radeon_bo_check_tiling(bo, 0, 0);
	return 0;
}

void radeon_bo_kunmap(struct radeon_bo *bo)
{
	if (bo->kptr == NULL)
		return;
	bo->kptr = NULL;
	radeon_bo_check_tiling(bo, 0, 0);
	ttm_bo_kunmap(&bo->kmap);
}

void radeon_bo_unref(struct radeon_bo **bo)
{
	struct ttm_buffer_object *tbo;
	struct radeon_device *rdev;

	if ((*bo) == NULL)
		return;
	rdev = (*bo)->rdev;
	tbo = &((*bo)->tbo);
	down_read(&rdev->pm.mclk_lock);
	ttm_bo_unref(&tbo);
	up_read(&rdev->pm.mclk_lock);
	if (tbo == NULL)
		*bo = NULL;
}

int radeon_bo_pin_restricted(struct radeon_bo *bo, u32 domain, u64 max_offset,
			     u64 *gpu_addr)
{
	int r, i;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = radeon_bo_gpu_offset(bo);

		if (max_offset != 0) {
			u64 domain_start;

			if (domain == RADEON_GEM_DOMAIN_VRAM)
				domain_start = bo->rdev->mc.vram_start;
			else
				domain_start = bo->rdev->mc.gtt_start;
			WARN_ON_ONCE(max_offset <
				     (radeon_bo_gpu_offset(bo) - domain_start));
		}

		return 0;
	}
	radeon_ttm_placement_from_domain(bo, domain);
	if (domain == RADEON_GEM_DOMAIN_VRAM) {
		/* force to pin into visible video ram */
		bo->placement.lpfn = bo->rdev->mc.visible_vram_size >> PAGE_SHIFT;
	}
	if (max_offset) {
		u64 lpfn = max_offset >> PAGE_SHIFT;

		if (!bo->placement.lpfn)
			bo->placement.lpfn = bo->rdev->mc.gtt_size >> PAGE_SHIFT;

		if (lpfn < bo->placement.lpfn)
			bo->placement.lpfn = lpfn;
	}
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i] |= TTM_PL_FLAG_NO_EVICT;
	r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
	if (likely(r == 0)) {
		bo->pin_count = 1;
		if (gpu_addr != NULL)
			*gpu_addr = radeon_bo_gpu_offset(bo);
	}
	if (unlikely(r != 0))
		dev_err(bo->rdev->dev, "%p pin failed\n", bo);
	return r;
}

int radeon_bo_pin(struct radeon_bo *bo, u32 domain, u64 *gpu_addr)
{
	return radeon_bo_pin_restricted(bo, domain, 0, gpu_addr);
}

int radeon_bo_unpin(struct radeon_bo *bo)
{
	int r, i;

	if (!bo->pin_count) {
		dev_warn(bo->rdev->dev, "%p unpin not necessary\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;
	for (i = 0; i < bo->placement.num_placement; i++)
		bo->placements[i] &= ~TTM_PL_FLAG_NO_EVICT;
	r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
	if (unlikely(r != 0))
		dev_err(bo->rdev->dev, "%p validate failed for unpin\n", bo);
	return r;
}

int radeon_bo_evict_vram(struct radeon_device *rdev)
{
	/* late 2.6.33 fix IGP hibernate - we need pm ops to do this correct */
	if (0 && (rdev->flags & RADEON_IS_IGP)) {
		if (rdev->mc.igp_sideport_enabled == false)
			/* Useless to evict on IGP chips */
			return 0;
	}
	return ttm_bo_evict_mm(&rdev->mman.bdev, TTM_PL_VRAM);
}

void radeon_bo_force_delete(struct radeon_device *rdev)
{
	struct radeon_bo *bo, *n;

	if (list_empty(&rdev->gem.objects)) {
		return;
	}
	dev_err(rdev->dev, "Userspace still has active objects !\n");
	list_for_each_entry_safe(bo, n, &rdev->gem.objects, list) {
		mutex_lock(&rdev->ddev->struct_mutex);
		dev_err(rdev->dev, "%p %p %lu %lu force free\n",
			&bo->gem_base, bo, (unsigned long)bo->gem_base.size,
			*((unsigned long *)&bo->gem_base.refcount));
		mutex_lock(&bo->rdev->gem.mutex);
		list_del_init(&bo->list);
		mutex_unlock(&bo->rdev->gem.mutex);
		/* this should unref the ttm bo */
		drm_gem_object_unreference(&bo->gem_base);
		mutex_unlock(&rdev->ddev->struct_mutex);
	}
}

int radeon_bo_init(struct radeon_device *rdev)
{
	/* Add an MTRR for the VRAM */
	if (!rdev->fastfb_working) {
		rdev->mc.vram_mtrr = arch_phys_wc_add(rdev->mc.aper_base,
						      rdev->mc.aper_size);
	}
	DRM_INFO("Detected VRAM RAM=%lluM, BAR=%lluM\n",
		rdev->mc.mc_vram_size >> 20,
		(unsigned long long)rdev->mc.aper_size >> 20);
	DRM_INFO("RAM width %dbits %cDR\n",
			rdev->mc.vram_width, rdev->mc.vram_is_ddr ? 'D' : 'S');
	return radeon_ttm_init(rdev);
}

void radeon_bo_fini(struct radeon_device *rdev)
{
	radeon_ttm_fini(rdev);
	arch_phys_wc_del(rdev->mc.vram_mtrr);
}

void radeon_bo_list_add_object(struct radeon_bo_list *lobj,
				struct list_head *head)
{
	if (lobj->written) {
		list_add(&lobj->tv.head, head);
	} else {
		list_add_tail(&lobj->tv.head, head);
	}
}

int radeon_bo_list_validate(struct ww_acquire_ctx *ticket,
			    struct list_head *head, int ring)
{
	struct radeon_bo_list *lobj;
	struct radeon_bo *bo;
	u32 domain;
	int r;

	r = ttm_eu_reserve_buffers(ticket, head);
	if (unlikely(r != 0)) {
		return r;
	}
	list_for_each_entry(lobj, head, tv.head) {
		bo = lobj->bo;
		if (!bo->pin_count) {
			domain = lobj->domain;
			
		retry:
			radeon_ttm_placement_from_domain(bo, domain);
			if (ring == R600_RING_TYPE_UVD_INDEX)
				radeon_uvd_force_into_uvd_segment(bo);
			r = ttm_bo_validate(&bo->tbo, &bo->placement,
						true, false);
			if (unlikely(r)) {
				if (r != -ERESTARTSYS && domain != lobj->alt_domain) {
					domain = lobj->alt_domain;
					goto retry;
				}
				return r;
			}
		}
		lobj->gpu_offset = radeon_bo_gpu_offset(bo);
		lobj->tiling_flags = bo->tiling_flags;
	}
	return 0;
}

int radeon_bo_fbdev_mmap(struct radeon_bo *bo,
			     struct vm_area_struct *vma)
{
	return ttm_fbdev_mmap(vma, &bo->tbo);
}

int radeon_bo_get_surface_reg(struct radeon_bo *bo)
{
	struct radeon_device *rdev = bo->rdev;
	struct radeon_surface_reg *reg;
	struct radeon_bo *old_object;
	int steal;
	int i;

	BUG_ON(!radeon_bo_is_reserved(bo));

	if (!bo->tiling_flags)
		return 0;

	if (bo->surface_reg >= 0) {
		reg = &rdev->surface_regs[bo->surface_reg];
		i = bo->surface_reg;
		goto out;
	}

	steal = -1;
	for (i = 0; i < RADEON_GEM_MAX_SURFACES; i++) {

		reg = &rdev->surface_regs[i];
		if (!reg->bo)
			break;

		old_object = reg->bo;
		if (old_object->pin_count == 0)
			steal = i;
	}

	/* if we are all out */
	if (i == RADEON_GEM_MAX_SURFACES) {
		if (steal == -1)
			return -ENOMEM;
		/* find someone with a surface reg and nuke their BO */
		reg = &rdev->surface_regs[steal];
		old_object = reg->bo;
		/* blow away the mapping */
		DRM_DEBUG("stealing surface reg %d from %p\n", steal, old_object);
		ttm_bo_unmap_virtual(&old_object->tbo);
		old_object->surface_reg = -1;
		i = steal;
	}

	bo->surface_reg = i;
	reg->bo = bo;

out:
	radeon_set_surface_reg(rdev, i, bo->tiling_flags, bo->pitch,
			       bo->tbo.mem.start << PAGE_SHIFT,
			       bo->tbo.num_pages << PAGE_SHIFT);
	return 0;
}

static void radeon_bo_clear_surface_reg(struct radeon_bo *bo)
{
	struct radeon_device *rdev = bo->rdev;
	struct radeon_surface_reg *reg;

	if (bo->surface_reg == -1)
		return;

	reg = &rdev->surface_regs[bo->surface_reg];
	radeon_clear_surface_reg(rdev, bo->surface_reg);

	reg->bo = NULL;
	bo->surface_reg = -1;
}

int radeon_bo_set_tiling_flags(struct radeon_bo *bo,
				uint32_t tiling_flags, uint32_t pitch)
{
	struct radeon_device *rdev = bo->rdev;
	int r;

	if (rdev->family >= CHIP_CEDAR) {
		unsigned bankw, bankh, mtaspect, tilesplit, stilesplit;

		bankw = (tiling_flags >> RADEON_TILING_EG_BANKW_SHIFT) & RADEON_TILING_EG_BANKW_MASK;
		bankh = (tiling_flags >> RADEON_TILING_EG_BANKH_SHIFT) & RADEON_TILING_EG_BANKH_MASK;
		mtaspect = (tiling_flags >> RADEON_TILING_EG_MACRO_TILE_ASPECT_SHIFT) & RADEON_TILING_EG_MACRO_TILE_ASPECT_MASK;
		tilesplit = (tiling_flags >> RADEON_TILING_EG_TILE_SPLIT_SHIFT) & RADEON_TILING_EG_TILE_SPLIT_MASK;
		stilesplit = (tiling_flags >> RADEON_TILING_EG_STENCIL_TILE_SPLIT_SHIFT) & RADEON_TILING_EG_STENCIL_TILE_SPLIT_MASK;
		switch (bankw) {
		case 0:
		case 1:
		case 2:
		case 4:
		case 8:
			break;
		default:
			return -EINVAL;
		}
		switch (bankh) {
		case 0:
		case 1:
		case 2:
		case 4:
		case 8:
			break;
		default:
			return -EINVAL;
		}
		switch (mtaspect) {
		case 0:
		case 1:
		case 2:
		case 4:
		case 8:
			break;
		default:
			return -EINVAL;
		}
		if (tilesplit > 6) {
			return -EINVAL;
		}
		if (stilesplit > 6) {
			return -EINVAL;
		}
	}
	r = radeon_bo_reserve(bo, false);
	if (unlikely(r != 0))
		return r;
	bo->tiling_flags = tiling_flags;
	bo->pitch = pitch;
	radeon_bo_unreserve(bo);
	return 0;
}

void radeon_bo_get_tiling_flags(struct radeon_bo *bo,
				uint32_t *tiling_flags,
				uint32_t *pitch)
{
	BUG_ON(!radeon_bo_is_reserved(bo));
	if (tiling_flags)
		*tiling_flags = bo->tiling_flags;
	if (pitch)
		*pitch = bo->pitch;
}

int radeon_bo_check_tiling(struct radeon_bo *bo, bool has_moved,
				bool force_drop)
{
	BUG_ON(!radeon_bo_is_reserved(bo) && !force_drop);

	if (!(bo->tiling_flags & RADEON_TILING_SURFACE))
		return 0;

	if (force_drop) {
		radeon_bo_clear_surface_reg(bo);
		return 0;
	}

	if (bo->tbo.mem.mem_type != TTM_PL_VRAM) {
		if (!has_moved)
			return 0;

		if (bo->surface_reg >= 0)
			radeon_bo_clear_surface_reg(bo);
		return 0;
	}

	if ((bo->surface_reg >= 0) && !has_moved)
		return 0;

	return radeon_bo_get_surface_reg(bo);
}

void radeon_bo_move_notify(struct ttm_buffer_object *bo,
			   struct ttm_mem_reg *mem)
{
	struct radeon_bo *rbo;
	if (!radeon_ttm_bo_is_radeon_bo(bo))
		return;
	rbo = container_of(bo, struct radeon_bo, tbo);
	radeon_bo_check_tiling(rbo, 0, 1);
	radeon_vm_bo_invalidate(rbo->rdev, rbo);
}

int radeon_bo_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct radeon_device *rdev;
	struct radeon_bo *rbo;
	unsigned long offset, size;
	int r;

	if (!radeon_ttm_bo_is_radeon_bo(bo))
		return 0;
	rbo = container_of(bo, struct radeon_bo, tbo);
	radeon_bo_check_tiling(rbo, 0, 0);
	rdev = rbo->rdev;
	if (bo->mem.mem_type == TTM_PL_VRAM) {
		size = bo->mem.num_pages << PAGE_SHIFT;
		offset = bo->mem.start << PAGE_SHIFT;
		if ((offset + size) > rdev->mc.visible_vram_size) {
			/* hurrah the memory is not visible ! */
			radeon_ttm_placement_from_domain(rbo, RADEON_GEM_DOMAIN_VRAM);
			rbo->placement.lpfn = rdev->mc.visible_vram_size >> PAGE_SHIFT;
			r = ttm_bo_validate(bo, &rbo->placement, false, false);
			if (unlikely(r != 0))
				return r;
			offset = bo->mem.start << PAGE_SHIFT;
			/* this should not happen */
			if ((offset + size) > rdev->mc.visible_vram_size)
				return -EINVAL;
		}
	}
	return 0;
}

int radeon_bo_wait(struct radeon_bo *bo, u32 *mem_type, bool no_wait)
{
	int r;

	r = ttm_bo_reserve(&bo->tbo, true, no_wait, false, 0);
	if (unlikely(r != 0))
		return r;
	spin_lock(&bo->tbo.bdev->fence_lock);
	if (mem_type)
		*mem_type = bo->tbo.mem.mem_type;
	if (bo->tbo.sync_obj)
		r = ttm_bo_wait(&bo->tbo, true, true, no_wait);
	spin_unlock(&bo->tbo.bdev->fence_lock);
	ttm_bo_unreserve(&bo->tbo);
	return r;
}
