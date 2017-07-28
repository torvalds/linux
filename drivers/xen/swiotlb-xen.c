/*
 *  Copyright 2010
 *  by Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 *
 * This code provides a IOMMU for Xen PV guests with PCI passthrough.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License v2.0 as published by
 * the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * PV guests under Xen are running in an non-contiguous memory architecture.
 *
 * When PCI pass-through is utilized, this necessitates an IOMMU for
 * translating bus (DMA) to virtual and vice-versa and also providing a
 * mechanism to have contiguous pages for device drivers operations (say DMA
 * operations).
 *
 * Specifically, under Xen the Linux idea of pages is an illusion. It
 * assumes that pages start at zero and go up to the available memory. To
 * help with that, the Linux Xen MMU provides a lookup mechanism to
 * translate the page frame numbers (PFN) to machine frame numbers (MFN)
 * and vice-versa. The MFN are the "real" frame numbers. Furthermore
 * memory is not contiguous. Xen hypervisor stitches memory for guests
 * from different pools, which means there is no guarantee that PFN==MFN
 * and PFN+1==MFN+1. Lastly with Xen 4.0, pages (in debug mode) are
 * allocated in descending order (high to low), meaning the guest might
 * never get any MFN's under the 4GB mark.
 *
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/bootmem.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <xen/swiotlb-xen.h>
#include <xen/page.h>
#include <xen/xen-ops.h>
#include <xen/hvc-console.h>

#include <asm/dma-mapping.h>
#include <asm/xen/page-coherent.h>

#include <trace/events/swiotlb.h>
/*
 * Used to do a quick range check in swiotlb_tbl_unmap_single and
 * swiotlb_tbl_sync_single_*, to see if the memory was in fact allocated by this
 * API.
 */

#ifndef CONFIG_X86
static unsigned long dma_alloc_coherent_mask(struct device *dev,
					    gfp_t gfp)
{
	unsigned long dma_mask = 0;

	dma_mask = dev->coherent_dma_mask;
	if (!dma_mask)
		dma_mask = (gfp & GFP_DMA) ? DMA_BIT_MASK(24) : DMA_BIT_MASK(32);

	return dma_mask;
}
#endif

#define XEN_SWIOTLB_ERROR_CODE	(~(dma_addr_t)0x0)

static char *xen_io_tlb_start, *xen_io_tlb_end;
static unsigned long xen_io_tlb_nslabs;
/*
 * Quick lookup value of the bus address of the IOTLB.
 */

static u64 start_dma_addr;

/*
 * Both of these functions should avoid XEN_PFN_PHYS because phys_addr_t
 * can be 32bit when dma_addr_t is 64bit leading to a loss in
 * information if the shift is done before casting to 64bit.
 */
static inline dma_addr_t xen_phys_to_bus(phys_addr_t paddr)
{
	unsigned long bfn = pfn_to_bfn(XEN_PFN_DOWN(paddr));
	dma_addr_t dma = (dma_addr_t)bfn << XEN_PAGE_SHIFT;

	dma |= paddr & ~XEN_PAGE_MASK;

	return dma;
}

static inline phys_addr_t xen_bus_to_phys(dma_addr_t baddr)
{
	unsigned long xen_pfn = bfn_to_pfn(XEN_PFN_DOWN(baddr));
	dma_addr_t dma = (dma_addr_t)xen_pfn << XEN_PAGE_SHIFT;
	phys_addr_t paddr = dma;

	paddr |= baddr & ~XEN_PAGE_MASK;

	return paddr;
}

static inline dma_addr_t xen_virt_to_bus(void *address)
{
	return xen_phys_to_bus(virt_to_phys(address));
}

static int check_pages_physically_contiguous(unsigned long xen_pfn,
					     unsigned int offset,
					     size_t length)
{
	unsigned long next_bfn;
	int i;
	int nr_pages;

	next_bfn = pfn_to_bfn(xen_pfn);
	nr_pages = (offset + length + XEN_PAGE_SIZE-1) >> XEN_PAGE_SHIFT;

	for (i = 1; i < nr_pages; i++) {
		if (pfn_to_bfn(++xen_pfn) != ++next_bfn)
			return 0;
	}
	return 1;
}

