// SPDX-License-Identifier: GPL-2.0-only
/*
 *  Copyright 2010
 *  by Konrad Rzeszutek Wilk <konrad.wilk@oracle.com>
 *
 * This code provides a IOMMU for Xen PV guests with PCI passthrough.
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
 */

#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/memblock.h>
#include <linux/dma-direct.h>
#include <linux/dma-map-ops.h>
#include <linux/export.h>
#include <xen/swiotlb-xen.h>
#include <xen/page.h>
#include <xen/xen-ops.h>
#include <xen/hvc-console.h>

#include <asm/dma-mapping.h>
#include <asm/xen/page-coherent.h>

#include <trace/events/swiotlb.h>
#define MAX_DMA_BITS 32

/*
 * Quick lookup value of the bus address of the IOTLB.
 */

static inline phys_addr_t xen_phys_to_bus(struct device *dev, phys_addr_t paddr)
{
	unsigned long bfn = pfn_to_bfn(XEN_PFN_DOWN(paddr));
	phys_addr_t baddr = (phys_addr_t)bfn << XEN_PAGE_SHIFT;

	baddr |= paddr & ~XEN_PAGE_MASK;
	return baddr;
}

static inline dma_addr_t xen_phys_to_dma(struct device *dev, phys_addr_t paddr)
{
	return phys_to_dma(dev, xen_phys_to_bus(dev, paddr));
}

static inline phys_addr_t xen_bus_to_phys(struct device *dev,
					  phys_addr_t baddr)
{
	unsigned long xen_pfn = bfn_to_pfn(XEN_PFN_DOWN(baddr));
	phys_addr_t paddr = (xen_pfn << XEN_PAGE_SHIFT) |
			    (baddr & ~XEN_PAGE_MASK);

	return paddr;
}

static inline phys_addr_t xen_dma_to_phys(struct device *dev,
					  dma_addr_t dma_addr)
{
	return xen_bus_to_phys(dev, dma_to_phys(dev, dma_addr));
}

static inline int range_straddles_page_boundary(phys_addr_t p, size_t size)
{
	unsigned long next_bfn, xen_pfn = XEN_PFN_DOWN(p);
	unsigned int i, nr_pages = XEN_PFN_UP(xen_offset_in_page(p) + size);

	next_bfn = pfn_to_bfn(xen_pfn);

	for (i = 1; i < nr_pages; i++)
		if (pfn_to_bfn(++xen_pfn) != ++next_bfn)
			return 1;

	return 0;
}

static int is_xen_swiotlb_buffer(struct device *dev, dma_addr_t dma_addr)
{
	unsigned long bfn = XEN_PFN_DOWN(dma_to_phys(dev, dma_addr));
	unsigned long xen_pfn = bfn_to_local_pfn(bfn);
	phys_addr_t paddr = (phys_addr_t)xen_pfn << XEN_PAGE_SHIFT;

	/* If the address is outside our domain, it CAN
	 * have the same virtual address as another address
	 * in our domain. Therefore _only_ check address within our domain.
	 */
	if (pfn_valid(PFN_DOWN(paddr)))
		return is_swiotlb_buffer(paddr);
	return 0;
}

static int xen_swiotlb_fixup(void *buf, unsigned long nslabs)
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
		} while (rc && dma_bits++ < MAX_DMA_BITS);
		if (rc)
			return rc;

		i += slabs;
	} while (i < nslabs);
	return 0;
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

#define DEFAULT_NSLABS		ALIGN(SZ_64M >> IO_TLB_SHIFT, IO_TLB_SEGSIZE)

