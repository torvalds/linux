// SPDX-License-Identifier: MIT
/*
 * Copyright © 2022 Intel Corporation
 */

#include "intel_ggtt_gmch.h"

#include <drm/intel-gtt.h>
#include <drm/i915_drm.h>

#include <linux/agp_backend.h>

#include "i915_drv.h"
#include "i915_utils.h"
#include "intel_gtt.h"
#include "intel_gt_regs.h"
#include "intel_gt.h"

static void gmch_ggtt_insert_page(struct i915_address_space *vm,
				  dma_addr_t addr,
				  u64 offset,
				  enum i915_cache_level cache_level,
				  u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gmch_gtt_insert_page(addr, offset >> PAGE_SHIFT, flags);
}

static void gmch_ggtt_insert_entries(struct i915_address_space *vm,
				     struct i915_vma_resource *vma_res,
				     enum i915_cache_level cache_level,
				     u32 unused)
{
	unsigned int flags = (cache_level == I915_CACHE_NONE) ?
		AGP_USER_MEMORY : AGP_USER_CACHED_MEMORY;

	intel_gmch_gtt_insert_sg_entries(vma_res->bi.pages, vma_res->start >> PAGE_SHIFT,
					 flags);
}

static void gmch_ggtt_invalidate(struct i915_ggtt *ggtt)
{
	intel_gmch_gtt_flush();
}

static void gmch_ggtt_clear_range(struct i915_address_space *vm,
				  u64 start, u64 length)
{
	intel_gmch_gtt_clear_range(start >> PAGE_SHIFT, length >> PAGE_SHIFT);
}

static void gmch_ggtt_remove(struct i915_address_space *vm)
{
	intel_gmch_remove();
}

/*
 * Certain Gen5 chipsets require idling the GPU before unmapping anything from
 * the GTT when VT-d is enabled.
 */
static bool needs_idle_maps(struct drm_i915_private *i915)
{
	/*
	 * Query intel_iommu to see if we need the workaround. Presumably that
	 * was loaded first.
	 */
	if (!i915_vtd_active(i915))
		return false;

	if (GRAPHICS_VER(i915) == 5 && IS_MOBILE(i915))
		return true;

	return false;
}

int intel_ggtt_gmch_probe(struct i915_ggtt *ggtt)
{
	struct drm_i915_private *i915 = ggtt->vm.i915;
	phys_addr_t gmadr_base;
	int ret;

	ret = intel_gmch_probe(i915->bridge_dev, to_pci_dev(i915->drm.dev), NULL);
	if (!ret) {
		drm_err(&i915->drm, "failed to set up gmch\n");
		return -EIO;
	}

	intel_gmch_gtt_get(&ggtt->vm.total, &gmadr_base, &ggtt->mappable_end);

	ggtt->gmadr =
		(struct resource)DEFINE_RES_MEM(gmadr_base, ggtt->mappable_end);

	ggtt->vm.alloc_pt_dma = alloc_pt_dma;
	ggtt->vm.alloc_scratch_dma = alloc_pt_dma;

	if (needs_idle_maps(i915)) {
		drm_notice(&i915->drm,
			   "Flushing DMA requests before IOMMU unmaps; performance may be degraded\n");
		ggtt->do_idle_maps = true;
	}

	ggtt->vm.insert_page = gmch_ggtt_insert_page;
	ggtt->vm.insert_entries = gmch_ggtt_insert_entries;
	ggtt->vm.clear_range = gmch_ggtt_clear_range;
	ggtt->vm.cleanup = gmch_ggtt_remove;

	ggtt->invalidate = gmch_ggtt_invalidate;

	ggtt->vm.vma_ops.bind_vma    = intel_ggtt_bind_vma;
	ggtt->vm.vma_ops.unbind_vma  = intel_ggtt_unbind_vma;

	if (unlikely(ggtt->do_idle_maps))
		drm_notice(&i915->drm,
			   "Applying Ironlake quirks for intel_iommu\n");

	return 0;
}

int intel_ggtt_gmch_enable_hw(struct drm_i915_private *i915)
{
	if (!intel_gmch_enable_gtt())
		return -EIO;

	return 0;
}

void intel_ggtt_gmch_flush(void)
{
	intel_gmch_gtt_flush();
}