static inline int range_straddles_page_boundary(phys_addr_t p, size_t size)
{
	unsigned long xen_pfn = XEN_PFN_DOWN(p);
	unsigned int offset = p & ~XEN_PAGE_MASK;

	if (offset + size <= XEN_PAGE_SIZE)
		return 0;
	if (check_pages_physically_contiguous(xen_pfn, offset, size))
		return 0;
	return 1;
}

static int is_xen_swiotlb_buffer(dma_addr_t dma_addr)
{
	unsigned long bfn = XEN_PFN_DOWN(dma_addr);
	unsigned long xen_pfn = bfn_to_local_pfn(bfn);
	phys_addr_t paddr = XEN_PFN_PHYS(xen_pfn);

	/* If the address is outside our domain, it CAN
	 * have the same virtual address as another address
	 * in our domain. Therefore _only_ check address within our domain.
	 */
	if (pfn_valid(PFN_DOWN(paddr))) {
		return paddr >= virt_to_phys(xen_io_tlb_start) &&
		       paddr < virt_to_phys(xen_io_tlb_end);
	}
	return 0;
}

static int max_dma_bits = 32;

static int
xen_swiotlb_fixup(void *buf, size_t size, unsigned long nslabs)
{
	int i, rc;
	int dma_bits;
	dma_addr_t dma_handle;
	phys_addr_t p = virt_to_phys(buf);

	dma_bits = get_order(IO_TLB_SEGSIZE << IO_TLB_SHIFT) + PAGE_SHIFT;

	i = 0;
	do {
		int slabs = min(nslabs - i, (unsigned long)IO_TLB_SEGSIZE);

		do {
			rc = xen_create_contiguous_region(
				p + (i << IO_TLB_SHIFT),
				get_order(slabs << IO_TLB_SHIFT),
				dma_bits, &dma_handle);
		} while (rc && dma_bits++ < max_dma_bits);
		if (rc)
			return rc;

		i += slabs;
	} while (i < nslabs);
	return 0;
}
static unsigned long xen_set_nslabs(unsigned long nr_tbl)
{
	if (!nr_tbl) {
		xen_io_tlb_nslabs = (64 * 1024 * 1024 >> IO_TLB_SHIFT);
		xen_io_tlb_nslabs = ALIGN(xen_io_tlb_nslabs, IO_TLB_SEGSIZE);
	} else
		xen_io_tlb_nslabs = nr_tbl;

	return xen_io_tlb_nslabs << IO_TLB_SHIFT;
}

enum xen_swiotlb_err {
	XEN_SWIOTLB_UNKNOWN = 0,
	XEN_SWIOTLB_ENOMEM,
	XEN_SWIOTLB_EFIXUP
};