int __ref xen_swiotlb_init(void)
{
	enum xen_swiotlb_err m_ret = XEN_SWIOTLB_UNKNOWN;
	unsigned long bytes = swiotlb_size_or_default();
	unsigned long nslabs = bytes >> IO_TLB_SHIFT;
	unsigned int order, repeat = 3;
	int rc = -ENOMEM;
	char *start;

	if (io_tlb_default_mem != NULL) {
		pr_warn("swiotlb buffer already initialized\n");
		return -EEXIST;
	}

retry:
	m_ret = XEN_SWIOTLB_ENOMEM;
	order = get_order(bytes);

	/*
	 * Get IO TLB memory from any location.
	 */
#define SLABS_PER_PAGE (1 << (PAGE_SHIFT - IO_TLB_SHIFT))
#define IO_TLB_MIN_SLABS ((1<<20) >> IO_TLB_SHIFT)
	while ((SLABS_PER_PAGE << order) > IO_TLB_MIN_SLABS) {
		start = (void *)xen_get_swiotlb_free_pages(order);
		if (start)
			break;
		order--;
	}
	if (!start)
		goto error;
	if (order != get_order(bytes)) {
		pr_warn("Warning: only able to allocate %ld MB for software IO TLB\n",
			(PAGE_SIZE << order) >> 20);
		nslabs = SLABS_PER_PAGE << order;
		bytes = nslabs << IO_TLB_SHIFT;
	}

	/*
	 * And replace that memory with pages under 4GB.
	 */
	rc = xen_swiotlb_fixup(start, nslabs);
	if (rc) {
		free_pages((unsigned long)start, order);
		m_ret = XEN_SWIOTLB_EFIXUP;
		goto error;
	}
	rc = swiotlb_late_init_with_tbl(start, nslabs);
	if (rc)
		return rc;
	swiotlb_set_max_segment(PAGE_SIZE);
	return 0;
error:
	if (repeat--) {
		/* Min is 2MB */
		nslabs = max(1024UL, (nslabs >> 1));
		pr_info("Lowering to %luMB\n",
			(nslabs << IO_TLB_SHIFT) >> 20);
		goto retry;
	}
	pr_err("%s (rc:%d)\n", xen_swiotlb_error(m_ret), rc);
	return rc;
}

