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
#include <linux/dma-buf.h>

#include <drm/amdgpu_drm.h>
#include <drm/drm_cache.h>
#include "amdgpu.h"
#include "amdgpu_trace.h"
#include "amdgpu_amdkfd.h"

/**
 * DOC: amdgpu_object
 *
 * This defines the interfaces to operate on an &amdgpu_bo buffer object which
 * represents memory used by driver (VRAM, system memory, etc.). The driver
 * provides DRM/GEM APIs to userspace. DRM/GEM APIs then use these interfaces
 * to create/destroy/set buffer object which are then managed by the kernel TTM
 * memory manager.
 * The interfaces are also used internally by kernel clients, including gfx,
 * uvd, etc. for kernel managed allocations used by the GPU.
 *
 */

/**
 * amdgpu_bo_subtract_pin_size - Remove BO from pin_size accounting
 *
 * @bo: &amdgpu_bo buffer object
 *
 * This function is called when a BO stops being pinned, and updates the
 * &amdgpu_device pin_size values accordingly.
 */
static void amdgpu_bo_subtract_pin_size(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	if (bo->tbo.mem.mem_type == TTM_PL_VRAM) {
		atomic64_sub(amdgpu_bo_size(bo), &adev->vram_pin_size);
		atomic64_sub(amdgpu_vram_mgr_bo_visible_size(bo),
			     &adev->visible_pin_size);
	} else if (bo->tbo.mem.mem_type == TTM_PL_TT) {
		atomic64_sub(amdgpu_bo_size(bo), &adev->gart_pin_size);
	}
}

static void amdgpu_bo_destroy(struct ttm_buffer_object *tbo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(tbo->bdev);
	struct amdgpu_bo *bo = ttm_to_amdgpu_bo(tbo);

	if (bo->tbo.pin_count > 0)
		amdgpu_bo_subtract_pin_size(bo);

	amdgpu_bo_kunmap(bo);

	if (bo->tbo.base.import_attach)
		drm_prime_gem_destroy(&bo->tbo.base, bo->tbo.sg);
	drm_gem_object_release(&bo->tbo.base);
	/* in case amdgpu_device_recover_vram got NULL of bo->parent */
	if (!list_empty(&bo->shadow_list)) {
		mutex_lock(&adev->shadow_list_lock);
		list_del_init(&bo->shadow_list);
		mutex_unlock(&adev->shadow_list_lock);
	}
	amdgpu_bo_unref(&bo->parent);

	kfree(bo->metadata);
	kfree(bo);
}

/**
 * amdgpu_bo_is_amdgpu_bo - check if the buffer object is an &amdgpu_bo
 * @bo: buffer object to be checked
 *
 * Uses destroy function associated with the object to determine if this is
 * an &amdgpu_bo.
 *
 * Returns:
 * true if the object belongs to &amdgpu_bo, false if not.
 */
bool amdgpu_bo_is_amdgpu_bo(struct ttm_buffer_object *bo)
{
	if (bo->destroy == &amdgpu_bo_destroy)
		return true;
	return false;
}

/**
 * amdgpu_bo_placement_from_domain - set buffer's placement
 * @abo: &amdgpu_bo buffer object whose placement is to be set
 * @domain: requested domain
 *
 * Sets buffer's placement according to requested domain and the buffer's
 * flags.
 */
void amdgpu_bo_placement_from_domain(struct amdgpu_bo *abo, u32 domain)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(abo->tbo.bdev);
	struct ttm_placement *placement = &abo->placement;
	struct ttm_place *places = abo->placements;
	u64 flags = abo->flags;
	u32 c = 0;

	if (domain & AMDGPU_GEM_DOMAIN_VRAM) {
		unsigned visible_pfn = adev->gmc.visible_vram_size >> PAGE_SHIFT;

		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = TTM_PL_VRAM;
		places[c].flags = TTM_PL_FLAG_WC | TTM_PL_FLAG_UNCACHED;

		if (flags & AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED)
			places[c].lpfn = visible_pfn;
		else
			places[c].flags |= TTM_PL_FLAG_TOPDOWN;

		if (flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS)
			places[c].flags |= TTM_PL_FLAG_CONTIGUOUS;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GTT) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = TTM_PL_TT;
		places[c].flags = 0;
		if (flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC)
			places[c].flags |= TTM_PL_FLAG_WC |
				TTM_PL_FLAG_UNCACHED;
		else
			places[c].flags |= TTM_PL_FLAG_CACHED;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_CPU) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = TTM_PL_SYSTEM;
		places[c].flags = 0;
		if (flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC)
			places[c].flags |= TTM_PL_FLAG_WC |
				TTM_PL_FLAG_UNCACHED;
		else
			places[c].flags |= TTM_PL_FLAG_CACHED;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GDS) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_GDS;
		places[c].flags = TTM_PL_FLAG_UNCACHED;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_GWS) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_GWS;
		places[c].flags = TTM_PL_FLAG_UNCACHED;
		c++;
	}

	if (domain & AMDGPU_GEM_DOMAIN_OA) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = AMDGPU_PL_OA;
		places[c].flags = TTM_PL_FLAG_UNCACHED;
		c++;
	}

	if (!c) {
		places[c].fpfn = 0;
		places[c].lpfn = 0;
		places[c].mem_type = TTM_PL_SYSTEM;
		places[c].flags = TTM_PL_MASK_CACHING;
		c++;
	}

	BUG_ON(c >= AMDGPU_BO_MAX_PLACEMENTS);

	placement->num_placement = c;
	placement->placement = places;

	placement->num_busy_placement = c;
	placement->busy_placement = places;
}

