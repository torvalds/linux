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
#include <drm/amdgpu_drm.h>
#include <drm/drm_cache.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"


int amdgpu_ttm_init(struct amdgpu_device *adev);
void amdgpu_ttm_fini(struct amdgpu_device *adev);

static u64 amdgpu_get_vis_part_size(struct amdgpu_device *adev,
						struct ttm_mem_reg *mem)
{
	u64 ret = 0;
	if (mem->start << PAGE_SHIFT < adev->mc.visible_vram_size) {
		ret = (u64)((mem->start << PAGE_SHIFT) + mem->size) >
			   adev->mc.visible_vram_size ?
			   adev->mc.visible_vram_size - (mem->start << PAGE_SHIFT) :
			   mem->size;
	}
	return ret;
}

static void amdgpu_update_memory_usage(struct amdgpu_device *adev,
		       struct ttm_mem_reg *old_mem,
		       struct ttm_mem_reg *new_mem)
{
	u64 vis_size;
	if (!adev)
		return;

	if (new_mem) {
		switch (new_mem->mem_type) {
		case TTM_PL_TT:
			atomic64_add(new_mem->size, &adev->gtt_usage);
			break;
		case TTM_PL_VRAM:
			atomic64_add(new_mem->size, &adev->vram_usage);
			vis_size = amdgpu_get_vis_part_size(adev, new_mem);
			atomic64_add(vis_size, &adev->vram_vis_usage);
			break;
		}
	}

	if (old_mem) {
		switch (old_mem->mem_type) {
		case TTM_PL_TT:
			atomic64_sub(old_mem->size, &adev->gtt_usage);
			break;
		case TTM_PL_VRAM:
			atomic64_sub(old_mem->size, &adev->vram_usage);
			vis_size = amdgpu_get_vis_part_size(adev, old_mem);
			atomic64_sub(vis_size, &adev->vram_vis_usage);
			break;
		}
	}
}

static void amdgpu_ttm_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct amdgpu_bo *bo;

	bo = container_of(tbo, struct amdgpu_bo, tbo);

	amdgpu_update_memory_usage(bo->adev, &bo->tbo.mem, NULL);

	drm_gem_object_release(&bo->gem_base);
	amdgpu_bo_unref(&bo->parent);
	kfree(bo->metadata);
	kfree(bo);
}

bool amdgpu_ttm_bo_is_amdgpu_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &amdgpu_ttm_bo_destroy)
		return true;
	return false;
}

static void amdgpu_ttm_placement_init(struct amdgpu_device *adev,
				      struct ttm_placement *placement,
				      struct ttm_place *placements,
				      u32 domain, u64 flags)
{
	u32 c = 0, i;

	placement->placement = placements;
	placement->busy_placement = placements;

	if (domain & AMDGPU_GEM_DOMAIN_VRAM) {
		if (flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS &&
			adev->mc.visible_vram_size < adev->mc.real_vram_size) {
			placements[c].fpfn =
				adev->mc.visible_vram_size >> PAGE_SHIFT;
			placements[c++].flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
				TTM_PL_FLAG_VRAM | TTM_PL_FLAG_TOPDOWN;
		}
		placements[c].fpfn = 0;
		placements[c++].flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED |
			TTM_PL_FLAG_VRAM;
		if (!(flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED))
			placements[c - 1].flags |= TTM_PL_FLAG_TOPDOWN;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GTT) {
		if (flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC) {
			placements[c].fpfn = 0;
			placements[c++].flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_TT |
				TTM_PL_FLAG_UNCACHED;
		} else {
			placements[c].fpfn = 0;
			placements[c++].flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_TT;
		}
	}

	if (domain & AMDGPU_GEM_DOMAIN_CPU) {
		if (flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC) {
			placements[c].fpfn = 0;
			placements[c++].flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_SYSTEM |
				TTM_PL_FLAG_UNCACHED;
		} else {
			placements[c].fpfn = 0;
			placements[c++].flags = TTM_PL_FLAG_CACHED | TTM_PL_FLAG_SYSTEM;
		}
	}

	if (domain & AMDGPU_GEM_DOMAIN_GDS) {
		placements[c].fpfn = 0;
		placements[c++].flags = TTM_PL_FLAG_UNCACHED |
			AMDGPU_PL_FLAG_GDS;
	}
	if (domain & AMDGPU_GEM_DOMAIN_GWS) {
		placements[c].fpfn = 0;
		placements[c++].flags = TTM_PL_FLAG_UNCACHED |
			AMDGPU_PL_FLAG_GWS;
	}
	if (domain & AMDGPU_GEM_DOMAIN_OA) {
		placements[c].fpfn = 0;
		placements[c++].flags = TTM_PL_FLAG_UNCACHED |
			AMDGPU_PL_FLAG_OA;
	}

	if (!c) {
		placements[c].fpfn = 0;
		placements[c++].flags = TTM_PL_MASK_CACHING |
			TTM_PL_FLAG_SYSTEM;
	}
	placement->num_placement = c;
	placement->num_busy_placement = c;

	for (i = 0; i < c; i++) {
		if ((flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED) &&
			(placements[i].flags & TTM_PL_FLAG_VRAM) &&
			!placements[i].fpfn)
			placements[i].lpfn =
				adev->mc.visible_vram_size >> PAGE_SHIFT;
		else
			placements[i].lpfn = 0;
	}
}