#ifdef CONFIG_X86
void __init xen_swiotlb_init_early(void)
{
	unsigned long bytes = swiotlb_size_or_default();
	unsigned long nslabs = bytes >> IO_TLB_SHIFT;
	unsigned int repeat = 3;
	char *start;
	int rc;

retry:
	/*
	 * Get IO TLB memory from any location.
	 */
	start = memblock_alloc(PAGE_ALIGN(bytes), PAGE_SIZE);
	if (!start)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_ALIGN(bytes), PAGE_SIZE);

	/*
	 * And replace that memory with pages under 4GB.
	 */
	rc = xen_swiotlb_fixup(start, nslabs);
	if (rc) {
		memblock_free(__pa(start), PAGE_ALIGN(bytes));
		if (repeat--) {
			/* Min is 2MB */
			nslabs = max(1024UL, (nslabs >> 1));
			bytes = nslabs << IO_TLB_SHIFT;
			pr_info("Lowering to %luMB\n", bytes >> 20);
			goto retry;
		}
		panic("%s (rc:%d)", xen_swiotlb_error(XEN_SWIOTLB_EFIXUP), rc);
	}

	if (swiotlb_init_with_tbl(start, nslabs, false))
		panic("Cannot allocate SWIOTLB buffer");
	swiotlb_set_max_segment(PAGE_SIZE);
}
#endif /* CONFIG_X86 */

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

	/* Convert the size to actually allocated. */
	size = 1UL << (order + XEN_PAGE_SHIFT);

	/* On ARM this function returns an ioremap'ped virtual address for
	 * which virt_to_phys doesn't return the corresponding physical
	 * address. In fact on ARM virt_to_phys only works for kernel direct
	 * mapped RAM memory. Also see comment below.
	 */
	ret = xen_alloc_coherent_pages(hwdev, size, dma_handle, flags, attrs);

	if (!ret)
		return ret;

	if (hwdev && hwdev->coherent_dma_mask)
		dma_mask = hwdev->coherent_dma_mask;

	/* At this point dma_handle is the dma address, next we are
	 * going to set it to the machine address.
	 * Do not use virt_to_phys(ret) because on ARM it doesn't correspond
	 * to *dma_handle. */
	phys = dma_to_phys(hwdev, *dma_handle);
	dev_addr = xen_phys_to_dma(hwdev, phys);
	if (((dev_addr + size - 1 <= dma_mask)) &&
	    !range_straddles_page_boundary(phys, size))
		*dma_handle = dev_addr;
	else {
		if (xen_create_contiguous_region(phys, order,
						 fls64(dma_mask), dma_handle) != 0) {
			xen_free_coherent_pages(hwdev, size, ret, (dma_addr_t)phys, attrs);
			return NULL;
		}
		*dma_handle = phys_to_dma(hwdev, *dma_handle);
		SetPageXenRemapped(virt_to_page(ret));
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
	struct page *page;

	if (hwdev && hwdev->coherent_dma_mask)
		dma_mask = hwdev->coherent_dma_mask;

	/* do not use virt_to_phys because on ARM it doesn't return you the
	 * physical address */
	phys = xen_dma_to_phys(hwdev, dev_addr);

	/* Convert the size to actually allocated. */
	size = 1UL << (order + XEN_PAGE_SHIFT);

	if (is_vmalloc_addr(vaddr))
		page = vmalloc_to_page(vaddr);
	else
		page = virt_to_page(vaddr);

	if (!WARN_ON((dev_addr + size - 1 > dma_mask) ||
		     range_straddles_page_boundary(phys, size)) &&
	    TestClearPageXenRemapped(page))
		xen_destroy_contiguous_region(phys, order);

	xen_free_coherent_pages(hwdev, size, vaddr, phys_to_dma(hwdev, phys),
				attrs);
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
	dma_addr_t dev_addr = xen_phys_to_dma(dev, phys);

	BUG_ON(dir == DMA_NONE);
	/*
	 * If the address happens to be in the device's DMA window,
	 * we can safely return the device addr and not worry about bounce
	 * buffering it.
	 */
	if (dma_capable(dev, dev_addr, size, true) &&
	    !range_straddles_page_boundary(phys, size) &&
		!xen_arch_need_swiotlb(dev, phys, dev_addr) &&
		swiotlb_force != SWIOTLB_FORCE)
		goto done;

	/*
	 * Oh well, have to allocate and map a bounce buffer.
	 */
	trace_swiotlb_bounced(dev, dev_addr, size, swiotlb_force);

	map = swiotlb_tbl_map_single(dev, phys, size, size, dir, attrs);
	if (map == (phys_addr_t)DMA_MAPPING_ERROR)
		return DMA_MAPPING_ERROR;

	phys = map;
	dev_addr = xen_phys_to_dma(dev, map);

	/*
	 * Ensure that the address returned is DMA'ble
	 */
	if (unlikely(!dma_capable(dev, dev_addr, size, true))) {
		swiotlb_tbl_unmap_single(dev, map, size, dir,
				attrs | DMA_ATTR_SKIP_CPU_SYNC);
		return DMA_MAPPING_ERROR;
	}

done:
	if (!dev_is_dma_coherent(dev) && !(attrs & DMA_ATTR_SKIP_CPU_SYNC)) {
		if (pfn_valid(PFN_DOWN(dma_to_phys(dev, dev_addr))))
			arch_sync_dma_for_device(phys, size, dir);
		else
			xen_dma_sync_for_device(dev, dev_addr, size, dir);
	}
	return dev_addr;
}

/*
 * Unmap a single streaming mode DMA translation.  The dma_addr and size must
 * match what was provided for in a previous xen_swiotlb_map_page call.  All
 * other usages are undefined.
 *
 * After this call, reads by the cpu to the buffer are guaranteed to see
 * whatever the device wrote there.
 */
static void xen_swiotlb_unmap_page(struct device *hwdev, dma_addr_t dev_addr,
		size_t size, enum dma_data_direction dir, unsigned long attrs)
{
	phys_addr_t paddr = xen_dma_to_phys(hwdev, dev_addr);

	BUG_ON(dir == DMA_NONE);

	if (!dev_is_dma_coherent(hwdev) && !(attrs & DMA_ATTR_SKIP_CPU_SYNC)) {
		if (pfn_valid(PFN_DOWN(dma_to_phys(hwdev, dev_addr))))
			arch_sync_dma_for_cpu(paddr, size, dir);
		else
			xen_dma_sync_for_cpu(hwdev, dev_addr, size, dir);
	}

	/* NOTE: We use dev_addr here, not paddr! */
	if (is_xen_swiotlb_buffer(hwdev, dev_addr))
		swiotlb_tbl_unmap_single(hwdev, paddr, size, dir, attrs);
}

