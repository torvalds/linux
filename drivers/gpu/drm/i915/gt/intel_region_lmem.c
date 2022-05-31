// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_reg.h"
#include "intel_memory_region.h"
#include "intel_region_lmem.h"
#include "intel_region_ttm.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_regs.h"

static int
region_lmem_release(struct intel_memory_region *mem)
{
	int ret;

	ret = intel_region_ttm_fini(mem);
	io_mapping_fini(&mem->iomap);

	return ret;
}

static int
region_lmem_init(struct intel_memory_region *mem)
{
	int ret;

	if (!io_mapping_init_wc(&mem->iomap,
				mem->io_start,
				mem->io_size))
		return -EIO;

	ret = intel_region_ttm_init(mem);
	if (ret)
		goto out_no_buddy;

	return 0;

out_no_buddy:
	io_mapping_fini(&mem->iomap);

	return ret;
}

static const struct intel_memory_region_ops intel_region_lmem_ops = {
	.init = region_lmem_init,
	.release = region_lmem_release,
	.init_object = __i915_gem_ttm_object_init,
};

static bool get_legacy_lowmem_region(struct intel_uncore *uncore,
				     u64 *start, u32 *size)
{
	if (!IS_DG1_GRAPHICS_STEP(uncore->i915, STEP_A0, STEP_C0))
		return false;

	*start = 0;
	*size = SZ_1M;

	drm_dbg(&uncore->i915->drm, "LMEM: reserved legacy low-memory [0x%llx-0x%llx]\n",
		*start, *start + *size);

	return true;
}

static int reserve_lowmem_region(struct intel_uncore *uncore,
				 struct intel_memory_region *mem)
{
	u64 reserve_start;
	u32 reserve_size;
	int ret;

	if (!get_legacy_lowmem_region(uncore, &reserve_start, &reserve_size))
		return 0;

	ret = intel_memory_region_reserve(mem, reserve_start, reserve_size);
	if (ret)
		drm_err(&uncore->i915->drm, "LMEM: reserving low memory region failed\n");

	return ret;
}

static struct intel_memory_region *setup_lmem(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct intel_uncore *uncore = gt->uncore;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct intel_memory_region *mem;
	resource_size_t min_page_size;
	resource_size_t io_start;
	resource_size_t lmem_size;
	int err;

	if (!IS_DGFX(i915))
		return ERR_PTR(-ENODEV);

	if (HAS_FLAT_CCS(i915)) {
		u64 tile_stolen, flat_ccs_base;

		lmem_size = pci_resource_len(pdev, 2);
		flat_ccs_base = intel_gt_read_register(gt, XEHPSDV_FLAT_CCS_BASE_ADDR);
		flat_ccs_base = (flat_ccs_base >> XEHPSDV_CCS_BASE_SHIFT) * SZ_64K;

		if (GEM_WARN_ON(lmem_size < flat_ccs_base))
			return ERR_PTR(-ENODEV);

		tile_stolen = lmem_size - flat_ccs_base;

		/* If the FLAT_CCS_BASE_ADDR register is not populated, flag an error */
		if (tile_stolen == lmem_size)
			drm_err(&i915->drm,
				"CCS_BASE_ADDR register did not have expected value\n");

		lmem_size -= tile_stolen;
	} else {
		/* Stolen starts from GSMBASE without CCS */
		lmem_size = intel_uncore_read64(&i915->uncore, GEN12_GSMBASE);
	}


	io_start = pci_resource_start(pdev, 2);
	if (GEM_WARN_ON(lmem_size > pci_resource_len(pdev, 2)))
		return ERR_PTR(-ENODEV);

	min_page_size = HAS_64K_PAGES(i915) ? I915_GTT_PAGE_SIZE_64K :
						I915_GTT_PAGE_SIZE_4K;
	mem = intel_memory_region_create(i915,
					 0,
					 lmem_size,
					 min_page_size,
					 io_start,
					 lmem_size,
					 INTEL_MEMORY_LOCAL,
					 0,
					 &intel_region_lmem_ops);
	if (IS_ERR(mem))
		return mem;

	err = reserve_lowmem_region(uncore, mem);
	if (err)
		goto err_region_put;

	drm_dbg(&i915->drm, "Local memory: %pR\n", &mem->region);
	drm_dbg(&i915->drm, "Local memory IO start: %pa\n",
		&mem->io_start);
	drm_info(&i915->drm, "Local memory IO size: %pa\n",
		 &mem->io_size);
	drm_info(&i915->drm, "Local memory available: %pa\n",
		 &lmem_size);

	return mem;

err_region_put:
	intel_memory_region_destroy(mem);
	return ERR_PTR(err);
}

struct intel_memory_region *intel_gt_setup_lmem(struct intel_gt *gt)
{
	return setup_lmem(gt);
}
