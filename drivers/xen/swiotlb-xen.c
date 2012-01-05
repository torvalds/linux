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

#include <linux/bootmem.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <xen/swiotlb-xen.h>
#include <xen/page.h>
#include <xen/xen-ops.h>
#include <xen/hvc-console.h>
/*
 * Used to do a quick range check in swiotlb_tbl_unmap_single and
 * swiotlb_tbl_sync_single_*, to see if the memory was in fact allocated by this
 * API.
 */

static char *xen_io_tlb_start, *xen_io_tlb_end;
static unsigned long xen_io_tlb_nslabs;
/*
 * Quick lookup value of the bus address of the IOTLB.
 */

u64 start_dma_addr;

static dma_addr_t xen_phys_to_bus(phys_addr_t paddr)
{
	return phys_to_machine(XPADDR(paddr)).maddr;
}

static phys_addr_t xen_bus_to_phys(dma_addr_t baddr)
{
	return machine_to_phys(XMADDR(baddr)).paddr;
}

static dma_addr_t xen_virt_to_bus(void *address)
{
	return xen_phys_to_bus(virt_to_phys(address));
}

static int check_pages_physically_contiguous(unsigned long pfn,
					     unsigned int offset,
					     size_t length)
{
	unsigned long next_mfn;
	int i;
	int nr_pages;

	next_mfn = pfn_to_mfn(pfn);
	nr_pages = (offset + length + PAGE_SIZE-1) >> PAGE_SHIFT;

	for (i = 1; i < nr_pages; i++) {
		if (pfn_to_mfn(++pfn) != ++next_mfn)
			return 0;
	}
	return 1;
}

static int range_straddles_page_boundary(phys_addr_t p, size_t size)
{
	unsigned long pfn = PFN_DOWN(p);
	unsigned int offset = p & ~PAGE_MASK;

	if (offset + size <= PAGE_SIZE)
		return 0;
	if (check_pages_physically_contiguous(pfn, offset, size))
		return 0;
	return 1;
}

