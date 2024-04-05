// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_fw_mips.h"
#include "pvr_gem.h"
#include "pvr_mmu.h"
#include "pvr_rogue_mips.h"
#include "pvr_vm.h"
#include "pvr_vm_mips.h"

#include <drm/drm_managed.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/types.h>

/**
 * pvr_vm_mips_init() - Initialise MIPS FW pagetable
 * @pvr_dev: Target PowerVR device.
 *
 * Returns:
 *  * 0 on success,
 *  * -%EINVAL,
 *  * Any error returned by pvr_gem_object_create(), or
 *  * And error returned by pvr_gem_object_vmap().
 */
int
pvr_vm_mips_init(struct pvr_device *pvr_dev)
{
	u32 pt_size = 1 << ROGUE_MIPSFW_LOG2_PAGETABLE_SIZE_4K(pvr_dev);
	struct device *dev = from_pvr_device(pvr_dev)->dev;
	struct pvr_fw_mips_data *mips_data;
	u32 phys_bus_width;
	int page_nr;
	int err;

	/* Page table size must be at most ROGUE_MIPSFW_MAX_NUM_PAGETABLE_PAGES * 4k pages. */
	if (pt_size > ROGUE_MIPSFW_MAX_NUM_PAGETABLE_PAGES * SZ_4K)
		return -EINVAL;

	if (PVR_FEATURE_VALUE(pvr_dev, phys_bus_width, &phys_bus_width))
		return -EINVAL;

	mips_data = drmm_kzalloc(from_pvr_device(pvr_dev), sizeof(*mips_data), GFP_KERNEL);
	if (!mips_data)
		return -ENOMEM;

	for (page_nr = 0; page_nr < PVR_MIPS_PT_PAGE_COUNT; page_nr++) {
		mips_data->pt_pages[page_nr] = alloc_page(GFP_KERNEL | __GFP_ZERO);
		if (!mips_data->pt_pages[page_nr]) {
			err = -ENOMEM;
			goto err_free_pages;
		}

		mips_data->pt_dma_addr[page_nr] = dma_map_page(dev, mips_data->pt_pages[page_nr], 0,
							       PAGE_SIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, mips_data->pt_dma_addr[page_nr])) {
			err = -ENOMEM;
			__free_page(mips_data->pt_pages[page_nr]);
			goto err_free_pages;
		}
	}

	mips_data->pt = vmap(mips_data->pt_pages, pt_size >> PAGE_SHIFT, VM_MAP,
			     pgprot_writecombine(PAGE_KERNEL));
	if (!mips_data->pt) {
		err = -ENOMEM;
		goto err_free_pages;
	}

	mips_data->pfn_mask = (phys_bus_width > 32) ? ROGUE_MIPSFW_ENTRYLO_PFN_MASK_ABOVE_32BIT :
						      ROGUE_MIPSFW_ENTRYLO_PFN_MASK;

	mips_data->cache_policy = (phys_bus_width > 32) ? ROGUE_MIPSFW_CACHED_POLICY_ABOVE_32BIT :
							  ROGUE_MIPSFW_CACHED_POLICY;

	pvr_dev->fw_dev.processor_data.mips_data = mips_data;

	return 0;

err_free_pages:
	while (--page_nr >= 0) {
		dma_unmap_page(from_pvr_device(pvr_dev)->dev,
			       mips_data->pt_dma_addr[page_nr], PAGE_SIZE, DMA_TO_DEVICE);

		__free_page(mips_data->pt_pages[page_nr]);
	}

	return err;
}

/**
 * pvr_vm_mips_fini() - Release MIPS FW pagetable
 * @pvr_dev: Target PowerVR device.
 */
void
pvr_vm_mips_fini(struct pvr_device *pvr_dev)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mips_data *mips_data = fw_dev->processor_data.mips_data;
	int page_nr;

	vunmap(mips_data->pt);
	for (page_nr = PVR_MIPS_PT_PAGE_COUNT - 1; page_nr >= 0; page_nr--) {
		dma_unmap_page(from_pvr_device(pvr_dev)->dev,
			       mips_data->pt_dma_addr[page_nr], PAGE_SIZE, DMA_TO_DEVICE);

		__free_page(mips_data->pt_pages[page_nr]);
	}

	fw_dev->processor_data.mips_data = NULL;
}