void amdgpu_ttm_placement_from_domain(struct amdgpu_bo *rbo, u32 domain)
{
	amdgpu_ttm_placement_init(rbo->adev, &rbo->placement,
				  rbo->placements, domain, rbo->flags);
}

static void amdgpu_fill_placement_to_bo(struct amdgpu_bo *bo,
					struct ttm_placement *placement)
{
	BUG_ON(placement->num_placement > (AMDGPU_GEM_DOMAIN_MAX + 1));

	memcpy(bo->placements, placement->placement,
	       placement->num_placement * sizeof(struct ttm_place));
	bo->placement.num_placement = placement->num_placement;
	bo->placement.num_busy_placement = placement->num_busy_placement;
	bo->placement.placement = bo->placements;
	bo->placement.busy_placement = bo->placements;
}

int amdgpu_bo_create_restricted(struct amdgpu_device *adev,
				unsigned long size, int byte_align,
				bool kernel, u32 domain, u64 flags,
				struct sg_table *sg,
				struct ttm_placement *placement,
				struct reservation_object *resv,
				struct amdgpu_bo **bo_ptr)
{
	struct amdgpu_bo *bo;
	enum ttm_bo_type type;
	unsigned long page_align;
	size_t acc_size;
	int r;

	page_align = roundup(byte_align, PAGE_SIZE) >> PAGE_SHIFT;
	size = ALIGN(size, PAGE_SIZE);

	if (kernel) {
		type = ttm_bo_type_kernel;
	} else if (sg) {
		type = ttm_bo_type_sg;
	} else {
		type = ttm_bo_type_device;
	}
	*bo_ptr = NULL;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	bo = kzalloc(sizeof(struct amdgpu_bo), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	r = drm_gem_object_init(adev->ddev, &bo->gem_base, size);
	if (unlikely(r)) {
		kfree(bo);
		return r;
	}
	bo->adev = adev;
	INIT_LIST_HEAD(&bo->list);
	INIT_LIST_HEAD(&bo->va);
	bo->prefered_domains = domain & (AMDGPU_GEM_DOMAIN_VRAM |
					 AMDGPU_GEM_DOMAIN_GTT |
					 AMDGPU_GEM_DOMAIN_CPU |
					 AMDGPU_GEM_DOMAIN_GDS |
					 AMDGPU_GEM_DOMAIN_GWS |
					 AMDGPU_GEM_DOMAIN_OA);
	bo->allowed_domains = bo->prefered_domains;
	if (!kernel && bo->allowed_domains == AMDGPU_GEM_DOMAIN_VRAM)
		bo->allowed_domains |= AMDGPU_GEM_DOMAIN_GTT;

	bo->flags = flags;

	/* For architectures that don't support WC memory,
	 * mask out the WC flag from the BO
	 */
	if (!drm_arch_can_wc_memory())
		bo->flags &= ~AMDGPU_GEM_CREATE_CPU_GTT_USWC;

	amdgpu_fill_placement_to_bo(bo, placement);
	/* Kernel allocation are uninterruptible */
	r = ttm_bo_init(&adev->mman.bdev, &bo->tbo, size, type,
			&bo->placement, page_align, !kernel, NULL,
			acc_size, sg, resv, &amdgpu_ttm_bo_destroy);
	if (unlikely(r != 0)) {
		return r;
	}
	*bo_ptr = bo;

	trace_amdgpu_bo_create(bo);

	return 0;
}

int amdgpu_bo_create(struct amdgpu_device *adev,
		     unsigned long size, int byte_align,
		     bool kernel, u32 domain, u64 flags,
		     struct sg_table *sg,
		     struct reservation_object *resv,
		     struct amdgpu_bo **bo_ptr)
{
	struct ttm_placement placement = {0};
	struct ttm_place placements[AMDGPU_GEM_DOMAIN_MAX + 1];

