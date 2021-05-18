// SPDX-License-Identifier: GPL-2.0-only
#include <linux/cpu.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
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
#include <xen/xen-ops.h>
#include <xen/swiotlb-xen.h>

#include <asm/cacheflush.h>
#include <asm/xen/hypercall.h>
#include <asm/xen/interface.h>

unsigned long xen_get_swiotlb_free_pages(unsigned int order)
{
	phys_addr_t base;
	gfp_t flags = __GFP_NOWARN|__GFP_KSWAPD_RECLAIM;
	u64 i;

	for_each_mem_range(i, &base, NULL) {
		if (base < (phys_addr_t)0xffffffff) {
			if (IS_ENABLED(CONFIG_ZONE_DMA32))
				flags |= __GFP_DMA32;
			else
				flags |= __GFP_DMA;
			break;
		}
	}
	return __get_free_pages(flags, order);
}

static bool hypercall_cflush = false;

/* buffers in highmem or foreign pages cannot cross page boundaries */
static void dma_cache_maint(struct device *dev, dma_addr_t handle,
			    size_t size, u32 op)
{
	struct gnttab_cache_flush cflush;

	cflush.offset = xen_offset_in_page(handle);
	cflush.op = op;
	handle &= XEN_PAGE_MASK;

	do {
		cflush.a.dev_bus_addr = dma_to_phys(dev, handle);

		if (size + cflush.offset > XEN_PAGE_SIZE)
			cflush.length = XEN_PAGE_SIZE - cflush.offset;
		else
			cflush.length = size;

		HYPERVISOR_grant_table_op(GNTTABOP_cache_flush, &cflush, 1);

		cflush.offset = 0;
		handle += cflush.length;
		size -= cflush.length;
	} while (size);
}

/*
 * Dom0 is mapped 1:1, and while the Linux page can span across multiple Xen
 * pages, it is not possible for it to contain a mix of local and foreign Xen
 * pages.  Calling pfn_valid on a foreign mfn will always return false, so if
 * pfn_valid returns true the pages is local and we can use the native
 * dma-direct functions, otherwise we call the Xen specific version.
 */
void xen_dma_sync_for_cpu(struct device *dev, dma_addr_t handle,
			  size_t size, enum dma_data_direction dir)
{
	if (dir != DMA_TO_DEVICE)
		dma_cache_maint(dev, handle, size, GNTTAB_CACHE_INVAL);
}

void xen_dma_sync_for_device(struct device *dev, dma_addr_t handle,
			     size_t size, enum dma_data_direction dir)
{
	if (dir == DMA_FROM_DEVICE)
		dma_cache_maint(dev, handle, size, GNTTAB_CACHE_INVAL);
	else
		dma_cache_maint(dev, handle, size, GNTTAB_CACHE_CLEAN);
}

bool xen_arch_need_swiotlb(struct device *dev,
			   phys_addr_t phys,
			   dma_addr_t dev_addr)
{
	unsigned int xen_pfn = XEN_PFN_DOWN(phys);
	unsigned int bfn = XEN_PFN_DOWN(dma_to_phys(dev, dev_addr));

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

void xen_destroy_contiguous_region(phys_addr_t pstart, unsigned int order)
{
	return;
}

int xen_swiotlb_detect(void)
{
	if (!xen_domain())
		return 0;
	if (xen_feature(XENFEAT_direct_mapped))
		return 1;
	/* legacy case */
	if (!xen_feature(XENFEAT_not_direct_mapped) && xen_initial_domain())
		return 1;
	return 0;
}

static int __init xen_mm_init(void)
{
	struct gnttab_cache_flush cflush;
	if (!xen_swiotlb_detect())
		return 0;
	xen_swiotlb_init();

	cflush.op = 0;
	cflush.a.dev_bus_addr = 0;
	cflush.offset = 0;
	cflush.length = 0;
	if (HYPERVISOR_grant_table_op(GNTTABOP_cache_flush, &cflush, 1) != -ENOSYS)
		hypercall_cflush = true;
	return 0;
}
arch_initcall(xen_mm_init);