static int is_xen_swiotlb_buffer(dma_addr_t dma_addr)
{
	unsigned long mfn = PFN_DOWN(dma_addr);
	unsigned long pfn = mfn_to_local_pfn(mfn);
	phys_addr_t paddr;

	/* If the address is outside our domain, it CAN
	 * have the same virtual address as another address
	 * in our domain. Therefore _only_ check address within our domain.
	 */
	if (pfn_valid(pfn)) {
		paddr = PFN_PHYS(pfn);
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

	dma_bits = get_order(IO_TLB_SEGSIZE << IO_TLB_SHIFT) + PAGE_SHIFT;

	i = 0;
	do {
		int slabs = min(nslabs - i, (unsigned long)IO_TLB_SEGSIZE);

		do {
			rc = xen_create_contiguous_region(
				(unsigned long)buf + (i << IO_TLB_SHIFT),
				get_order(slabs << IO_TLB_SHIFT),
				dma_bits);
		} while (rc && dma_bits++ < max_dma_bits);
		if (rc)
			return rc;

		i += slabs;
	} while (i < nslabs);
	return 0;
}

void __init xen_swiotlb_init(int verbose)
{
	unsigned long bytes;
	int rc = -ENOMEM;
	unsigned long nr_tbl;
	char *m = NULL;
	unsigned int repeat = 3;

	nr_tbl = swioltb_nr_tbl();
	if (nr_tbl)
		xen_io_tlb_nslabs = nr_tbl;
	else {
		xen_io_tlb_nslabs = (64 * 1024 * 1024 >> IO_TLB_SHIFT);
		xen_io_tlb_nslabs = ALIGN(xen_io_tlb_nslabs, IO_TLB_SEGSIZE);
	}
retry:
	bytes = xen_io_tlb_nslabs << IO_TLB_SHIFT;

	/*
	 * Get IO TLB memory from any location.
	 */
	xen_io_tlb_start = alloc_bootmem_pages(PAGE_ALIGN(bytes));
	if (!xen_io_tlb_start) {
		m = "Cannot allocate Xen-SWIOTLB buffer!\n";
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
		free_bootmem(__pa(xen_io_tlb_start), PAGE_ALIGN(bytes));
		m = "Failed to get contiguous memory for DMA from Xen!\n"\
		    "You either: don't have the permissions, do not have"\
		    " enough free memory under 4GB, or the hypervisor memory"\
		    "is too fragmented!";
		goto error;
	}
	start_dma_addr = xen_virt_to_bus(xen_io_tlb_start);
	swiotlb_init_with_tbl(xen_io_tlb_start, xen_io_tlb_nslabs, verbose);

	return;
error:
	if (repeat--) {
		xen_io_tlb_nslabs = max(1024UL, /* Min is 2MB */
					(xen_io_tlb_nslabs >> 1));
		printk(KERN_INFO "Xen-SWIOTLB: Lowering to %luMB\n",
		      (xen_io_tlb_nslabs << IO_TLB_SHIFT) >> 20);
		goto retry;
	}
	xen_raw_printk("%s (rc:%d)", m, rc);
	panic("%s (rc:%d)", m, rc);
}

void *
xen_swiotlb_alloc_coherent(struct device *hwdev, size_t size,
			   dma_addr_t *dma_handle, gfp_t flags)
{
	void *ret;
	int order = get_order(size);
	u64 dma_mask = DMA_BIT_MASK(32);
	unsigned long vstart;
	phys_addr_t phys;
	dma_addr_t dev_addr;

	/*
	* Ignore region specifiers - the kernel's ideas of
	* pseudo-phys memory layout has nothing to do with the
	* machine physical layout.  We can't allocate highmem
	* because we can't return a pointer to it.
	*/
	flags &= ~(__GFP_DMA | __GFP_HIGHMEM);

	if (dma_alloc_from_coherent(hwdev, size, dma_handle, &ret))
		return ret;

	vstart = __get_free_pages(flags, order);
	ret = (void *)vstart;

	if (!ret)
		return ret;

	if (hwdev && hwdev->coherent_dma_mask)
		dma_mask = hwdev->coherent_dma_mask;

	phys = virt_to_phys(ret);
	dev_addr = xen_phys_to_bus(phys);
	if (((dev_addr + size - 1 <= dma_mask)) &&
	    !range_straddles_page_boundary(phys, size))
		*dma_handle = dev_addr;
	else {
		if (xen_create_contiguous_region(vstart, order,
						 fls64(dma_mask)) != 0) {
			free_pages(vstart, order);
			return NULL;
		}
		*dma_handle = virt_to_machine(ret).maddr;
	}
	memset(ret, 0, size);
	return ret;
}
EXPORT_SYMBOL_GPL(xen_swiotlb_alloc_coherent);

void
xen_swiotlb_free_coherent(struct device *hwdev, size_t size, void *vaddr,
			  dma_addr_t dev_addr)
{
	int order = get_order(size);
	phys_addr_t phys;
	u64 dma_mask = DMA_BIT_MASK(32);

	if (dma_release_from_coherent(hwdev, order, vaddr))
		return;

	if (hwdev && hwdev->coherent_dma_mask)
		dma_mask = hwdev->coherent_dma_mask;

	phys = virt_to_phys(vaddr);

	if (((dev_addr + size - 1 > dma_mask)) ||
	    range_straddles_page_boundary(phys, size))
		xen_destroy_contiguous_region((unsigned long)vaddr, order);

	free_pages((unsigned long)vaddr, order);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_free_coherent);


/*
 * Map a single buffer of the indicated size for DMA in streaming mode.  The
 * physical address to use is returned.
 *
 * Once the device is given the dma address, the device owns this memory until
 * either xen_swiotlb_unmap_page or xen_swiotlb_dma_sync_single is performed.
 */
dma_addr_t xen_swiotlb_map_page(struct device *dev, struct page *page,
				unsigned long offset, size_t size,
				enum dma_data_direction dir,
				struct dma_attrs *attrs)
{
	phys_addr_t phys = page_to_phys(page) + offset;
	dma_addr_t dev_addr = xen_phys_to_bus(phys);
	void *map;

	BUG_ON(dir == DMA_NONE);
	/*
	 * If the address happens to be in the device's DMA window,
	 * we can safely return the device addr and not worry about bounce
	 * buffering it.
	 */
	if (dma_capable(dev, dev_addr, size) &&
	    !range_straddles_page_boundary(phys, size) && !swiotlb_force)
		return dev_addr;

	/*
	 * Oh well, have to allocate and map a bounce buffer.
	 */
	map = swiotlb_tbl_map_single(dev, start_dma_addr, phys, size, dir);
	if (!map)
		return DMA_ERROR_CODE;

	dev_addr = xen_virt_to_bus(map);

	/*
	 * Ensure that the address returned is DMA'ble
	 */
	if (!dma_capable(dev, dev_addr, size)) {
		swiotlb_tbl_unmap_single(dev, map, size, dir);
		dev_addr = 0;
	}
	return dev_addr;
}
EXPORT_SYMBOL_GPL(xen_swiotlb_map_page);

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size must
 * match what was provided for in a previous xen_swiotlb_map_page call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static void xen_unmap_single(struct device *hwdev, dma_addr_t dev_addr,
			     size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = xen_bus_to_phys(dev_addr);

	BUG_ON(dir == DMA_NONE);

	/* NOTE: We use dev_addr here, not paddr! */
	if (is_xen_swiotlb_buffer(dev_addr)) {
		swiotlb_tbl_unmap_single(hwdev, phys_to_virt(paddr), size, dir);
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

void xen_swiotlb_unmap_page(struct device *hwdev, dma_addr_t dev_addr,
			    size_t size, enum dma_data_direction dir,
			    struct dma_attrs *attrs)
{
	xen_unmap_single(hwdev, dev_addr, size, dir);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_unmap_page);

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

	/* NOTE: We use dev_addr here, not paddr! */
	if (is_xen_swiotlb_buffer(dev_addr)) {
		swiotlb_tbl_sync_single(hwdev, phys_to_virt(paddr), size, dir,
				       target);
		return;
	}

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
EXPORT_SYMBOL_GPL(xen_swiotlb_sync_single_for_cpu);

void
xen_swiotlb_sync_single_for_device(struct device *hwdev, dma_addr_t dev_addr,
				   size_t size, enum dma_data_direction dir)
{
	xen_swiotlb_sync_single(hwdev, dev_addr, size, dir, SYNC_FOR_DEVICE);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_sync_single_for_device);

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
int
xen_swiotlb_map_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
			 int nelems, enum dma_data_direction dir,
			 struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		phys_addr_t paddr = sg_phys(sg);
		dma_addr_t dev_addr = xen_phys_to_bus(paddr);

		if (swiotlb_force ||
		    !dma_capable(hwdev, dev_addr, sg->length) ||
		    range_straddles_page_boundary(paddr, sg->length)) {
			void *map = swiotlb_tbl_map_single(hwdev,
							   start_dma_addr,
							   sg_phys(sg),
							   sg->length, dir);
			if (!map) {
				/* Don't panic here, we expect map_sg users
				   to do proper error handling. */
				xen_swiotlb_unmap_sg_attrs(hwdev, sgl, i, dir,
							   attrs);
				sgl[0].dma_length = 0;
				return DMA_ERROR_CODE;
			}
			sg->dma_address = xen_virt_to_bus(map);
		} else
			sg->dma_address = dev_addr;
		sg->dma_length = sg->length;
	}
	return nelems;
}
EXPORT_SYMBOL_GPL(xen_swiotlb_map_sg_attrs);

int
xen_swiotlb_map_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
		   enum dma_data_direction dir)
{
	return xen_swiotlb_map_sg_attrs(hwdev, sgl, nelems, dir, NULL);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_map_sg);

