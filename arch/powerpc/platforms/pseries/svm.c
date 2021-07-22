// SPDX-License-Identifier: GPL-2.0+
/*
 * Secure VM platform
 *
 * Copyright 2018 IBM Corporation
 * Author: Anshuman Khandual <khandual@linux.vnet.ibm.com>
 */

#include <linux/mm.h>
#include <linux/memblock.h>
#include <asm/machdep.h>
#include <asm/svm.h>
#include <asm/swiotlb.h>
#include <asm/ultravisor.h>
#include <asm/dtl.h>

static int __init init_svm(void)
{
	if (!is_secure_guest())
		return 0;

	/* Don't release the SWIOTLB buffer. */
	ppc_swiotlb_enable = 1;

	/*
	 * Since the guest memory is inaccessible to the host, devices always
	 * need to use the SWIOTLB buffer for DMA even if dma_capable() says
	 * otherwise.
	 */
	swiotlb_force = SWIOTLB_FORCE;

	/* Share the SWIOTLB buffer with the host. */
	swiotlb_update_mem_attributes();

	return 0;
}
machine_early_initcall(pseries, init_svm);

/*
 * Initialize SWIOTLB. Essentially the same as swiotlb_init(), except that it
 * can allocate the buffer anywhere in memory. Since the hypervisor doesn't have
 * any addressing limitation, we don't need to allocate it in low addresses.
 */
void __init svm_swiotlb_init(void)
{
	unsigned char *vstart;
	unsigned long bytes, io_tlb_nslabs;

	io_tlb_nslabs = (swiotlb_size_or_default() >> IO_TLB_SHIFT);
	io_tlb_nslabs = ALIGN(io_tlb_nslabs, IO_TLB_SEGSIZE);

	bytes = io_tlb_nslabs << IO_TLB_SHIFT;

	vstart = memblock_alloc(PAGE_ALIGN(bytes), PAGE_SIZE);
	if (vstart && !swiotlb_init_with_tbl(vstart, io_tlb_nslabs, false))
		return;


	memblock_free_early(__pa(vstart),
			    PAGE_ALIGN(io_tlb_nslabs << IO_TLB_SHIFT));
	panic("SVM: Cannot allocate SWIOTLB buffer");
}

int set_memory_encrypted(unsigned long addr, int numpages)
{
	if (!PAGE_ALIGNED(addr))
		return -EINVAL;

	uv_unshare_page(PHYS_PFN(__pa(addr)), numpages);

	return 0;
}

int set_memory_decrypted(unsigned long addr, int numpages)
{
	if (!PAGE_ALIGNED(addr))
		return -EINVAL;

	uv_share_page(PHYS_PFN(__pa(addr)), numpages);

	return 0;
}

/* There's one dispatch log per CPU. */
#define NR_DTL_PAGE (DISPATCH_LOG_BYTES * CONFIG_NR_CPUS / PAGE_SIZE)

static struct page *dtl_page_store[NR_DTL_PAGE];
static long dtl_nr_pages;

static bool is_dtl_page_shared(struct page *page)
{
	long i;

	for (i = 0; i < dtl_nr_pages; i++)
		if (dtl_page_store[i] == page)
			return true;

	return false;
}

void dtl_cache_ctor(void *addr)
{
	unsigned long pfn = PHYS_PFN(__pa(addr));
	struct page *page = pfn_to_page(pfn);

	if (!is_dtl_page_shared(page)) {
		dtl_page_store[dtl_nr_pages] = page;
		dtl_nr_pages++;
		WARN_ON(dtl_nr_pages >= NR_DTL_PAGE);
		uv_share_page(pfn, 1);
	}
}
