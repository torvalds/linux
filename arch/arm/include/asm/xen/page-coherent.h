#ifndef _ASM_ARM_XEN_PAGE_COHERENT_H
#define _ASM_ARM_XEN_PAGE_COHERENT_H

#include <asm/page.h>
#include <linux/dma-attrs.h>
#include <linux/dma-mapping.h>

void __xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, struct dma_attrs *attrs);
void __xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs);
void __xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir);

void __xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir);

static inline void *xen_alloc_coherent_pages(struct device *hwdev, size_t size,
		dma_addr_t *dma_handle, gfp_t flags,
		struct dma_attrs *attrs)
{
	return __generic_dma_ops(hwdev)->alloc(hwdev, size, dma_handle, flags, attrs);
}

static inline void xen_free_coherent_pages(struct device *hwdev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle,
		struct dma_attrs *attrs)
{
	__generic_dma_ops(hwdev)->free(hwdev, size, cpu_addr, dma_handle, attrs);
}

static inline void xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, struct dma_attrs *attrs)
{
	bool local = PFN_DOWN(dev_addr) == page_to_pfn(page);
	/* Dom0 is mapped 1:1, so if pfn == mfn the page is local otherwise
	 * is a foreign page grant-mapped in dom0. If the page is local we
	 * can safely call the native dma_ops function, otherwise we call
	 * the xen specific function. */
	if (local)
		__generic_dma_ops(hwdev)->map_page(hwdev, page, offset, size, dir, attrs);
	else
		__xen_dma_map_page(hwdev, page, dev_addr, offset, size, dir, attrs);
}

static inline void xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	unsigned long pfn = PFN_DOWN(handle);
	/* Dom0 is mapped 1:1, so calling pfn_valid on a foreign mfn will
	 * always return false. If the page is local we can safely call the
	 * native dma_ops function, otherwise we call the xen specific
	 * function. */
	if (pfn_valid(pfn)) {
		if (__generic_dma_ops(hwdev)->unmap_page)
			__generic_dma_ops(hwdev)->unmap_page(hwdev, handle, size, dir, attrs);
	} else
		__xen_dma_unmap_page(hwdev, handle, size, dir, attrs);
}

static inline void xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned long pfn = PFN_DOWN(handle);
	if (pfn_valid(pfn)) {
		if (__generic_dma_ops(hwdev)->sync_single_for_cpu)
			__generic_dma_ops(hwdev)->sync_single_for_cpu(hwdev, handle, size, dir);
	} else
		__xen_dma_sync_single_for_cpu(hwdev, handle, size, dir);
}

static inline void xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	unsigned long pfn = PFN_DOWN(handle);
	if (pfn_valid(pfn)) {
		if (__generic_dma_ops(hwdev)->sync_single_for_device)
			__generic_dma_ops(hwdev)->sync_single_for_device(hwdev, handle, size, dir);
	} else
		__xen_dma_sync_single_for_device(hwdev, handle, size, dir);
}

#endif /* _ASM_ARM_XEN_PAGE_COHERENT_H */