static const char *xen_swiotlb_error(enum xen_swiotlb_err err)
{
	switch (err) {
	case XEN_SWIOTLB_ENOMEM:
		return "Cannot allocate Xen-SWIOTLB buffer\n";
	case XEN_SWIOTLB_EFIXUP:
		return "Failed to get contiguous memory for DMA from Xen!\n"\
		    "You either: don't have the permissions, do not have"\
		    " enough free memory under 4GB, or the hypervisor memory"\
		    " is too fragmented!";
	default:
		break;
	}
	return "";
}
int __ref xen_swiotlb_init(int verbose, bool early)
{
	unsigned long bytes, order;
	int rc = -ENOMEM;
	enum xen_swiotlb_err m_ret = XEN_SWIOTLB_UNKNOWN;
	unsigned int repeat = 3;

	xen_io_tlb_nslabs = swiotlb_nr_tbl();
retry:
	bytes = xen_set_nslabs(xen_io_tlb_nslabs);
	order = get_order(xen_io_tlb_nslabs << IO_TLB_SHIFT);
	/*
	 * Get IO TLB memory from any location.
	 */
	if (early)
		xen_io_tlb_start = alloc_bootmem_pages(PAGE_ALIGN(bytes));
	else {
#define SLABS_PER_PAGE (1 << (PAGE_SHIFT - IO_TLB_SHIFT))
#define IO_TLB_MIN_SLABS ((1<<20) >> IO_TLB_SHIFT)
		while ((SLABS_PER_PAGE << order) > IO_TLB_MIN_SLABS) {
			xen_io_tlb_start = (void *)xen_get_swiotlb_free_pages(order);
			if (xen_io_tlb_start)
				break;
			order--;
		}
		if (order != get_order(bytes)) {
			pr_warn("Warning: only able to allocate %ld MB for software IO TLB\n",
				(PAGE_SIZE << order) >> 20);
			xen_io_tlb_nslabs = SLABS_PER_PAGE << order;
			bytes = xen_io_tlb_nslabs << IO_TLB_SHIFT;
		}
	}
	if (!xen_io_tlb_start) {
		m_ret = XEN_SWIOTLB_ENOMEM;
		goto error;
	}
	xen_io_tlb_end = xen_io_tlb_start + bytes;
	/*
	 * And replace that memory with pages under 4GB.
	 */
	rc = xen_swiotlb_fixup(xen_io_tlb_start,
			       bytes,
			       xen_io_tlb_nslabs);
	if (rc) {
		if (early)
			free_bootmem(__pa(xen_io_tlb_start), PAGE_ALIGN(bytes));
		else {
			free_pages((unsigned long)xen_io_tlb_start, order);
			xen_io_tlb_start = NULL;
		}
		m_ret = XEN_SWIOTLB_EFIXUP;
		goto error;
	}
	start_dma_addr = xen_virt_to_bus(xen_io_tlb_start);
	if (early) {
		if (swiotlb_init_with_tbl(xen_io_tlb_start, xen_io_tlb_nslabs,
			 verbose))
			panic("Cannot allocate SWIOTLB buffer");
		rc = 0;
	} else
		rc = swiotlb_late_init_with_tbl(xen_io_tlb_start, xen_io_tlb_nslabs);

	if (!rc)
		swiotlb_set_max_segment(PAGE_SIZE);

	return rc;
error:
	if (repeat--) {
		xen_io_tlb_nslabs = max(1024UL, /* Min is 2MB */
					(xen_io_tlb_nslabs >> 1));
		pr_info("Lowering to %luMB\n",
			(xen_io_tlb_nslabs << IO_TLB_SHIFT) >> 20);
		goto retry;
	}
	pr_err("%s (rc:%d)\n", xen_swiotlb_error(m_ret), rc);
	if (early)
		panic("%s (rc:%d)", xen_swiotlb_error(m_ret), rc);
	else
		free_pages((unsigned long)xen_io_tlb_start, order);
	return rc;
}

static void *
xen_swiotlb_alloc_coherent(struct device *hwdev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flags,
			   unsigned long attrs)
{
	void *ret;
	int order = get_order(size);
	u64 dma_mask = DMA_BIT_MASK(32);
	phys_addr_t phys;
	dma_addr_t dev_addr;

	/*
	* Ignore region specifiers - the kernel's ideas of
	* pseudo-phys memory layout has nothing to do with the
	* machine physical layout.  We can't allocate highmem
	* because we can't return a pointer to it.
	*/
	flags &= ~(__GFP_DMA | __GFP_HIGHMEM);

	/* On ARM this function returns an ioremap'ped virtual address for
	 * which virt_to_phys doesn't return the corresponding physical
	 * address. In fact on ARM virt_to_phys only works for kernel direct
	 * mapped RAM memory. Also see comment below.
	 */
	ret = xen_alloc_coherent_pages(hwdev, size, dma_handle, flags, attrs);

	if (!ret)
		return ret;

	if (hwdev && hwdev->coherent_dma_mask)
		dma_mask = dma_alloc_coherent_mask(hwdev, flags);

	/* At this point dma_handle is the physical address, next we are
	 * going to set it to the machine address.
	 * Do not use virt_to_phys(ret) because on ARM it doesn't correspond
	 * to *dma_handle. */
	phys = *dma_handle;
	dev_addr = xen_phys_to_bus(phys);
	if (((dev_addr + size - 1 <= dma_mask)) &&
	    !range_straddles_page_boundary(phys, size))
		*dma_handle = dev_addr;
	else {
		if (xen_create_contiguous_region(phys, order,
						 fls64(dma_mask), dma_handle) != 0) {
			xen_free_coherent_pages(hwdev, size, ret, (dma_addr_t)phys, attrs);
			return NULL;
		}
	}
	memset(ret, 0, size);
	return ret;
}