static u32
get_mips_pte_flags(bool read, bool write, u32 cache_policy)
{
	u32 flags = 0;

	if (read && write) /* Read/write. */
		flags |= ROGUE_MIPSFW_ENTRYLO_DIRTY_EN;
	else if (write)    /* Write only. */
		flags |= ROGUE_MIPSFW_ENTRYLO_READ_INHIBIT_EN;
	else
		WARN_ON(!read);

	flags |= cache_policy << ROGUE_MIPSFW_ENTRYLO_CACHE_POLICY_SHIFT;

	flags |= ROGUE_MIPSFW_ENTRYLO_VALID_EN | ROGUE_MIPSFW_ENTRYLO_GLOBAL_EN;

	return flags;
}

/**
 * pvr_vm_mips_map() - Map a FW object into MIPS address space
 * @pvr_dev: Target PowerVR device.
 * @fw_obj: FW object to map.
 *
 * Returns:
 *  * 0 on success,
 *  * -%EINVAL if object does not reside within FW address space, or
 *  * Any error returned by pvr_fw_object_get_dma_addr().
 */
int
pvr_vm_mips_map(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mips_data *mips_data = fw_dev->processor_data.mips_data;
	struct pvr_gem_object *pvr_obj = fw_obj->gem;
	const u64 start = fw_obj->fw_mm_node.start;
	const u64 size = fw_obj->fw_mm_node.size;
	u64 end;
	u32 cache_policy;
	u32 pte_flags;
	s32 start_pfn;
	s32 end_pfn;
	s32 pfn;
	int err;

	if (check_add_overflow(start, size - 1, &end))
		return -EINVAL;

	if (start < ROGUE_FW_HEAP_BASE ||
	    start >= ROGUE_FW_HEAP_BASE + fw_dev->fw_heap_info.raw_size ||
	    end < ROGUE_FW_HEAP_BASE ||
	    end >= ROGUE_FW_HEAP_BASE + fw_dev->fw_heap_info.raw_size ||
	    (start & ROGUE_MIPSFW_PAGE_MASK_4K) ||
	    ((end + 1) & ROGUE_MIPSFW_PAGE_MASK_4K))
		return -EINVAL;

	start_pfn = (start & fw_dev->fw_heap_info.offset_mask) >> ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K;
	end_pfn = (end & fw_dev->fw_heap_info.offset_mask) >> ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K;

	if (pvr_obj->flags & PVR_BO_FW_FLAGS_DEVICE_UNCACHED)
		cache_policy = ROGUE_MIPSFW_UNCACHED_CACHE_POLICY;
	else
		cache_policy = mips_data->cache_policy;

	pte_flags = get_mips_pte_flags(true, true, cache_policy);

	for (pfn = start_pfn; pfn <= end_pfn; pfn++) {
		dma_addr_t dma_addr;
		u32 pte;

		err = pvr_fw_object_get_dma_addr(fw_obj,
						 (pfn - start_pfn) <<
						 ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K,
						 &dma_addr);
		if (err)
			goto err_unmap_pages;

		pte = ((dma_addr >> ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K)
		       << ROGUE_MIPSFW_ENTRYLO_PFN_SHIFT) & mips_data->pfn_mask;
		pte |= pte_flags;

		WRITE_ONCE(mips_data->pt[pfn], pte);
	}

	pvr_mmu_flush_request_all(pvr_dev);

	return 0;

err_unmap_pages:
	while (--pfn >= start_pfn)
		WRITE_ONCE(mips_data->pt[pfn], 0);

	pvr_mmu_flush_request_all(pvr_dev);
	WARN_ON(pvr_mmu_flush_exec(pvr_dev, true));

	return err;
}

/**
 * pvr_vm_mips_unmap() - Unmap a FW object into MIPS address space
 * @pvr_dev: Target PowerVR device.
 * @fw_obj: FW object to unmap.
 */
void
pvr_vm_mips_unmap(struct pvr_device *pvr_dev, struct pvr_fw_object *fw_obj)
{
	struct pvr_fw_device *fw_dev = &pvr_dev->fw_dev;
	struct pvr_fw_mips_data *mips_data = fw_dev->processor_data.mips_data;
	const u64 start = fw_obj->fw_mm_node.start;
	const u64 size = fw_obj->fw_mm_node.size;
	const u64 end = start + size;

	const u32 start_pfn = (start & fw_dev->fw_heap_info.offset_mask) >>
			      ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K;
	const u32 end_pfn = (end & fw_dev->fw_heap_info.offset_mask) >>
			    ROGUE_MIPSFW_LOG2_PAGE_SIZE_4K;

	for (u32 pfn = start_pfn; pfn < end_pfn; pfn++)
		WRITE_ONCE(mips_data->pt[pfn], 0);

	pvr_mmu_flush_request_all(pvr_dev);
	WARN_ON(pvr_mmu_flush_exec(pvr_dev, true));
}