/**
 * amdgpu_bo_create_reserved - create reserved BO for kernel use
 *
 * @adev: amdgpu device object
 * @size: size for the new BO
 * @align: alignment for the new BO
 * @domain: where to place it
 * @bo_ptr: used to initialize BOs in structures
 * @gpu_addr: GPU addr of the pinned BO
 * @cpu_addr: optional CPU address mapping
 *
 * Allocates and pins a BO for kernel internal use, and returns it still
 * reserved.
 *
 * Note: For bo_ptr new BO is only created if bo_ptr points to NULL.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int amdgpu_bo_create_reserved(struct amdgpu_device *adev,
			      unsigned long size, int align,
			      u32 domain, struct amdgpu_bo **bo_ptr,
			      u64 *gpu_addr, void **cpu_addr)
{
	struct amdgpu_bo_param bp;
	bool free = false;
	int r;

	if (!size) {
		amdgpu_bo_unref(bo_ptr);
		return 0;
	}

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.byte_align = align;
	bp.domain = domain;
	bp.flags = cpu_addr ? AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED
		: AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	bp.flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
	bp.type = ttm_bo_type_kernel;
	bp.resv = NULL;

	if (!*bo_ptr) {
		r = amdgpu_bo_create(adev, &bp, bo_ptr);
		if (r) {
			dev_err(adev->dev, "(%d) failed to allocate kernel bo\n",
				r);
			return r;
		}
		free = true;
	}

	r = amdgpu_bo_reserve(*bo_ptr, false);
	if (r) {
		dev_err(adev->dev, "(%d) failed to reserve kernel bo\n", r);
		goto error_free;
	}

	r = amdgpu_bo_pin(*bo_ptr, domain);
	if (r) {
		dev_err(adev->dev, "(%d) kernel bo pin failed\n", r);
		goto error_unreserve;
	}

	r = amdgpu_ttm_alloc_gart(&(*bo_ptr)->tbo);
	if (r) {
		dev_err(adev->dev, "%p bind failed\n", *bo_ptr);
		goto error_unpin;
	}

	if (gpu_addr)
		*gpu_addr = amdgpu_bo_gpu_offset(*bo_ptr);

	if (cpu_addr) {
		r = amdgpu_bo_kmap(*bo_ptr, cpu_addr);
		if (r) {
			dev_err(adev->dev, "(%d) kernel bo map failed\n", r);
			goto error_unpin;
		}
	}

	return 0;

error_unpin:
	amdgpu_bo_unpin(*bo_ptr);
error_unreserve:
	amdgpu_bo_unreserve(*bo_ptr);

error_free:
	if (free)
		amdgpu_bo_unref(bo_ptr);

	return r;
}

/**
 * amdgpu_bo_create_kernel - create BO for kernel use
 *
 * @adev: amdgpu device object
 * @size: size for the new BO
 * @align: alignment for the new BO
 * @domain: where to place it
 * @bo_ptr:  used to initialize BOs in structures
 * @gpu_addr: GPU addr of the pinned BO
 * @cpu_addr: optional CPU address mapping
 *
 * Allocates and pins a BO for kernel internal use.
 *
 * Note: For bo_ptr new BO is only created if bo_ptr points to NULL.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int amdgpu_bo_create_kernel(struct amdgpu_device *adev,
			    unsigned long size, int align,
			    u32 domain, struct amdgpu_bo **bo_ptr,
			    u64 *gpu_addr, void **cpu_addr)
{
	int r;

	r = amdgpu_bo_create_reserved(adev, size, align, domain, bo_ptr,
				      gpu_addr, cpu_addr);

	if (r)
		return r;

	if (*bo_ptr)
		amdgpu_bo_unreserve(*bo_ptr);

	return 0;
}

/**
 * amdgpu_bo_create_kernel_at - create BO for kernel use at specific location
 *
 * @adev: amdgpu device object
 * @offset: offset of the BO
 * @size: size of the BO
 * @domain: where to place it
 * @bo_ptr:  used to initialize BOs in structures
 * @cpu_addr: optional CPU address mapping
 *
 * Creates a kernel BO at a specific offset in the address space of the domain.
 *
 * Returns:
 * 0 on success, negative error code otherwise.
 */
int amdgpu_bo_create_kernel_at(struct amdgpu_device *adev,
			       uint64_t offset, uint64_t size, uint32_t domain,
			       struct amdgpu_bo **bo_ptr, void **cpu_addr)
{
	struct ttm_operation_ctx ctx = { false, false };
	unsigned int i;
	int r;

	offset &= PAGE_MASK;
	size = ALIGN(size, PAGE_SIZE);

	r = amdgpu_bo_create_reserved(adev, size, PAGE_SIZE, domain, bo_ptr,
				      NULL, cpu_addr);
	if (r)
		return r;

	if ((*bo_ptr) == NULL)
		return 0;

	/*
	 * Remove the original mem node and create a new one at the request
	 * position.
	 */
	if (cpu_addr)
		amdgpu_bo_kunmap(*bo_ptr);

	ttm_resource_free(&(*bo_ptr)->tbo, &(*bo_ptr)->tbo.mem);

	for (i = 0; i < (*bo_ptr)->placement.num_placement; ++i) {
		(*bo_ptr)->placements[i].fpfn = offset >> PAGE_SHIFT;
		(*bo_ptr)->placements[i].lpfn = (offset + size) >> PAGE_SHIFT;
	}
	r = ttm_bo_mem_space(&(*bo_ptr)->tbo, &(*bo_ptr)->placement,
			     &(*bo_ptr)->tbo.mem, &ctx);
	if (r)
		goto error;

	if (cpu_addr) {
		r = amdgpu_bo_kmap(*bo_ptr, cpu_addr);
		if (r)
			goto error;
	}

	amdgpu_bo_unreserve(*bo_ptr);
	return 0;

error:
	amdgpu_bo_unreserve(*bo_ptr);
	amdgpu_bo_unref(bo_ptr);
	return r;
}