static void
xen_swiotlb_free_coherent(struct device *hwdev, size_t size, void *vaddr,
			  dma_addr_t dev_addr, unsigned long attrs)
{
	int order = get_order(size);
	phys_addr_t phys;
	u64 dma_mask = DMA_BIT_MASK(32);

	if (hwdev && hwdev->coherent_dma_mask)
		dma_mask = hwdev->coherent_dma_mask;

	/* do not use virt_to_phys because on ARM it doesn't return you the
	 * physical address */
	phys = xen_bus_to_phys(dev_addr);

	if (((dev_addr + size - 1 > dma_mask)) ||
	    range_straddles_page_boundary(phys, size))
		xen_destroy_contiguous_region(phys, order);

	xen_free_coherent_pages(hwdev, size, vaddr, (dma_addr_t)phys, attrs);
}

/*
 * Map a single buffer of the indicated size for DMA in streaming mode.  The
 * physical address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory until
 * either xen_swiotlb_unmap_page or xen_swiotlb_dma_sync_single is performed.
 */
static dma_addr_t xen_swiotlb_map_page(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction dir,
				unsigned long attrs)
{
	phys_addr_t map, phys = page_to_phys(page) + offset;
	dma_addr_t dev_addr = xen_phys_to_bus(phys);

	BUG_ON(dir == DMA_NONE);
	/*
	 * If the address happens to be in the device's DMA window,
	 * we can safely return the device addr and not worry about bounce
	 * buffering it.
	 */
	if (dma_capable(dev, dev_addr, size) &&
	    !range_straddles_page_boundary(phys, size) &&
		!xen_arch_need_swiotlb(dev, phys, dev_addr) &&
		(swiotlb_force != SWIOTLB_FORCE)) {
		/* we are not interested in the dma_addr returned by
		 * xen_dma_map_page, only in the potential cache flushes executed
		 * by the function. */
		xen_dma_map_page(dev, page, dev_addr, offset, size, dir, attrs);
		return dev_addr;
	}

	/*
	 * Oh well, have to allocate and map a bounce buffer.
	 */
	trace_swiotlb_bounced(dev, dev_addr, size, swiotlb_force);

	map = swiotlb_tbl_map_single(dev, start_dma_addr, phys, size, dir,
				     attrs);
	if (map == SWIOTLB_MAP_ERROR)
		return XEN_SWIOTLB_ERROR_CODE;

	dev_addr = xen_phys_to_bus(map);
	xen_dma_map_page(dev, pfn_to_page(map >> PAGE_SHIFT),
					dev_addr, map & ~PAGE_MASK, size, dir, attrs);

	/*
	 * Ensure that the address returned is DMA'ble
	 */
	if (dma_capable(dev, dev_addr, size))
		return dev_addr;

	attrs |= DMA_ATTR_SKIP_CPU_SYNC;
	swiotlb_tbl_unmap_single(dev, map, size, dir, attrs);

	return XEN_SWIOTLB_ERROR_CODE;
}

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size must
 * match what was provided for in a previous xen_swiotlb_map_page call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static void xen_unmap_single(struct device *hwdev, dma_addr_t dev_addr,
			     size_t size, enum dma_data_direction dir,
			     unsigned long attrs)
{
	phys_addr_t paddr = xen_bus_to_phys(dev_addr);

	BUG_ON(dir == DMA_NONE);

	xen_dma_unmap_page(hwdev, dev_addr, size, dir, attrs);

	/* NOTE: We use dev_addr here, not paddr! */
	if (is_xen_swiotlb_buffer(dev_addr)) {
		swiotlb_tbl_unmap_single(hwdev, paddr, size, dir, attrs);
		return;
	}

	if (dir != DMA_FROM_DEVICE)
		return;

	/*
	 * phys_to_virt doesn't work with hihgmem page but we could
	 * call dma_mark_clean() with hihgmem page here. However, we
	 * are fine since dma_mark_clean() is null on POWERPC. We can
	 * make dma_mark_clean() take a physical address if necessary.
	 */
	dma_mark_clean(phys_to_virt(paddr), size);
}

static void xen_swiotlb_unmap_page(struct device *hwdev, dma_addr_t dev_addr,
			    size_t size, enum dma_data_direction dir,
			    unsigned long attrs)
{
	xen_unmap_single(hwdev, dev_addr, size, dir, attrs);
}