/*
 * Unmap a set of streaming mode DMA translations.  Again, cpu read rules
 * concerning calls here are the same as for swiotlb_unmap_page() above.
 */
void
xen_swiotlb_unmap_sg_attrs(struct device *hwdev, struct scatterlist *sgl,
			   int nelems, enum dma_data_direction dir,
			   struct dma_attrs *attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i)
		xen_unmap_single(hwdev, sg->dma_address, sg->dma_length, dir);

}
EXPORT_SYMBOL_GPL(xen_swiotlb_unmap_sg_attrs);

void
xen_swiotlb_unmap_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
		     enum dma_data_direction dir)
{
	return xen_swiotlb_unmap_sg_attrs(hwdev, sgl, nelems, dir, NULL);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_unmap_sg);

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
					sg->dma_length, dir, target);
}

void
xen_swiotlb_sync_sg_for_cpu(struct device *hwdev, struct scatterlist *sg,
			    int nelems, enum dma_data_direction dir)
{
	xen_swiotlb_sync_sg(hwdev, sg, nelems, dir, SYNC_FOR_CPU);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_sync_sg_for_cpu);

void
xen_swiotlb_sync_sg_for_device(struct device *hwdev, struct scatterlist *sg,
			       int nelems, enum dma_data_direction dir)
{
	xen_swiotlb_sync_sg(hwdev, sg, nelems, dir, SYNC_FOR_DEVICE);
}
EXPORT_SYMBOL_GPL(xen_swiotlb_sync_sg_for_device);

int
xen_swiotlb_dma_mapping_error(struct device *hwdev, dma_addr_t dma_addr)
{
	return !dma_addr;
}
EXPORT_SYMBOL_GPL(xen_swiotlb_dma_mapping_error);

/*
 * Return whether the given device DMA address mask can be supported
 * properly.  For example, if your device can only drive the low 24-bits
 * during bus mastering, then you would pass 0x00ffffff as the mask to
 * this function.
 */
int
xen_swiotlb_dma_supported(struct device *hwdev, u64 mask)
{
	return xen_virt_to_bus(xen_io_tlb_end - 1) <= mask;
}
EXPORT_SYMBOL_GPL(xen_swiotlb_dma_supported);