/**
 * amdgpu_bo_free_kernel - free BO for kernel use
 *
 * @bo: amdgpu BO to free
 * @gpu_addr: pointer to where the BO's GPU memory space address was stored
 * @cpu_addr: pointer to where the BO's CPU memory space address was stored
 *
 * unmaps and unpin a BO for kernel internal use.
 */
void amdgpu_bo_free_kernel(struct amdgpu_bo **bo, u64 *gpu_addr,
			   void **cpu_addr)
{
	if (*bo == NULL)
		return;

	if (likely(amdgpu_bo_reserve(*bo, true) == 0)) {
		if (cpu_addr)
			amdgpu_bo_kunmap(*bo);

		amdgpu_bo_unpin(*bo);
		amdgpu_bo_unreserve(*bo);
	}
	amdgpu_bo_unref(bo);

	if (gpu_addr)
		*gpu_addr = 0;

	if (cpu_addr)
		*cpu_addr = NULL;
}

/* Validate bo size is bit bigger then the request domain */
static bool amdgpu_bo_validate_size(struct amdgpu_device *adev,
					  unsigned long size, u32 domain)
{
	struct ttm_resource_manager *man = NULL;

	/*
	 * If GTT is part of requested domains the check must succeed to
	 * allow fall back to GTT
	 */
	if (domain & AMDGPU_GEM_DOMAIN_GTT) {
		man = ttm_manager_type(&adev->mman.bdev, TTM_PL_TT);

		if (size < (man->size << PAGE_SHIFT))
			return true;
		else
			goto fail;
	}

	if (domain & AMDGPU_GEM_DOMAIN_VRAM) {
		man = ttm_manager_type(&adev->mman.bdev, TTM_PL_VRAM);

		if (size < (man->size << PAGE_SHIFT))
			return true;
		else
			goto fail;
	}


	/* TODO add more domains checks, such as AMDGPU_GEM_DOMAIN_CPU */
	return true;

fail:
	DRM_DEBUG("BO size %lu > total memory in domain: %llu\n", size,
		  man->size << PAGE_SHIFT);
	return false;
}

bool amdgpu_bo_support_uswc(u64 bo_flags)
{

#ifdef CONFIG_X86_32
	/* XXX: Write-combined CPU mappings of GTT seem broken on 32-bit
	 * See https://bugs.freedesktop.org/show_bug.cgi?id=84627
	 */
	return false;
#elif defined(CONFIG_X86) && !defined(CONFIG_X86_PAT)
	/* Don't try to enable write-combining when it can't work, or things
	 * may be slow
	 * See https://bugs.freedesktop.org/show_bug.cgi?id=88758
	 */

#ifndef CONFIG_COMPILE_TEST
#warning Please enable CONFIG_MTRR and CONFIG_X86_PAT for better performance \
	 thanks to write-combining
#endif

	if (bo_flags & AMDGPU_GEM_CREATE_CPU_GTT_USWC)
		DRM_INFO_ONCE("Please enable CONFIG_MTRR and CONFIG_X86_PAT for "
			      "better performance thanks to write-combining\n");
	return false;
#else
	/* For architectures that don't support WC memory,
	 * mask out the WC flag from the BO
	 */
	if (!drm_arch_can_wc_memory())
		return false;

	return true;
#endif
}

static int amdgpu_bo_do_create(struct amdgpu_device *adev,
			       struct amdgpu_bo_param *bp,
			       struct amdgpu_bo **bo_ptr)
{
	struct ttm_operation_ctx ctx = {
		.interruptible = (bp->type != ttm_bo_type_kernel),
		.no_wait_gpu = bp->no_wait_gpu,
		.resv = bp->resv,
		.flags = bp->type != ttm_bo_type_kernel ?
			TTM_OPT_FLAG_ALLOW_RES_EVICT : 0
	};
	struct amdgpu_bo *bo;
	unsigned long page_align, size = bp->size;
	size_t acc_size;
	int r;

	/* Note that GDS/GWS/OA allocates 1 page per byte/resource. */
	if (bp->domain & (AMDGPU_GEM_DOMAIN_GWS | AMDGPU_GEM_DOMAIN_OA)) {
		/* GWS and OA don't need any alignment. */
		page_align = bp->byte_align;
		size <<= PAGE_SHIFT;
	} else if (bp->domain & AMDGPU_GEM_DOMAIN_GDS) {
		/* Both size and alignment must be a multiple of 4. */
		page_align = ALIGN(bp->byte_align, 4);
		size = ALIGN(size, 4) << PAGE_SHIFT;
	} else {
		/* Memory should be aligned at least to a page size. */
		page_align = ALIGN(bp->byte_align, PAGE_SIZE) >> PAGE_SHIFT;
		size = ALIGN(size, PAGE_SIZE);
	}

	if (!amdgpu_bo_validate_size(adev, size, bp->domain))
		return -ENOMEM;

	*bo_ptr = NULL;

	acc_size = ttm_bo_dma_acc_size(&adev->mman.bdev, size,
				       sizeof(struct amdgpu_bo));

	bo = kzalloc(sizeof(struct amdgpu_bo), GFP_KERNEL);
	if (bo == NULL)
		return -ENOMEM;
	drm_gem_private_object_init(adev_to_drm(adev), &bo->tbo.base, size);
	INIT_LIST_HEAD(&bo->shadow_list);
	bo->vm_bo = NULL;
	bo->preferred_domains = bp->preferred_domain ? bp->preferred_domain :
		bp->domain;
	bo->allowed_domains = bo->preferred_domains;
	if (bp->type != ttm_bo_type_kernel &&
	    bo->allowed_domains == AMDGPU_GEM_DOMAIN_VRAM)
		bo->allowed_domains |= AMDGPU_GEM_DOMAIN_GTT;

	bo->flags = bp->flags;

