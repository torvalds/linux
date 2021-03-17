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

	intel_memory_region_set_name(mem, "local");

	return ret;
}

static const struct intel_memory_region_ops intel_region_lmem_ops = {
	.init = region_lmem_init,
	.release = region_lmem_release,
	.init_object = __i915_gem_lmem_object_init,
};

struct intel_memory_region *
intel_setup_fake_lmem(struct drm_i915_private *i915)
{
	struct pci_dev *pdev = to_pci_dev(i915->drm.dev);
	struct intel_memory_region *mem;
	resource_size_t mappable_end;
	resource_size_t io_start;
	resource_size_t start;

	GEM_BUG_ON(i915_ggtt_has_aperture(&i915->ggtt));
	GEM_BUG_ON(!i915->params.fake_lmem_start);

	/* Your mappable aperture belongs to me now! */
	mappable_end = pci_resource_len(pdev, 2);
	io_start = pci_resource_start(pdev, 2),
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
