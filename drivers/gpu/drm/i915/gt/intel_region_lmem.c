// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "i915_pci.h"
#include "i915_reg.h"
#include "intel_memory_region.h"
#include "intel_pci_config.h"
#include "intel_region_lmem.h"
#include "intel_region_ttm.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "gem/i915_gem_ttm.h"
#include "gt/intel_gt.h"
#include "gt/intel_gt_mcr.h"
#include "gt/intel_gt_regs.h"

#ifdef CONFIG_64BIT
static void _release_bars(struct pci_dev *pdev)
{
	int resno;

	for (resno = PCI_STD_RESOURCES; resno < PCI_STD_RESOURCE_END; resno++) {
		if (pci_resource_len(pdev, resno))
			pci_release_resource(pdev, resno);
	}
}

static void
_resize_bar(struct drm_i915_private *i915, int resno, resource_size_t size)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	int bar_size = pci_rebar_bytes_to_size(size);
	int ret;

	_release_bars(pdev);

	ret = pci_resize_resource(pdev, resno, bar_size);
	if (ret) {
		drm_info(&i915->drm, "Failed to resize BAR%d to %dM (%pe)\n",
			 resno, 1 << bar_size, ERR_PTR(ret));
		return;
	}

	drm_info(&i915->drm, "BAR%d resized to %dM\n", resno, 1 << bar_size);
}

static void i915_resize_lmem_bar(struct drm_i915_private *i915, resource_size_t lmem_size)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct pci_bus *root = pdev->bus;
	struct resource *root_res;
	resource_size_t rebar_size;
	resource_size_t current_size;
	intel_wakeref_t wakeref;
	u32 pci_cmd;
	int i;

	current_size = roundup_pow_of_two(pci_resource_len(pdev, GEN12_LMEM_BAR));

	if (i915->params.lmem_bar_size) {
		u32 bar_sizes;

		rebar_size = i915->params.lmem_bar_size *
			(resource_size_t)SZ_1M;
		bar_sizes = pci_rebar_get_possible_sizes(pdev, GEN12_LMEM_BAR);

		if (rebar_size == current_size)
			return;

		if (!(bar_sizes & BIT(pci_rebar_bytes_to_size(rebar_size))) ||
		    rebar_size >= roundup_pow_of_two(lmem_size)) {
			rebar_size = lmem_size;

			drm_info(&i915->drm,
				 "Given bar size is not within supported size, setting it to default: %llu\n",
				 (u64)lmem_size >> 20);
		}
	} else {
		rebar_size = current_size;

		if (rebar_size != roundup_pow_of_two(lmem_size))
			rebar_size = lmem_size;
		else
			return;
	}

	/* Find out if root bus contains 64bit memory addressing */
	while (root->parent)
		root = root->parent;

	pci_bus_for_each_resource(root, root_res, i) {
		if (root_res && root_res->flags & (IORESOURCE_MEM | IORESOURCE_MEM_64) &&
		    root_res->start > 0x100000000ull)
			break;
	}

	/* pci_resize_resource will fail anyways */
	if (!root_res) {
		drm_info(&i915->drm, "Can't resize LMEM BAR - platform support is missing\n");
		return;
	}

	/*
	 * Releasing forcewake during BAR resizing results in later forcewake
	 * ack timeouts and former can happen any time - it is asynchronous.
	 * Grabbing all forcewakes prevents it.
	 */
	with_intel_runtime_pm(i915->uncore.rpm, wakeref) {
		intel_uncore_forcewake_get(&i915->uncore, FORCEWAKE_ALL);

		/* First disable PCI memory decoding references */
		pci_read_config_dword(pdev, PCI_COMMAND, &pci_cmd);
		pci_write_config_dword(pdev, PCI_COMMAND,
				       pci_cmd & ~PCI_COMMAND_MEMORY);

		_resize_bar(i915, GEN12_LMEM_BAR, rebar_size);

		pci_assign_unassigned_bus_resources(pdev->bus);
		pci_write_config_dword(pdev, PCI_COMMAND, pci_cmd);
		intel_uncore_forcewake_put(&i915->uncore, FORCEWAKE_ALL);
	}
}
#else
static void i915_resize_lmem_bar(struct drm_i915_private *i915, resource_size_t lmem_size) {}
#endif

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
	if (!IS_DG1(uncore->i915))
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
	resource_size_t io_size;
	resource_size_t lmem_size;
	int err;

	if (!IS_DGFX(i915))
		return ERR_PTR(-ENODEV);

	if (!i915_pci_resource_valid(pdev, GEN12_LMEM_BAR))
		return ERR_PTR(-ENXIO);

	if (HAS_FLAT_CCS(i915)) {
		resource_size_t lmem_range;
		u64 tile_stolen, flat_ccs_base;

		lmem_range = intel_gt_mcr_read_any(to_gt(i915), XEHP_TILE0_ADDR_RANGE) & 0xFFFF;
		lmem_size = lmem_range >> XEHP_TILE_LMEM_RANGE_SHIFT;
		lmem_size *= SZ_1G;

		flat_ccs_base = intel_gt_mcr_read_any(gt, XEHP_FLAT_CCS_BASE_ADDR);
		flat_ccs_base = (flat_ccs_base >> XEHP_CCS_BASE_SHIFT) * SZ_64K;

		if (GEM_WARN_ON(lmem_size < flat_ccs_base))
			return ERR_PTR(-EIO);

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

	i915_resize_lmem_bar(i915, lmem_size);

	if (i915->params.lmem_size > 0) {
		lmem_size = min_t(resource_size_t, lmem_size,
				  mul_u32_u32(i915->params.lmem_size, SZ_1M));
	}

	io_start = pci_resource_start(pdev, GEN12_LMEM_BAR);
	io_size = min(pci_resource_len(pdev, GEN12_LMEM_BAR), lmem_size);
	if (!io_size)
		return ERR_PTR(-EIO);

	min_page_size = HAS_64K_PAGES(i915) ? I915_GTT_PAGE_SIZE_64K :
						I915_GTT_PAGE_SIZE_4K;
	mem = intel_memory_region_create(i915,
					 0,
					 lmem_size,
					 min_page_size,
					 io_start,
					 io_size,
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

	if (io_size < lmem_size)
		drm_info(&i915->drm, "Using a reduced BAR size of %lluMiB. Consider enabling 'Resizable BAR' or similar, if available in the BIOS.\n",
			 (u64)io_size >> 20);

	return mem;

err_region_put:
	intel_memory_region_destroy(mem);
	return ERR_PTR(err);
}

struct intel_memory_region *intel_gt_setup_lmem(struct intel_gt *gt)
{
	return setup_lmem(gt);
}