	if (!amdgpu_bo_support_uswc(bo->flags))
		bo->flags &= ~AMDGPU_GEM_CREATE_CPU_GTT_USWC;

	bo->tbo.bdev = &adev->mman.bdev;
	if (bp->domain & (AMDGPU_GEM_DOMAIN_GWS | AMDGPU_GEM_DOMAIN_OA |
			  AMDGPU_GEM_DOMAIN_GDS))
		amdgpu_bo_placement_from_domain(bo, AMDGPU_GEM_DOMAIN_CPU);
	else
		amdgpu_bo_placement_from_domain(bo, bp->domain);
	if (bp->type == ttm_bo_type_kernel)
		bo->tbo.priority = 1;

	r = ttm_bo_init_reserved(&adev->mman.bdev, &bo->tbo, size, bp->type,
				 &bo->placement, page_align, &ctx, acc_size,
				 NULL, bp->resv, &amdgpu_bo_destroy);
	if (unlikely(r != 0))
		return r;

	if (!amdgpu_gmc_vram_full_visible(&adev->gmc) &&
	    bo->tbo.mem.mem_type == TTM_PL_VRAM &&
	    bo->tbo.mem.start < adev->gmc.visible_vram_size >> PAGE_SHIFT)
		amdgpu_cs_report_moved_bytes(adev, ctx.bytes_moved,
					     ctx.bytes_moved);
	else
		amdgpu_cs_report_moved_bytes(adev, ctx.bytes_moved, 0);

	if (bp->flags & AMDGPU_GEM_CREATE_VRAM_CLEARED &&
	    bo->tbo.mem.mem_type == TTM_PL_VRAM) {
		struct dma_fence *fence;

		r = amdgpu_fill_buffer(bo, 0, bo->tbo.base.resv, &fence);
		if (unlikely(r))
			goto fail_unreserve;

		amdgpu_bo_fence(bo, fence, false);
		dma_fence_put(bo->tbo.moving);
		bo->tbo.moving = dma_fence_get(fence);
		dma_fence_put(fence);
	}
	if (!bp->resv)
		amdgpu_bo_unreserve(bo);
	*bo_ptr = bo;

	trace_amdgpu_bo_create(bo);

	/* Treat CPU_ACCESS_REQUIRED only as a hint if given by UMD */
	if (bp->type == ttm_bo_type_device)
		bo->flags &= ~AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	return 0;

fail_unreserve:
	if (!bp->resv)
		dma_resv_unlock(bo->tbo.base.resv);
	amdgpu_bo_unref(&bo);
	return r;
}

static int amdgpu_bo_create_shadow(struct amdgpu_device *adev,
				   unsigned long size,
				   struct amdgpu_bo *bo)
{
	struct amdgpu_bo_param bp;
	int r;

	if (bo->shadow)
		return 0;

	memset(&bp, 0, sizeof(bp));
	bp.size = size;
	bp.domain = AMDGPU_GEM_DOMAIN_GTT;
	bp.flags = AMDGPU_GEM_CREATE_CPU_GTT_USWC |
		AMDGPU_GEM_CREATE_SHADOW;
	bp.type = ttm_bo_type_kernel;
	bp.resv = bo->tbo.base.resv;

	r = amdgpu_bo_do_create(adev, &bp, &bo->shadow);
	if (!r) {
		bo->shadow->parent = amdgpu_bo_ref(bo);
		mutex_lock(&adev->shadow_list_lock);
		list_add_tail(&bo->shadow->shadow_list, &adev->shadow_list);
		mutex_unlock(&adev->shadow_list_lock);
	}

	return r;
}

/**
 * amdgpu_bo_create - create an &amdgpu_bo buffer object
 * @adev: amdgpu device object
 * @bp: parameters to be used for the buffer object
 * @bo_ptr: pointer to the buffer object pointer
 *
 * Creates an &amdgpu_bo buffer object; and if requested, also creates a
 * shadow object.
 * Shadow object is used to backup the original buffer object, and is always
 * in GTT.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_create(struct amdgpu_device *adev,
		     struct amdgpu_bo_param *bp,
		     struct amdgpu_bo **bo_ptr)
{
	u64 flags = bp->flags;
	int r;

	bp->flags = bp->flags & ~AMDGPU_GEM_CREATE_SHADOW;
	r = amdgpu_bo_do_create(adev, bp, bo_ptr);
	if (r)
		return r;

	if ((flags & AMDGPU_GEM_CREATE_SHADOW) && !(adev->flags & AMD_IS_APU)) {
		if (!bp->resv)
			WARN_ON(dma_resv_lock((*bo_ptr)->tbo.base.resv,
							NULL));

		r = amdgpu_bo_create_shadow(adev, bp->size, *bo_ptr);

		if (!bp->resv)
			dma_resv_unlock((*bo_ptr)->tbo.base.resv);

		if (r)
			amdgpu_bo_unref(bo_ptr);
	}

	return r;
}

/**
 * amdgpu_bo_validate - validate an &amdgpu_bo buffer object
 * @bo: pointer to the buffer object
 *
 * Sets placement according to domain; and changes placement and caching
 * policy of the buffer object according to the placement.
 * This is used for validating shadow bos.  It calls ttm_bo_validate() to
 * make sure the buffer is resident where it needs to be.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_validate(struct amdgpu_bo *bo)
{
	struct ttm_operation_ctx ctx = { false, false };
	uint32_t domain;
	int r;

	if (bo->tbo.pin_count)
		return 0;

	domain = bo->preferred_domains;

retry:
	amdgpu_bo_placement_from_domain(bo, domain);
	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (unlikely(r == -ENOMEM) && domain != bo->allowed_domains) {
		domain = bo->allowed_domains;
		goto retry;
	}

	return r;
}

/**
 * amdgpu_bo_restore_shadow - restore an &amdgpu_bo shadow
 *
 * @shadow: &amdgpu_bo shadow to be restored
 * @fence: dma_fence associated with the operation
 *
 * Copies a buffer object's shadow content back to the object.
 * This is used for recovering a buffer from its shadow in case of a gpu
 * reset where vram context may be lost.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_restore_shadow(struct amdgpu_bo *shadow, struct dma_fence **fence)

{
	struct amdgpu_device *adev = amdgpu_ttm_adev(shadow->tbo.bdev);
	struct amdgpu_ring *ring = adev->mman.buffer_funcs_ring;
	uint64_t shadow_addr, parent_addr;

	shadow_addr = amdgpu_bo_gpu_offset(shadow);
	parent_addr = amdgpu_bo_gpu_offset(shadow->parent);

	return amdgpu_copy_buffer(ring, shadow_addr, parent_addr,
				  amdgpu_bo_size(shadow), NULL, fence,
				  true, false, false);
}

/**
 * amdgpu_bo_kmap - map an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be mapped
 * @ptr: kernel virtual address to be returned
 *
 * Calls ttm_bo_kmap() to set up the kernel virtual mapping; calls
 * amdgpu_bo_kptr() to get the kernel virtual address.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_kmap(struct amdgpu_bo *bo, void **ptr)
{
	void *kptr;
	long r;

	if (bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS)
		return -EPERM;

	kptr = amdgpu_bo_kptr(bo);
	if (kptr) {
		if (ptr)
			*ptr = kptr;
		return 0;
	}

	r = dma_resv_wait_timeout_rcu(bo->tbo.base.resv, false, false,
						MAX_SCHEDULE_TIMEOUT);
	if (r < 0)
		return r;

	r = ttm_bo_kmap(&bo->tbo, 0, bo->tbo.num_pages, &bo->kmap);
	if (r)
		return r;

	if (ptr)
		*ptr = amdgpu_bo_kptr(bo);

	return 0;
}

/**
 * amdgpu_bo_kptr - returns a kernel virtual address of the buffer object
 * @bo: &amdgpu_bo buffer object
 *
 * Calls ttm_kmap_obj_virtual() to get the kernel virtual address
 *
 * Returns:
 * the virtual address of a buffer object area.
 */
