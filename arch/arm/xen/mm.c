// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/dma-noncoherent.h>
#include <linux/gfp.h>
#include <linux/highmem.h>
#include <linux/export.h>
#include <linux/memblock.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/vmalloc.h>
#include <linux/swiotlb.h>

#include <xen/xen.h>
#include <xen/interface/grant_table.h>
#include <xen/interface/memory.h>
#include <xen/page.h>
#include <xen/swiotlb-xen.h>

#include <asm/cacheflush.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/interface.h>

unsigned long xen_get_swiotlb_free_pages(unsigned int order)
{
	struct memblock_region *reg;
	gfp_t flags = __GFP_NOWARN|__GFP_KSWAPD_RECLAIM;

	for_each_memblock(memory, reg) {
		if (reg->base < (phys_addr_t)0xffffffff) {
			flags |= __GFP_DMA;
			break;
		}
	}
	return __get_free_pages(flags, order);
}

static bool hypercall_cflush = false;

/* buffers in highmem or foreign pages cannot cross page boundaries */
static void dma_cache_maint(dma_addr_t handle, size_t size, u32 op)
{
	struct gnttab_cache_flush cflush;

	cflush.a.dev_bus_addr = handle & XEN_PAGE_MASK;
	cflush.offset = xen_offset_in_page(handle);
	cflush.op = op;

	do {
		if (size + cflush.offset > XEN_PAGE_SIZE)
			cflush.length = XEN_PAGE_SIZE - cflush.offset;
		else
			cflush.length = size;

		HYPERVISOR_grant_table_op(GNTTABOP_cache_flush, &cflush, 1);

		cflush.offset = 0;
		cflush.a.dev_bus_addr += cflush.length;
		size -= cflush.length;
	} while (size);
}

static void __xen_dma_page_dev_to_cpu(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	if (dir != DMA_TO_DEVICE)
		dma_cache_maint(handle, size, GNTTAB_CACHE_INVAL);
}

static void __xen_dma_page_cpu_to_dev(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir)
{
	if (dir == DMA_FROM_DEVICE)
		dma_cache_maint(handle, size, GNTTAB_CACHE_INVAL);
	else
		dma_cache_maint(handle, size, GNTTAB_CACHE_CLEAN);
}

void __xen_dma_map_page(struct device *hwdev, struct page *page,
	     dma_addr_t dev_addr, unsigned long offset, size_t size,
	     enum dma_data_direction dir, unsigned long attrs)
{
	if (dev_is_dma_coherent(hwdev))
		return;
	if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
		return;

	__xen_dma_page_cpu_to_dev(hwdev, dev_addr, size, dir);
}

void __xen_dma_unmap_page(struct device *hwdev, dma_addr_t handle,
		size_t size, enum dma_data_direction dir,
		unsigned long attrs)

{
	if (dev_is_dma_coherent(hwdev))
		return;
	if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
		return;

	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void __xen_dma_sync_single_for_cpu(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (dev_is_dma_coherent(hwdev))
		return;
	__xen_dma_page_dev_to_cpu(hwdev, handle, size, dir);
}

void __xen_dma_sync_single_for_device(struct device *hwdev,
		dma_addr_t handle, size_t size, enum dma_data_direction dir)
{
	if (dev_is_dma_coherent(hwdev))
		return;
	__xen_dma_page_cpu_to_dev(hwdev, handle, size, dir);
}

bool xen_arch_need_swiotlb(struct device *dev,
			   phys_addr_t phys,
			   dma_addr_t dev_addr)
{
	unsigned int xen_pfn = XEN_PFN_DOWN(phys);
	unsigned int bfn = XEN_PFN_DOWN(dev_addr);

	/*
	 * The swiotlb buffer should be used if
	 *	- Xen doesn't have the cache flush hypercall
	 *	- The Linux page refers to foreign memory
	 *	- The device doesn't support coherent DMA request
	 *
	 * The Linux page may be spanned acrros multiple Xen page, although
	 * it's not possible to have a mix of local and foreign Xen page.
	 * Furthermore, range_straddles_page_boundary is already checking
	 * if buffer is physically contiguous in the host RAM.
	 *
	 * Therefore we only need to check the first Xen page to know if we
	 * require a bounce buffer because the device doesn't support coherent
	 * memory and we are not able to flush the cache.
	 */
	return (!hypercall_cflush && (xen_pfn != bfn) &&
		!dev_is_dma_coherent(dev));
}

int xen_create_contiguous_region(phys_addr_t pstart, unsigned int order,
				 unsigned int address_bits,
				 dma_addr_t *dma_handle)
{
	if (!xen_initial_domain())
		return -EINVAL;

	/* we assume that dom0 is mapped 1:1 for now */
	*dma_handle = pstart;
	return 0;
}
EXPORT_SYMBOL_GPL(xen_create_contiguous_region);

void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order)
{
	return;
}
EXPORT_SYMBOL_GPL(xen_destroy_contiguous_region);

int __init xen_mm_init(void)
{
	struct gnttab_cache_flush cflush;
	if (!xen_initial_domain())
		return 0;
	xen_swiotlb_init(1, false);

	cflush.op = 0;
	cflush.a.dev_bus_addr = 0;
	cflush.offset = 0;
	cflush.length = 0;
	if (HYPERVISOR_grant_table_op(GNTTABOP_cache_flush, &cflush, 1) != -ENOSYS)
		hypercall_cflush = true;
	return 0;
}
arch_initcall(xen_mm_init);