	memset(&placements, 0,
	       (AMDGPU_GEM_DOMAIN_MAX + 1) * sizeof(struct ttm_place));

	amdgpu_ttm_placement_init(adev, &placement,
				  placements, domain, flags);

	return amdgpu_bo_create_restricted(adev, size, byte_align, kernel,
					   domain, flags, sg, &placement,
					   resv, bo_ptr);
}

int amdgpu_bo_kmap(struct amdgpu_bo *bo, void **ptr)
{
	bool is_iomem;
	int r;

	if (bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS)
		return -EPERM;

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
	return 0;
}

void amdgpu_bo_kunmap(struct amdgpu_bo *bo)
{
	if (bo->kptr == NULL)
		return;
	bo->kptr = NULL;
	ttm_bo_kunmap(&bo->kmap);
}

struct amdgpu_bo *amdgpu_bo_ref(struct amdgpu_bo *bo)
{
	if (bo == NULL)
		return NULL;

	ttm_bo_reference(&bo->tbo);
	return bo;
}

void amdgpu_bo_unref(struct amdgpu_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;

	tbo = &((*bo)->tbo);
	ttm_bo_unref(&tbo);
	if (tbo == NULL)
		*bo = NULL;
}

int amdgpu_bo_pin_restricted(struct amdgpu_bo *bo, u32 domain,
			     u64 min_offset, u64 max_offset,
			     u64 *gpu_addr)
{
	int r, i;
	unsigned fpfn, lpfn;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm))
		return -EPERM;

	if (WARN_ON_ONCE(min_offset > max_offset))
		return -EINVAL;

	if (bo->pin_count) {
		bo->pin_count++;
		if (gpu_addr)
			*gpu_addr = amdgpu_bo_gpu_offset(bo);

		if (max_offset != 0) {
			u64 domain_start;
			if (domain == AMDGPU_GEM_DOMAIN_VRAM)
				domain_start = bo->adev->mc.vram_start;
			else
				domain_start = bo->adev->mc.gtt_start;
			WARN_ON_ONCE(max_offset <
				     (amdgpu_bo_gpu_offset(bo) - domain_start));
		}

		return 0;
	}
	amdgpu_ttm_placement_from_domain(bo, domain);
	for (i = 0; i < bo->placement.num_placement; i++) {
		/* force to pin into visible video ram */
		if ((bo->placements[i].flags & TTM_PL_FLAG_VRAM) &&
		    !(bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS) &&
		    (!max_offset || max_offset > bo->adev->mc.visible_vram_size)) {
			if (WARN_ON_ONCE(min_offset >
					 bo->adev->mc.visible_vram_size))
				return -EINVAL;
			fpfn = min_offset >> PAGE_SHIFT;
			lpfn = bo->adev->mc.visible_vram_size >> PAGE_SHIFT;
		} else {
			fpfn = min_offset >> PAGE_SHIFT;
			lpfn = max_offset >> PAGE_SHIFT;
		}
		if (fpfn > bo->placements[i].fpfn)
			bo->placements[i].fpfn = fpfn;
		if (!bo->placements[i].lpfn ||
		    (lpfn && lpfn < bo->placements[i].lpfn))
			bo->placements[i].lpfn = lpfn;
		bo->placements[i].flags |= TTM_PL_FLAG_NO_EVICT;
	}

	r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
	if (likely(r == 0)) {
		bo->pin_count = 1;
		if (gpu_addr != NULL)
			*gpu_addr = amdgpu_bo_gpu_offset(bo);
		if (domain == AMDGPU_GEM_DOMAIN_VRAM)
			bo->adev->vram_pin_size += amdgpu_bo_size(bo);
		else
			bo->adev->gart_pin_size += amdgpu_bo_size(bo);
	} else {
		dev_err(bo->adev->dev, "%p pin failed\n", bo);
	}
	return r;
}