void *amdgpu_bo_kptr(struct amdgpu_bo *bo)
{
	bool is_iomem;

	return ttm_kmap_obj_virtual(&bo->kmap, &is_iomem);
}

/**
 * amdgpu_bo_kunmap - unmap an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be unmapped
 *
 * Unmaps a kernel map set up by amdgpu_bo_kmap().
 */
void amdgpu_bo_kunmap(struct amdgpu_bo *bo)
{
	if (bo->kmap.bo)
		ttm_bo_kunmap(&bo->kmap);
}

/**
 * amdgpu_bo_ref - reference an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object
 *
 * References the contained &ttm_buffer_object.
 *
 * Returns:
 * a refcounted pointer to the &amdgpu_bo buffer object.
 */
struct amdgpu_bo *amdgpu_bo_ref(struct amdgpu_bo *bo)
{
	if (bo == NULL)
		return NULL;

	ttm_bo_get(&bo->tbo);
	return bo;
}

/**
 * amdgpu_bo_unref - unreference an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object
 *
 * Unreferences the contained &ttm_buffer_object and clear the pointer
 */
void amdgpu_bo_unref(struct amdgpu_bo **bo)
{
	struct ttm_buffer_object *tbo;

	if ((*bo) == NULL)
		return;

	tbo = &((*bo)->tbo);
	ttm_bo_put(tbo);
	*bo = NULL;
}

/**
 * amdgpu_bo_pin_restricted - pin an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be pinned
 * @domain: domain to be pinned to
 * @min_offset: the start of requested address range
 * @max_offset: the end of requested address range
 *
 * Pins the buffer object according to requested domain and address range. If
 * the memory is unbound gart memory, binds the pages into gart table. Adjusts
 * pin_count and pin_size accordingly.
 *
 * Pinning means to lock pages in memory along with keeping them at a fixed
 * offset. It is required when a buffer can not be moved, for example, when
 * a display buffer is being scanned out.
 *
 * Compared with amdgpu_bo_pin(), this function gives more flexibility on
 * where to pin a buffer if there are specific restrictions on where a buffer
 * must be located.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_pin_restricted(struct amdgpu_bo *bo, u32 domain,
			     u64 min_offset, u64 max_offset)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	struct ttm_operation_ctx ctx = { false, false };
	int r, i;

	if (amdgpu_ttm_tt_get_usermm(bo->tbo.ttm))
		return -EPERM;

	if (WARN_ON_ONCE(min_offset > max_offset))
		return -EINVAL;

	/* A shared bo cannot be migrated to VRAM */
	if (bo->prime_shared_count) {
		if (domain & AMDGPU_GEM_DOMAIN_GTT)
			domain = AMDGPU_GEM_DOMAIN_GTT;
		else
			return -EINVAL;
	}

	/* This assumes only APU display buffers are pinned with (VRAM|GTT).
	 * See function amdgpu_display_supported_domains()
	 */
	domain = amdgpu_bo_get_preferred_pin_domain(adev, domain);

	if (bo->tbo.pin_count) {
		uint32_t mem_type = bo->tbo.mem.mem_type;

		if (!(domain & amdgpu_mem_type_to_domain(mem_type)))
			return -EINVAL;

		ttm_bo_pin(&bo->tbo);

		if (max_offset != 0) {
			u64 domain_start = amdgpu_ttm_domain_start(adev,
								   mem_type);
			WARN_ON_ONCE(max_offset <
				     (amdgpu_bo_gpu_offset(bo) - domain_start));
		}

		return 0;
	}

	if (bo->tbo.base.import_attach)
		dma_buf_pin(bo->tbo.base.import_attach);

	bo->flags |= AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS;
	/* force to pin into visible video ram */
	if (!(bo->flags & AMDGPU_GEM_CREATE_NO_CPU_ACCESS))
		bo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	amdgpu_bo_placement_from_domain(bo, domain);
	for (i = 0; i < bo->placement.num_placement; i++) {
		unsigned fpfn, lpfn;

		fpfn = min_offset >> PAGE_SHIFT;
		lpfn = max_offset >> PAGE_SHIFT;

		if (fpfn > bo->placements[i].fpfn)
			bo->placements[i].fpfn = fpfn;
		if (!bo->placements[i].lpfn ||
		    (lpfn && lpfn < bo->placements[i].lpfn))
			bo->placements[i].lpfn = lpfn;
	}

	r = ttm_bo_validate(&bo->tbo, &bo->placement, &ctx);
	if (unlikely(r)) {
		dev_err(adev->dev, "%p pin failed\n", bo);
		goto error;
	}

	ttm_bo_pin(&bo->tbo);

	domain = amdgpu_mem_type_to_domain(bo->tbo.mem.mem_type);
	if (domain == AMDGPU_GEM_DOMAIN_VRAM) {
		atomic64_add(amdgpu_bo_size(bo), &adev->vram_pin_size);
		atomic64_add(amdgpu_vram_mgr_bo_visible_size(bo),
			     &adev->visible_pin_size);
	} else if (domain == AMDGPU_GEM_DOMAIN_GTT) {
		atomic64_add(amdgpu_bo_size(bo), &adev->gart_pin_size);
	}

