/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ARM_XEN_PAGE_COHERENT_H
#define _ASM_ARM_XEN_PAGE_COHERENT_H

#include <linux/dma-mapping.h>
#include <asm/page.h>
#include <xen/arm/page-coherent.h>

static inline const struct dma_map_ops *xen_get_dma_ops(struct device *dev)
{
	if (dev && dev->archdata.dev_dma_ops)
		return dev->archdata.dev_dma_ops;
	return get_arch_dma_ops(NULL);
}

static inline void *xen_alloc_coherent_pages(struct device *hwdev, size_t size,
		dma_addr_t *dma_handle, gfp_t flags, unsigned long attrs)
{
	return xen_get_dma_ops(hwdev)->alloc(hwdev, size, dma_handle, flags, attrs);
}

static inline void xen_free_coherent_pages(struct device *hwdev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle, unsigned long attrs)
{
	xen_get_dma_ops(hwdev)->free(hwdev, size, cpu_addr, dma_handle, attrs);
}

static inline void xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, unsigned long attrs)
{
	unsigned long page_pfn = page_to_xen_pfn(page);
	unsigned long dev_pfn = XEN_PFN_DOWN(dev_addr);
	unsigned long compound_pages =
		(1<<compound_order(page)) * XEN_PFN_PER_PAGE;
	bool local = (page_pfn <= dev_pfn) &&
		(dev_pfn - page_pfn < compound_pages);

	/*
	 * Dom0 is mapped 1:1, while the Linux page can span across
	 * multiple Xen pages, it's not possible for it to contain a
	 * mix of local and foreign Xen pages. So if the first xen_pfn
	 * == mfn the page is local otherwise it's a foreign page
	 * grant-mapped in dom0. If the page is local we can safely
	 * call the native dma_ops function, otherwise we call the xen
	 * specific function.
	 */
	if (local)
		xen_get_dma_ops(hwdev)->map_page(hwdev, page, offset, size, dir, attrs);
	else
		__xen_dma_map_page(hwdev, page, dev_addr, offset, size, dir, attrs);
}

static inline void xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	unsigned long pfn = PFN_DOWN(handle);
	/*
	 * Dom0 is mapped 1:1, while the Linux page can be spanned accross
	 * multiple Xen page, it's not possible to have a mix of local and
	 * foreign Xen page. Dom0 is mapped 1:1, so calling pfn_valid on a
	 * foreign mfn will always return false. If the page is local we can
	 * safely call the native dma_ops function, otherwise we call the xen
	 * specific function.
	 */
	if (pfn_valid(pfn)) {
		if (xen_get_dma_ops(hwdev)->unmap_page)
			xen_get_dma_ops(hwdev)->unmap_page(hwdev, handle, size, dir, attrs);
	} else
		__xen_dma_unmap_page(hwdev, handle, size, dir, attrs);
}

static inline void xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned long pfn = PFN_DOWN(handle);
	if (pfn_valid(pfn)) {
		if (xen_get_dma_ops(hwdev)->sync_single_for_cpu)
			xen_get_dma_ops(hwdev)->sync_single_for_cpu(hwdev, handle, size, dir);
	} else
		__xen_dma_sync_single_for_cpu(hwdev, handle, size, dir);
}

static inline void xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned long pfn = PFN_DOWN(handle);
	if (pfn_valid(pfn)) {
		if (xen_get_dma_ops(hwdev)->sync_single_for_device)
			xen_get_dma_ops(hwdev)->sync_single_for_device(hwdev, handle, size, dir);
	} else
		__xen_dma_sync_single_for_device(hwdev, handle, size, dir);
}

#endif /* _ASM_ARM_XEN_PAGE_COHERENT_H */