int amdgpu_bo_pin(struct amdgpu_bo *bo, u32 domain, u64 *gpu_addr)
{
	return amdgpu_bo_pin_restricted(bo, domain, 0, 0, gpu_addr);
}

int amdgpu_bo_unpin(struct amdgpu_bo *bo)
{
	int r, i;

	if (!bo->pin_count) {
		dev_warn(bo->adev->dev, "%p unpin not necessary\n", bo);
		return 0;
	}
	bo->pin_count--;
	if (bo->pin_count)
		return 0;
	for (i = 0; i < bo->placement.num_placement; i++) {
		bo->placements[i].lpfn = 0;
		bo->placements[i].flags &= ~TTM_PL_FLAG_NO_EVICT;
	}
	r = ttm_bo_validate(&bo->tbo, &bo->placement, false, false);
	if (likely(r == 0)) {
		if (bo->tbo.mem.mem_type == TTM_PL_VRAM)
			bo->adev->vram_pin_size -= amdgpu_bo_size(bo);
		else
			bo->adev->gart_pin_size -= amdgpu_bo_size(bo);
	} else {
		dev_err(bo->adev->dev, "%p validate failed for unpin\n", bo);
	}
	return r;
}

int amdgpu_bo_evict_vram(struct amdgpu_device *adev)
{
	/* late 2.6.33 fix IGP hibernate - we need pm ops to do this correct */
	if (0 && (adev->flags & AMD_IS_APU)) {
		/* Useless to evict on IGP chips */
		return 0;
	}
	return ttm_bo_evict_mm(&adev->mman.bdev, TTM_PL_VRAM);
}

int amdgpu_bo_init(struct amdgpu_device *adev)
{
	/* Add an MTRR for the VRAM */
	adev->mc.vram_mtrr = arch_phys_wc_add(adev->mc.aper_base,
					      adev->mc.aper_size);
	DRM_INFO("Detected VRAM RAM=%lluM, BAR=%lluM\n",
		adev->mc.mc_vram_size >> 20,
		(unsigned long long)adev->mc.aper_size >> 20);
	DRM_INFO("RAM width %dbits DDR\n",
			adev->mc.vram_width);
	return amdgpu_ttm_init(adev);
}

void amdgpu_bo_fini(struct amdgpu_device *adev)
{
	amdgpu_ttm_fini(adev);
	arch_phys_wc_del(adev->mc.vram_mtrr);
}

int amdgpu_bo_fbdev_mmap(struct amdgpu_bo *bo,
			     struct vm_area_struct *vma)
{
	return ttm_fbdev_mmap(vma, &bo->tbo);
}

int amdgpu_bo_set_tiling_flags(struct amdgpu_bo *bo, u64 tiling_flags)
{
	if (AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT) > 6)
		return -EINVAL;

	bo->tiling_flags = tiling_flags;
	return 0;
}

void amdgpu_bo_get_tiling_flags(struct amdgpu_bo *bo, u64 *tiling_flags)
{
	lockdep_assert_held(&bo->tbo.resv->lock.base);

	if (tiling_flags)
		*tiling_flags = bo->tiling_flags;
}