error:
	return r;
}

/**
 * amdgpu_bo_pin - pin an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be pinned
 * @domain: domain to be pinned to
 *
 * A simple wrapper to amdgpu_bo_pin_restricted().
 * Provides a simpler API for buffers that do not have any strict restrictions
 * on where a buffer must be located.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_pin(struct amdgpu_bo *bo, u32 domain)
{
	return amdgpu_bo_pin_restricted(bo, domain, 0, 0);
}

/**
 * amdgpu_bo_unpin - unpin an &amdgpu_bo buffer object
 * @bo: &amdgpu_bo buffer object to be unpinned
 *
 * Decreases the pin_count, and clears the flags if pin_count reaches 0.
 * Changes placement and pin size accordingly.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
void amdgpu_bo_unpin(struct amdgpu_bo *bo)
{
	ttm_bo_unpin(&bo->tbo);
	if (bo->tbo.pin_count)
		return;

	amdgpu_bo_subtract_pin_size(bo);

	if (bo->tbo.base.import_attach)
		dma_buf_unpin(bo->tbo.base.import_attach);
}

/**
 * amdgpu_bo_evict_vram - evict VRAM buffers
 * @adev: amdgpu device object
 *
 * Evicts all VRAM buffers on the lru list of the memory type.
 * Mainly used for evicting vram at suspend time.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_evict_vram(struct amdgpu_device *adev)
{
	/* late 2.6.33 fix IGP hibernate - we need pm ops to do this correct */
#ifndef CONFIG_HIBERNATION
	if (adev->flags & AMD_IS_APU) {
		/* Useless to evict on IGP chips */
		return 0;
	}
#endif
	return ttm_bo_evict_mm(&adev->mman.bdev, TTM_PL_VRAM);
}

static const char *amdgpu_vram_names[] = {
	"UNKNOWN",
	"GDDR1",
	"DDR2",
	"GDDR3",
	"GDDR4",
	"GDDR5",
	"HBM",
	"DDR3",
	"DDR4",
	"GDDR6",
};

/**
 * amdgpu_bo_init - initialize memory manager
 * @adev: amdgpu device object
 *
 * Calls amdgpu_ttm_init() to initialize amdgpu memory manager.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_init(struct amdgpu_device *adev)
{
	/* reserve PAT memory space to WC for VRAM */
	arch_io_reserve_memtype_wc(adev->gmc.aper_base,
				   adev->gmc.aper_size);

	/* Add an MTRR for the VRAM */
	adev->gmc.vram_mtrr = arch_phys_wc_add(adev->gmc.aper_base,
					      adev->gmc.aper_size);
	DRM_INFO("Detected VRAM RAM=%lluM, BAR=%lluM\n",
		 adev->gmc.mc_vram_size >> 20,
		 (unsigned long long)adev->gmc.aper_size >> 20);
	DRM_INFO("RAM width %dbits %s\n",
		 adev->gmc.vram_width, amdgpu_vram_names[adev->gmc.vram_type]);
	return amdgpu_ttm_init(adev);
}

/**
 * amdgpu_bo_late_init - late init
 * @adev: amdgpu device object
 *
 * Calls amdgpu_ttm_late_init() to free resources used earlier during
 * initialization.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_late_init(struct amdgpu_device *adev)
{
	amdgpu_ttm_late_init(adev);

	return 0;
}

/**
 * amdgpu_bo_fini - tear down memory manager
 * @adev: amdgpu device object
 *
 * Reverses amdgpu_bo_init() to tear down memory manager.
 */
void amdgpu_bo_fini(struct amdgpu_device *adev)
{
	amdgpu_ttm_fini(adev);
	arch_phys_wc_del(adev->gmc.vram_mtrr);
	arch_io_free_memtype_wc(adev->gmc.aper_base, adev->gmc.aper_size);
}

