// SPDX-License-Identifier: MIT
/*
 * Copyright © 2019 Intel Corporation
 */

#include "i915_drv.h"
#include "intel_memory_region.h"
#include "gem/i915_gem_lmem.h"
#include "gem/i915_gem_region.h"
#include "intel_region_lmem.h"

static int init_fake_lmem_bar(struct intel_memory_region *mem)
{
	struct drm_i915_private *i915 = mem->i915;
	struct i915_ggtt *ggtt = &i915->ggtt;
	unsigned long n;
	int ret;

	/* We want to 1:1 map the mappable aperture to our reserved region */

	mem->fake_mappable.start = 0;
	mem->fake_mappable.size = resource_size(&mem->region);
	mem->fake_mappable.color = I915_COLOR_UNEVICTABLE;

	ret = drm_mm_reserve_node(&ggtt->vm.mm, &mem->fake_mappable);
	if (ret)
		return ret;

	mem->remap_addr = dma_map_resource(i915->drm.dev,
					   mem->region.start,
					   mem->fake_mappable.size,
					   PCI_DMA_BIDIRECTIONAL,
					   DMA_ATTR_FORCE_CONTIGUOUS);
	if (dma_mapping_error(i915->drm.dev, mem->remap_addr)) {
		drm_mm_remove_node(&mem->fake_mappable);
		return -EINVAL;
	}

	for (n = 0; n < mem->fake_mappable.size >> PAGE_SHIFT; ++n) {
		ggtt->vm.insert_page(&ggtt->vm,
				     mem->remap_addr + (n << PAGE_SHIFT),
				     n << PAGE_SHIFT,
				     I915_CACHE_NONE, 0);
	}

	mem->region = (struct resource)DEFINE_RES_MEM(mem->remap_addr,
						      mem->fake_mappable.size);

	return 0;
}

static void release_fake_lmem_bar(struct intel_memory_region *mem)
{
	if (!drm_mm_node_allocated(&mem->fake_mappable))
		return;

	drm_mm_remove_node(&mem->fake_mappable);

	dma_unmap_resource(mem->i915->drm.dev,
			   mem->remap_addr,
			   mem->fake_mappable.size,
			   PCI_DMA_BIDIRECTIONAL,
			   DMA_ATTR_FORCE_CONTIGUOUS);
}

static void
region_lmem_release(struct intel_memory_region *mem)
{
	release_fake_lmem_bar(mem);
	io_mapping_fini(&mem->iomap);
	intel_memory_region_release_buddy(mem);
}

static int
region_lmem_init(struct intel_memory_region *mem)
{
	int ret;

	if (mem->i915->params.fake_lmem_start) {
		ret = init_fake_lmem_bar(mem);
		GEM_BUG_ON(ret);
	}

	if (!io_mapping_init_wc(&mem->iomap,
				mem->io_start,
				resource_size(&mem->region)))
		return -EIO;

	ret = intel_memory_region_init_buddy(mem);
	if (ret)
		io_mapping_fini(&mem->iomap);

	return ret;
}

static const struct intel_memory_region_ops intel_region_lmem_ops = {
	.init = region_lmem_init,
	.release = region_lmem_release,
	.init_object = __i915_gem_lmem_object_init,
};

struct intel_memory_region *
intel_gt_setup_fake_lmem(struct intel_gt *gt)
{
	struct drm_i915_private *i915 = gt->i915;
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct intel_memory_region *mem;
	resource_size_t mappable_end;
	resource_size_t io_start;
	resource_size_t start;

	if (!HAS_LMEM(i915))
		return ERR_PTR(-ENODEV);

	if (!i915->params.fake_lmem_start)
		return ERR_PTR(-ENODEV);

	GEM_BUG_ON(i915_ggtt_has_aperture(&i915->ggtt));

	/* Your mappable aperture belongs to me now! */
	mappable_end = pci_resource_len(pdev, 2);
	io_start = pci_resource_start(pdev, 2);
	start = i915->params.fake_lmem_start;

	mem = intel_memory_region_create(i915,
					 start,
					 mappable_end,
					 PAGE_SIZE,
					 io_start,
					 &intel_region_lmem_ops);
	if (!IS_ERR(mem)) {
		drm_info(&i915->drm, "Intel graphics fake LMEM: %pR\n",
			 &mem->region);
		drm_info(&i915->drm,
			 "Intel graphics fake LMEM IO start: %llx\n",
			(u64)mem->io_start);
		drm_info(&i915->drm, "Intel graphics fake LMEM size: %llx\n",
			 (u64)resource_size(&mem->region));
	}

	return mem;
}

static bool get_legacy_lowmem_region(struct intel_uncore *uncore,
				     u64 *start, u32 *size)
{
	if (!IS_DG1_REVID(uncore->i915, DG1_REVID_A0, DG1_REVID_B0))
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
	struct pci_dev *pdev = i915->drm.pdev;
	struct intel_memory_region *mem;
	resource_size_t io_start;
	resource_size_t lmem_size;
	int err;

	if (!IS_DGFX(i915))
		return ERR_PTR(-ENODEV);

	/* Stolen starts from GSMBASE on DG1 */
	lmem_size = intel_uncore_read64(uncore, GEN12_GSMBASE);

	io_start = pci_resource_start(pdev, 2);
	if (GEM_WARN_ON(lmem_size > pci_resource_len(pdev, 2)))
		return ERR_PTR(-ENODEV);

	mem = intel_memory_region_create(i915,
					 0,
					 lmem_size,
					 I915_GTT_PAGE_SIZE_4K,
					 io_start,
					 &intel_region_lmem_ops);
	if (IS_ERR(mem))
		return mem;

	err = reserve_lowmem_region(uncore, mem);
	if (err)
		goto err_region_put;

	drm_dbg(&i915->drm, "Local memory: %pR\n", &mem->region);
	drm_dbg(&i915->drm, "Local memory IO start: %pa\n",
		&mem->io_start);
	drm_info(&i915->drm, "Local memory available: %pa\n",
		 &lmem_size);

	return mem;

err_region_put:
	intel_memory_region_put(mem);
	return ERR_PTR(err);
}

struct intel_memory_region *intel_gt_setup_lmem(struct intel_gt *gt)
{
	return setup_lmem(gt);
}
