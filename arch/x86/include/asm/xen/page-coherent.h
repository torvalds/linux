#ifndef _ASM_X86_XEN_PAGE_COHERENT_H
#define _ASM_X86_XEN_PAGE_COHERENT_H

#include <asm/page.h>
#include <linux/dma-mapping.h>

static inline void *xen_alloc_coherent_pages(struct device *hwdev, size_t size,
		dma_addr_t *dma_handle, gfp_t flags,
		unsigned long attrs)
{
	void *vstart = (void*)__get_free_pages(flags, get_order(size));
	*dma_handle = virt_to_phys(vstart);
	return vstart;
}

static inline void xen_free_coherent_pages(struct device *hwdev, size_t size,
		void *cpu_addr, dma_addr_t dma_handle,
		unsigned long attrs)
{
	free_pages((unsigned long) cpu_addr, get_order(size));
}

static inline void xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, unsigned long attrs) { }

static inline void xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		unsigned long attrs) { }

static inline void xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir) { }

static inline void xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir) { }

#endif /* _ASM_X86_XEN_PAGE_COHERENT_H */