/**
 * amdgpu_bo_fbdev_mmap - mmap fbdev memory
 * @bo: &amdgpu_bo buffer object
 * @vma: vma as input from the fbdev mmap method
 *
 * Calls ttm_fbdev_mmap() to mmap fbdev memory if it is backed by a bo.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_fbdev_mmap(struct amdgpu_bo *bo,
			     struct vm_area_struct *vma)
{
	if (vma->vm_pgoff != 0)
		return -EACCES;

	return ttm_bo_mmap_obj(vma, &bo->tbo);
}

/**
 * amdgpu_bo_set_tiling_flags - set tiling flags
 * @bo: &amdgpu_bo buffer object
 * @tiling_flags: new flags
 *
 * Sets buffer object's tiling flags with the new one. Used by GEM ioctl or
 * kernel driver to set the tiling flags on a buffer.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_set_tiling_flags(struct amdgpu_bo *bo, u64 tiling_flags)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	if (adev->family <= AMDGPU_FAMILY_CZ &&
	    AMDGPU_TILING_GET(tiling_flags, TILE_SPLIT) > 6)
		return -EINVAL;

	bo->tiling_flags = tiling_flags;
	return 0;
}

/**
 * amdgpu_bo_get_tiling_flags - get tiling flags
 * @bo: &amdgpu_bo buffer object
 * @tiling_flags: returned flags
 *
 * Gets buffer object's tiling flags. Used by GEM ioctl or kernel driver to
 * set the tiling flags on a buffer.
 */
void amdgpu_bo_get_tiling_flags(struct amdgpu_bo *bo, u64 *tiling_flags)
{
	dma_resv_assert_held(bo->tbo.base.resv);

	if (tiling_flags)
		*tiling_flags = bo->tiling_flags;
}

/**
 * amdgpu_bo_set_metadata - set metadata
 * @bo: &amdgpu_bo buffer object
 * @metadata: new metadata
 * @metadata_size: size of the new metadata
 * @flags: flags of the new metadata
 *
 * Sets buffer object's metadata, its size and flags.
 * Used via GEM ioctl.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_set_metadata (struct amdgpu_bo *bo, void *metadata,
			    uint32_t metadata_size, uint64_t flags)
{
	void *buffer;

	if (!metadata_size) {
		if (bo->metadata_size) {
			kfree(bo->metadata);
			bo->metadata = NULL;
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

/**
 * amdgpu_bo_get_metadata - get metadata
 * @bo: &amdgpu_bo buffer object
 * @buffer: returned metadata
 * @buffer_size: size of the buffer
 * @metadata_size: size of the returned metadata
 * @flags: flags of the returned metadata
 *
 * Gets buffer object's metadata, its size and flags. buffer_size shall not be
 * less than metadata_size.
 * Used via GEM ioctl.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
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

/**
 * amdgpu_bo_move_notify - notification about a memory move
 * @bo: pointer to a buffer object
 * @evict: if this move is evicting the buffer from the graphics address space
 * @new_mem: new information of the bufer object
 *
 * Marks the corresponding &amdgpu_bo buffer object as invalid, also performs
 * bookkeeping.
 * TTM driver callback which is called when ttm moves a buffer.
 */
void amdgpu_bo_move_notify(struct ttm_buffer_object *bo,
			   bool evict,
			   struct ttm_resource *new_mem)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);
	struct amdgpu_bo *abo;
	struct ttm_resource *old_mem = &bo->mem;

	if (!amdgpu_bo_is_amdgpu_bo(bo))
		return;

	abo = ttm_to_amdgpu_bo(bo);
	amdgpu_vm_bo_invalidate(adev, abo, evict);

	amdgpu_bo_kunmap(abo);

	if (abo->tbo.base.dma_buf && !abo->tbo.base.import_attach &&
	    bo->mem.mem_type != TTM_PL_SYSTEM)
		dma_buf_move_notify(abo->tbo.base.dma_buf);

	/* remember the eviction */
	if (evict)
		atomic64_inc(&adev->num_evictions);

	/* update statistics */
	if (!new_mem)
		return;

	/* move_notify is called before move happens */
	trace_amdgpu_bo_move(abo, new_mem->mem_type, old_mem->mem_type);
}

/**
 * amdgpu_bo_release_notify - notification about a BO being released
 * @bo: pointer to a buffer object
 *
 * Wipes VRAM buffers whose contents should not be leaked before the
 * memory is released.
 */
void amdgpu_bo_release_notify(struct ttm_buffer_object *bo)
{
	struct dma_fence *fence = NULL;
	struct amdgpu_bo *abo;
	int r;

	if (!amdgpu_bo_is_amdgpu_bo(bo))
		return;

	abo = ttm_to_amdgpu_bo(bo);

	if (abo->kfd_bo)
		amdgpu_amdkfd_unreserve_memory_limit(abo);

	/* We only remove the fence if the resv has individualized. */
	WARN_ON_ONCE(bo->type == ttm_bo_type_kernel
			&& bo->base.resv != &bo->base._resv);
	if (bo->base.resv == &bo->base._resv)
		amdgpu_amdkfd_remove_fence_on_pt_pd_bos(abo);

	if (bo->mem.mem_type != TTM_PL_VRAM || !bo->mem.mm_node ||
	    !(abo->flags & AMDGPU_GEM_CREATE_VRAM_WIPE_ON_RELEASE))
		return;

	dma_resv_lock(bo->base.resv, NULL);

	r = amdgpu_fill_buffer(abo, AMDGPU_POISON, bo->base.resv, &fence);
	if (!WARN_ON(r)) {
		amdgpu_bo_fence(abo, fence, false);
		dma_fence_put(fence);
	}

	dma_resv_unlock(bo->base.resv);
}

/**
 * amdgpu_bo_fault_reserve_notify - notification about a memory fault
 * @bo: pointer to a buffer object
 *
 * Notifies the driver we are taking a fault on this BO and have reserved it,
 * also performs bookkeeping.
 * TTM driver callback for dealing with vm faults.
 *
 * Returns:
 * 0 for success or a negative error code on failure.
 */