/*
 * Make physical memory consistent for a single streaming mode DMA translation
 * after a transfer.
 *
 * If you perform a xen_swiotlb_map_page() but wish to interrogate the buffer
 * using the cpu, yet do not wish to teardown the dma mapping, you must
 * call this function before doing so.  At the next point you give the dma
 * address back to the card, you must first perform a
 * xen_swiotlb_dma_sync_for_device, and then the device again owns the buffer
 */
static void
xen_swiotlb_sync_single(struct device *hwdev, dma_addr_t dev_addr,
			size_t size, enum dma_data_direction dir,
			enum dma_sync_target target)
{
	phys_addr_t paddr = xen_bus_to_phys(dev_addr);

	BUG_ON(dir == DMA_NONE);

	if (target == SYNC_FOR_CPU)
		xen_dma_sync_single_for_cpu(hwdev, dev_addr, size, dir);

	/* NOTE: We use dev_addr here, not paddr! */
	if (is_xen_swiotlb_buffer(dev_addr))
		swiotlb_tbl_sync_single(hwdev, paddr, size, dir, target);

	if (target == SYNC_FOR_DEVICE)
		xen_dma_sync_single_for_device(hwdev, dev_addr, size, dir);

	if (dir != DMA_FROM_DEVICE)
		return;

	dma_mark_clean(phys_to_virt(paddr), size);
}

void
xen_swiotlb_sync_single_for_cpu(struct device *hwdev, dma_addr_t dev_addr,
				size_t size, enum dma_data_direction dir)
{
	xen_swiotlb_sync_single(hwdev, dev_addr, size, dir, SYNC_FOR_CPU);
}

void
xen_swiotlb_sync_single_for_device(struct device *hwdev, dma_addr_t dev_addr,
				   size_t size, enum dma_data_direction dir)
{
	xen_swiotlb_sync_single(hwdev, dev_addr, size, dir, SYNC_FOR_DEVICE);
}

/*
 * Unmap a set of streaming mode DMA translations.  Again, cpu read rules
 * concerning calls here are the same as for swiotlb_unmap_page() above.
 */
static void
xen_swiotlb_unmap_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
			   int nelems, enum dma_data_direction dir,
			   unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i)
		xen_unmap_single(hwdev, sg->dma_address, sg_dma_len(sg), dir, attrs);

}

/*
 * Map a set of buffers described by scatterlist in streaming mode for DMA.
 * This is the scatter-gather version of the above xen_swiotlb_map_page
 * interface.  Here the scatter gather list elements are each tagged with the
 * appropriate dma address and length.  They are obtained via
 * sg_dma_{address,length}(SG).
 *
 * NOTE: An implementation may be able to use a smaller number of
 *       DMA address/length pairs than there are SG table elements.
 *       (for example via virtual mapping capabilities)
 *       The routine returns the number of addr/length pairs actually
 *       used, at most nents.
 *
 * Device ownership issues as mentioned above for xen_swiotlb_map_page are the
 * same here.
 */
static int
xen_swiotlb_map_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
			 int nelems, enum dma_data_direction dir,
			 unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		phys_addr_t paddr = sg_phys(sg);
		dma_addr_t dev_addr = xen_phys_to_bus(paddr);

		if (swiotlb_force == SWIOTLB_FORCE ||
		    xen_arch_need_swiotlb(hwdev, paddr, dev_addr) ||
		    !dma_capable(hwdev, dev_addr, sg->length) ||
		    range_straddles_page_boundary(paddr, sg->length)) {
			phys_addr_t map = swiotlb_tbl_map_single(hwdev,
								 start_dma_addr,
								 sg_phys(sg),
								 sg->length,
								 dir, attrs);
			if (map == SWIOTLB_MAP_ERROR) {
				dev_warn(hwdev, "swiotlb buffer is full\n");
				/* Don't panic here, we expect map_sg users
				   to do proper error handling. */
				attrs |= DMA_ATTR_SKIP_CPU_SYNC;
				xen_swiotlb_unmap_sg_attrs(hwdev, sgl, i, dir,
							   attrs);
				sg_dma_len(sgl) = 0;
				return 0;
			}
			dev_addr = xen_phys_to_bus(map);
			xen_dma_map_page(hwdev, pfn_to_page(map >> PAGE_SHIFT),
						dev_addr,
						map & ~PAGE_MASK,
						sg->length,
						dir,
						attrs);
			sg->dma_address = dev_addr;
		} else {
			/* we are not interested in the dma_addr returned by
			 * xen_dma_map_page, only in the potential cache flushes executed
			 * by the function. */
			xen_dma_map_page(hwdev, pfn_to_page(paddr >> PAGE_SHIFT),
						dev_addr,
						paddr & ~PAGE_MASK,
						sg->length,
						dir,
						attrs);
			sg->dma_address = dev_addr;
		}
		sg_dma_len(sg) = sg->length;
	}
	return nelems;
}