int amdgpu_bo_set_metadata (struct amdgpu_bo *bo, void *metadata,
			    uint32_t metadata_size, uint64_t flags)
{
	void *buffer;

	if (!metadata_size) {
		if (bo->metadata_size) {
			kfree(bo->metadata);
			bo->metadata_size = 0;
		}
		return 0;
	}

	if (metadata == NULL)
		return -EINVAL;

	buffer = kmemdup(metadata, metadata_size, GFP_KERNEL);
	if (buffer == NULL)
		return -ENOMEM;

	kfree(bo->metadata);
	bo->metadata_flags = flags;
	bo->metadata = buffer;
	bo->metadata_size = metadata_size;

	return 0;
}

int amdgpu_bo_get_metadata(struct amdgpu_bo *bo, void *buffer,
			   size_t buffer_size, uint32_t *metadata_size,
			   uint64_t *flags)
{
	if (!buffer && !metadata_size)
		return -EINVAL;

	if (buffer) {
		if (buffer_size < bo->metadata_size)
			return -EINVAL;

		if (bo->metadata_size)
			memcpy(buffer, bo->metadata, bo->metadata_size);
	}

	if (metadata_size)
		*metadata_size = bo->metadata_size;
	if (flags)
		*flags = bo->metadata_flags;

	return 0;
}

void amdgpu_bo_move_notify(struct ttm_buffer_object *bo,
			   struct ttm_mem_reg *new_mem)
{
	struct amdgpu_bo *rbo;

	if (!amdgpu_ttm_bo_is_amdgpu_bo(bo))
		return;

	rbo = container_of(bo, struct amdgpu_bo, tbo);
	amdgpu_vm_bo_invalidate(rbo->adev, rbo);

	/* update statistics */
	if (!new_mem)
		return;

	/* move_notify is called before move happens */
	amdgpu_update_memory_usage(rbo->adev, &bo->mem, new_mem);
}

int amdgpu_bo_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev;
	struct amdgpu_bo *abo;
	unsigned long offset, size, lpfn;
	int i, r;

	if (!amdgpu_ttm_bo_is_amdgpu_bo(bo))
		return 0;

	abo = container_of(bo, struct amdgpu_bo, tbo);
	adev = abo->adev;
	if (bo->mem.mem_type != TTM_PL_VRAM)
		return 0;

	size = bo->mem.num_pages << PAGE_SHIFT;
	offset = bo->mem.start << PAGE_SHIFT;
	if ((offset + size) <= adev->mc.visible_vram_size)
		return 0;

	/* hurrah the memory is not visible ! */
	amdgpu_ttm_placement_from_domain(abo, AMDGPU_GEM_DOMAIN_VRAM);
	lpfn =	adev->mc.visible_vram_size >> PAGE_SHIFT;
	for (i = 0; i < abo->placement.num_placement; i++) {
		/* Force into visible VRAM */
		if ((abo->placements[i].flags & TTM_PL_FLAG_VRAM) &&
		    (!abo->placements[i].lpfn || abo->placements[i].lpfn > lpfn))
			abo->placements[i].lpfn = lpfn;
	}
	r = ttm_bo_validate(bo, &abo->placement, false, false);
	if (unlikely(r == -ENOMEM)) {
		amdgpu_ttm_placement_from_domain(abo, AMDGPU_GEM_DOMAIN_GTT);
		return ttm_bo_validate(bo, &abo->placement, false, false);
	} else if (unlikely(r != 0)) {
		return r;
	}

	offset = bo->mem.start << PAGE_SHIFT;
	/* this should never happen */
	if ((offset + size) > adev->mc.visible_vram_size)
		return -EINVAL;

	return 0;
}

/**
 * amdgpu_bo_fence - add fence to buffer object
 *
 * @bo: buffer object in question
 * @fence: fence to add
 * @shared: true if fence should be added shared
 *
 */
void amdgpu_bo_fence(struct amdgpu_bo *bo, struct fence *fence,
		     bool shared)
{
	struct reservation_object *resv = bo->tbo.resv;

	if (shared)
		reservation_object_add_shared_fence(resv, fence);
	else
		reservation_object_add_excl_fence(resv, fence);
}