int amdgpu_bo_fault_reserve_notify(struct ttm_buffer_object *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->bdev);
	struct ttm_operation_ctx ctx = { false, false };
	struct amdgpu_bo *abo;
	unsigned long offset, size;
	int r;

	if (!amdgpu_bo_is_amdgpu_bo(bo))
		return 0;

	abo = ttm_to_amdgpu_bo(bo);

	/* Remember that this BO was accessed by the CPU */
	abo->flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;

	if (bo->mem.mem_type != TTM_PL_VRAM)
		return 0;

	size = bo->mem.num_pages << PAGE_SHIFT;
	offset = bo->mem.start << PAGE_SHIFT;
	if ((offset + size) <= adev->gmc.visible_vram_size)
		return 0;

	/* Can't move a pinned BO to visible VRAM */
	if (abo->tbo.pin_count > 0)
		return -EINVAL;

	/* hurrah the memory is not visible ! */
	atomic64_inc(&adev->num_vram_cpu_page_faults);
	amdgpu_bo_placement_from_domain(abo, AMDGPU_GEM_DOMAIN_VRAM |
					AMDGPU_GEM_DOMAIN_GTT);

	/* Avoid costly evictions; only set GTT as a busy placement */
	abo->placement.num_busy_placement = 1;
	abo->placement.busy_placement = &abo->placements[1];

	r = ttm_bo_validate(bo, &abo->placement, &ctx);
	if (unlikely(r != 0))
		return r;

	offset = bo->mem.start << PAGE_SHIFT;
	/* this should never happen */
	if (bo->mem.mem_type == TTM_PL_VRAM &&
	    (offset + size) > adev->gmc.visible_vram_size)
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
void amdgpu_bo_fence(struct amdgpu_bo *bo, struct dma_fence *fence,
		     bool shared)
{
	struct dma_resv *resv = bo->tbo.base.resv;

	if (shared)
		dma_resv_add_shared_fence(resv, fence);
	else
		dma_resv_add_excl_fence(resv, fence);
}

/**
 * amdgpu_bo_sync_wait_resv - Wait for BO reservation fences
 *
 * @adev: amdgpu device pointer
 * @resv: reservation object to sync to
 * @sync_mode: synchronization mode
 * @owner: fence owner
 * @intr: Whether the wait is interruptible
 *
 * Extract the fences from the reservation object and waits for them to finish.
 *
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_bo_sync_wait_resv(struct amdgpu_device *adev, struct dma_resv *resv,
			     enum amdgpu_sync_mode sync_mode, void *owner,
			     bool intr)
{
	struct amdgpu_sync sync;
	int r;

	amdgpu_sync_create(&sync);
	amdgpu_sync_resv(adev, &sync, resv, sync_mode, owner);
	r = amdgpu_sync_wait(&sync, intr);
	amdgpu_sync_free(&sync);
	return r;
}

/**
 * amdgpu_bo_sync_wait - Wrapper for amdgpu_bo_sync_wait_resv
 * @bo: buffer object to wait for
 * @owner: fence owner
 * @intr: Whether the wait is interruptible
 *
 * Wrapper to wait for fences in a BO.
 * Returns:
 * 0 on success, errno otherwise.
 */
int amdgpu_bo_sync_wait(struct amdgpu_bo *bo, void *owner, bool intr)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);

	return amdgpu_bo_sync_wait_resv(adev, bo->tbo.base.resv,
					AMDGPU_SYNC_NE_OWNER, owner, intr);
}

/**
 * amdgpu_bo_gpu_offset - return GPU offset of bo
 * @bo:	amdgpu object for which we query the offset
 *
 * Note: object should either be pinned or reserved when calling this
 * function, it might be useful to add check for this for debugging.
 *
 * Returns:
 * current GPU offset of the object.
 */
u64 amdgpu_bo_gpu_offset(struct amdgpu_bo *bo)
{
	WARN_ON_ONCE(bo->tbo.mem.mem_type == TTM_PL_SYSTEM);
	WARN_ON_ONCE(!dma_resv_is_locked(bo->tbo.base.resv) &&
		     !bo->tbo.pin_count && bo->tbo.type != ttm_bo_type_kernel);
	WARN_ON_ONCE(bo->tbo.mem.start == AMDGPU_BO_INVALID_OFFSET);
	WARN_ON_ONCE(bo->tbo.mem.mem_type == TTM_PL_VRAM &&
		     !(bo->flags & AMDGPU_GEM_CREATE_VRAM_CONTIGUOUS));

	return amdgpu_bo_gpu_offset_no_check(bo);
}

/**
 * amdgpu_bo_gpu_offset_no_check - return GPU offset of bo
 * @bo:	amdgpu object for which we query the offset
 *
 * Returns:
 * current GPU offset of the object without raising warnings.
 */
u64 amdgpu_bo_gpu_offset_no_check(struct amdgpu_bo *bo)
{
	struct amdgpu_device *adev = amdgpu_ttm_adev(bo->tbo.bdev);
	uint64_t offset;

	offset = (bo->tbo.mem.start << PAGE_SHIFT) +
		 amdgpu_ttm_domain_start(adev, bo->tbo.mem.mem_type);

	return amdgpu_gmc_sign_extend(offset);
}

/**
 * amdgpu_bo_get_preferred_pin_domain - get preferred domain for scanout
 * @adev: amdgpu device object
 * @domain: allowed :ref:`memory domains <amdgpu_memory_domains>`
 *
 * Returns:
 * Which of the allowed domains is preferred for pinning the BO for scanout.
 */
uint32_t amdgpu_bo_get_preferred_pin_domain(struct amdgpu_device *adev,
					    uint32_t domain)
{
	if (domain == (AMDGPU_GEM_DOMAIN_VRAM | AMDGPU_GEM_DOMAIN_GTT)) {
		domain = AMDGPU_GEM_DOMAIN_VRAM;
		if (adev->gmc.real_vram_size <= AMDGPU_SG_THRESHOLD)
			domain = AMDGPU_GEM_DOMAIN_GTT;
	}
	return domain;
}