/*
 * Make physical memory consistent for a set of streaming mode DMA translations
 * after a transfer.
 *
 * The same as swiotlb_sync_single_* but for a scatter-gather list, same rules
 * and usage.
 */
static void
xen_swiotlb_sync_sg(struct device *hwdev, struct scatterlist *sgl,
		    int nelems, enum dma_data_direction dir,
		    enum dma_sync_target target)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i)
		xen_swiotlb_sync_single(hwdev, sg->dma_address,
					sg_dma_len(sg), dir, target);
}

static void
xen_swiotlb_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction dir)
{
	xen_swiotlb_sync_sg(hwdev, sg, nelems, dir, SYNC_FOR_CPU);
}

static void
xen_swiotlb_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
			       int nelems, enum dma_data_direction dir)
{
	xen_swiotlb_sync_sg(hwdev, sg, nelems, dir, SYNC_FOR_DEVICE);
}

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.
 */
static int
xen_swiotlb_dma_supported(struct device *hwdev, u64 mask)
{
	return xen_virt_to_bus(xen_io_tlb_end - 1) <= mask;
}

/*
 * Create userspace mapping for the DMA-coherent memory.
 * This function should be called with the pages from the current domain only,
 * passing pages mapped from other domains would lead to memory corruption.
 */
static int
xen_swiotlb_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		     void *cpu_addr, dma_addr_t dma_addr, size_t size,
		     unsigned long attrs)
{
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
	if (xen_get_dma_ops(dev)->mmap)
		return xen_get_dma_ops(dev)->mmap(dev, vma, cpu_addr,
						    dma_addr, size, attrs);
#endif
	return dma_common_mmap(dev, vma, cpu_addr, dma_addr, size);
}

/*
 * This function should be called with the pages from the current domain only,
 * passing pages mapped from other domains would lead to memory corruption.
 */
static int
xen_swiotlb_get_sgtable(struct device *dev, struct sg_table *sgt,
			void *cpu_addr, dma_addr_t handle, size_t size,
			unsigned long attrs)
{
#if defined(CONFIG_ARM) || defined(CONFIG_ARM64)
	if (xen_get_dma_ops(dev)->get_sgtable) {
#if 0
	/*
	 * This check verifies that the page belongs to the current domain and
	 * is not one mapped from another domain.
	 * This check is for debug only, and should not go to production build
	 */
		unsigned long bfn = PHYS_PFN(dma_to_phys(dev, handle));
		BUG_ON (!page_is_ram(bfn));
#endif
		return xen_get_dma_ops(dev)->get_sgtable(dev, sgt, cpu_addr,
							   handle, size, attrs);
	}
#endif
	return dma_common_get_sgtable(dev, sgt, cpu_addr, handle, size);
}

static int xen_swiotlb_mapping_error(struct device *dev, dma_addr_t dma_addr)
{
	return dma_addr == XEN_SWIOTLB_ERROR_CODE;
}

const struct dma_map_ops xen_swiotlb_dma_ops = {
	.alloc = xen_swiotlb_alloc_coherent,
	.free = xen_swiotlb_free_coherent,
	.sync_single_for_cpu = xen_swiotlb_sync_single_for_cpu,
	.sync_single_for_device = xen_swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = xen_swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = xen_swiotlb_sync_sg_for_device,
	.map_sg = xen_swiotlb_map_sg_attrs,
	.unmap_sg = xen_swiotlb_unmap_sg_attrs,
	.map_page = xen_swiotlb_map_page,
	.unmap_page = xen_swiotlb_unmap_page,
	.dma_supported = xen_swiotlb_dma_supported,
	.mmap = xen_swiotlb_dma_mmap,
	.get_sgtable = xen_swiotlb_get_sgtable,
	.mapping_error	= xen_swiotlb_mapping_error,
};
