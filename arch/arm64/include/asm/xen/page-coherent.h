#ifndef _ASM_ARM64_XEN_PAGE_COHERENT_H
#define _ASM_ARM64_XEN_PAGE_COHERENT_H

#include <asm/page.h>
#include <linux/dma-attrs.h>
#include <linux/dma-mapping.h>

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
	     unsigned long offset, size_t size, enum dma_data_direction dir,
	     struct dma_attrs *attrs)
{
	__generic_dma_ops(hwdev)->map_page(hwdev, page, offset, size, dir, attrs);
}

static inline void xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		struct dma_attrs *attrs)
{
	__generic_dma_ops(hwdev)->unmap_page(hwdev, handle, size, dir, attrs);
}

static inline void xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	__generic_dma_ops(hwdev)->sync_single_for_cpu(hwdev, handle, size, dir);
}

static inline void xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	__generic_dma_ops(hwdev)->sync_single_for_device(hwdev, handle, size, dir);
}
#endif /* _ASM_ARM64_XEN_PAGE_COHERENT_H */