static void
xen_swiotlb_sync_single_for_cpu(struct device *dev, dma_addr_t dma_addr,
		size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = xen_dma_to_phys(dev, dma_addr);

	if (!dev_is_dma_coherent(dev)) {
		if (pfn_valid(PFN_DOWN(dma_to_phys(dev, dma_addr))))
			arch_sync_dma_for_cpu(paddr, size, dir);
		else
			xen_dma_sync_for_cpu(dev, dma_addr, size, dir);
	}

	if (is_xen_swiotlb_buffer(dev, dma_addr))
		swiotlb_sync_single_for_cpu(dev, paddr, size, dir);
}

static void
xen_swiotlb_sync_single_for_device(struct device *dev, dma_addr_t dma_addr,
		size_t size, enum dma_data_direction dir)
{
	phys_addr_t paddr = xen_dma_to_phys(dev, dma_addr);

	if (is_xen_swiotlb_buffer(dev, dma_addr))
		swiotlb_sync_single_for_device(dev, paddr, size, dir);

	if (!dev_is_dma_coherent(dev)) {
		if (pfn_valid(PFN_DOWN(dma_to_phys(dev, dma_addr))))
			arch_sync_dma_for_device(paddr, size, dir);
		else
			xen_dma_sync_for_device(dev, dma_addr, size, dir);
	}
}

/*
 * Unmap a set of streaming mode DMA translations.  Again, cpu read rules
 * concerning calls here are the same as for swiotlb_unmap_page() above.
 */
static void
xen_swiotlb_unmap_sg(struct device *hwdev, struct scatterlist *sgl, int nelems,
		enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i)
		xen_swiotlb_unmap_page(hwdev, sg->dma_address, sg_dma_len(sg),
				dir, attrs);

}

static int
xen_swiotlb_map_sg(struct device *dev, struct scatterlist *sgl, int nelems,
		enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	BUG_ON(dir == DMA_NONE);

	for_each_sg(sgl, sg, nelems, i) {
		sg->dma_address = xen_swiotlb_map_page(dev, sg_page(sg),
				sg->offset, sg->length, dir, attrs);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			goto out_unmap;
		sg_dma_len(sg) = sg->length;
	}

	return nelems;
out_unmap:
	xen_swiotlb_unmap_sg(dev, sgl, i, dir, attrs | DMA_ATTR_SKIP_CPU_SYNC);
	sg_dma_len(sgl) = 0;
	return 0;
}

static void
xen_swiotlb_sync_sg_for_cpu(struct device *dev, struct scatterlist *sgl,
			    int nelems, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i) {
		xen_swiotlb_sync_single_for_cpu(dev, sg->dma_address,
				sg->length, dir);
	}
}

static void
xen_swiotlb_sync_sg_for_device(struct device *dev, struct scatterlist *sgl,
			       int nelems, enum dma_data_direction dir)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nelems, i) {
		xen_swiotlb_sync_single_for_device(dev, sg->dma_address,
				sg->length, dir);
	}
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
	return xen_phys_to_dma(hwdev, io_tlb_default_mem->end - 1) <= mask;
}

const struct dma_map_ops xen_swiotlb_dma_ops = {
	.alloc = xen_swiotlb_alloc_coherent,
	.free = xen_swiotlb_free_coherent,
	.sync_single_for_cpu = xen_swiotlb_sync_single_for_cpu,
	.sync_single_for_device = xen_swiotlb_sync_single_for_device,
	.sync_sg_for_cpu = xen_swiotlb_sync_sg_for_cpu,
	.sync_sg_for_device = xen_swiotlb_sync_sg_for_device,
	.map_sg = xen_swiotlb_map_sg,
	.unmap_sg = xen_swiotlb_unmap_sg,
	.map_page = xen_swiotlb_map_page,
	.unmap_page = xen_swiotlb_unmap_page,
	.dma_supported = xen_swiotlb_dma_supported,
	.mmap = dma_common_mmap,
	.get_sgtable = dma_common_get_sgtable,
	.alloc_pages = dma_common_alloc_pages,
	.free_pages = dma_common_free_pages,
};
