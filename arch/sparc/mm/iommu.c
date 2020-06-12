// SPDX-License-Identifier: GPL-2.0
/*
 * iommu.c:  IOMMU specific routines for memory management.
 *
 * Copyright (C) 1995 David S. Miller  (davem@caip.rutgers.edu)
 * Copyright (C) 1995,2002 Pete Zaitcev     (zaitcev@yahoo.com)
 * Copyright (C) 1996 Eddie C. Dost    (ecd@skynet.be)
 * Copyright (C) 1997,1998 Jakub Jelinek    (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/dma-mapping.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/pgalloc.h>
#include <asm/io.h>
#include <asm/mxcc.h>
#include <asm/mbus.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/bitext.h>
#include <asm/iommu.h>
#include <asm/dma.h>

#include "mm_32.h"

/*
 * This can be sized dynamically, but we will do this
 * only when we have a guidance about actual I/O pressures.
 */
#define IOMMU_RNGE	IOMMU_RNGE_256MB
#define IOMMU_START	0xF0000000
#define IOMMU_WINSIZE	(256*1024*1024U)
#define IOMMU_NPTES	(IOMMU_WINSIZE/PAGE_SIZE)	/* 64K PTEs, 256KB */
#define IOMMU_ORDER	6				/* 4096 * (1<<6) */

static int viking_flush;
/* viking.S */
extern void viking_flush_page(unsigned long page);
extern void viking_mxcc_flush_page(unsigned long page);

/*
 * Values precomputed according to CPU type.
 */
static unsigned int ioperm_noc;		/* Consistent mapping iopte flags */
static pgprot_t dvma_prot;		/* Consistent mapping pte flags */

#define IOPERM        (IOPTE_CACHE | IOPTE_WRITE | IOPTE_VALID)
#define MKIOPTE(pfn, perm) (((((pfn)<<8) & IOPTE_PAGE) | (perm)) & ~IOPTE_WAZ)

static const struct dma_map_ops sbus_iommu_dma_gflush_ops;
static const struct dma_map_ops sbus_iommu_dma_pflush_ops;

static void __init sbus_iommu_init(struct platform_device *op)
{
	struct iommu_struct *iommu;
	unsigned int impl, vers;
	unsigned long *bitmap;
	unsigned long control;
	unsigned long base;
	unsigned long tmp;

	iommu = kmalloc(sizeof(struct iommu_struct), GFP_KERNEL);
	if (!iommu) {
		prom_printf("Unable to allocate iommu structure\n");
		prom_halt();
	}

	iommu->regs = of_ioremap(&op->resource[0], 0, PAGE_SIZE * 3,
				 "iommu_regs");
	if (!iommu->regs) {
		prom_printf("Cannot map IOMMU registers\n");
		prom_halt();
	}

	control = sbus_readl(&iommu->regs->control);
	impl = (control & IOMMU_CTRL_IMPL) >> 28;
	vers = (control & IOMMU_CTRL_VERS) >> 24;
	control &= ~(IOMMU_CTRL_RNGE);
	control |= (IOMMU_RNGE_256MB | IOMMU_CTRL_ENAB);
	sbus_writel(control, &iommu->regs->control);

	iommu_invalidate(iommu->regs);
	iommu->start = IOMMU_START;
	iommu->end = 0xffffffff;

	/* Allocate IOMMU page table */
	/* Stupid alignment constraints give me a headache. 
	   We need 256K or 512K or 1M or 2M area aligned to
           its size and current gfp will fortunately give
           it to us. */
        tmp = __get_free_pages(GFP_KERNEL, IOMMU_ORDER);
	if (!tmp) {
		prom_printf("Unable to allocate iommu table [0x%lx]\n",
			    IOMMU_NPTES * sizeof(iopte_t));
		prom_halt();
	}
	iommu->page_table = (iopte_t *)tmp;

	/* Initialize new table. */
	memset(iommu->page_table, 0, IOMMU_NPTES*sizeof(iopte_t));
	flush_cache_all();
	flush_tlb_all();

	base = __pa((unsigned long)iommu->page_table) >> 4;
	sbus_writel(base, &iommu->regs->base);
	iommu_invalidate(iommu->regs);

	bitmap = kmalloc(IOMMU_NPTES>>3, GFP_KERNEL);
	if (!bitmap) {
		prom_printf("Unable to allocate iommu bitmap [%d]\n",
			    (int)(IOMMU_NPTES>>3));
		prom_halt();
	}
	bit_map_init(&iommu->usemap, bitmap, IOMMU_NPTES);
	/* To be coherent on HyperSparc, the page color of DVMA
	 * and physical addresses must match.
	 */
	if (srmmu_modtype == HyperSparc)
		iommu->usemap.num_colors = vac_cache_size >> PAGE_SHIFT;
	else
		iommu->usemap.num_colors = 1;

	printk(KERN_INFO "IOMMU: impl %d vers %d table 0x%p[%d B] map [%d b]\n",
	       impl, vers, iommu->page_table,
	       (int)(IOMMU_NPTES*sizeof(iopte_t)), (int)IOMMU_NPTES);

	op->dev.archdata.iommu = iommu;

	if (flush_page_for_dma_global)
		op->dev.dma_ops = &sbus_iommu_dma_gflush_ops;
	 else
		op->dev.dma_ops = &sbus_iommu_dma_pflush_ops;
}

static int __init iommu_init(void)
{
	struct device_node *dp;

	for_each_node_by_name(dp, "iommu") {
		struct platform_device *op = of_find_device_by_node(dp);

		sbus_iommu_init(op);
		of_propagate_archdata(op);
	}

	return 0;
}

subsys_initcall(iommu_init);

/* Flush the iotlb entries to ram. */
/* This could be better if we didn't have to flush whole pages. */
static void iommu_flush_iotlb(iopte_t *iopte, unsigned int niopte)
{
	unsigned long start;
	unsigned long end;

	start = (unsigned long)iopte;
	end = PAGE_ALIGN(start + niopte*sizeof(iopte_t));
	start &= PAGE_MASK;
	if (viking_mxcc_present) {
		while(start < end) {
			viking_mxcc_flush_page(start);
			start += PAGE_SIZE;
		}
	} else if (viking_flush) {
		while(start < end) {
			viking_flush_page(start);
			start += PAGE_SIZE;
		}
	} else {
		while(start < end) {
			__flush_page_to_ram(start);
			start += PAGE_SIZE;
		}
	}
}

static dma_addr_t __sbus_iommu_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t len, bool per_page_flush)
{
	struct iommu_struct *iommu = dev->archdata.iommu;
	phys_addr_t paddr = page_to_phys(page) + offset;
	unsigned long off = paddr & ~PAGE_MASK;
	unsigned long npages = (off + len + PAGE_SIZE - 1) >> PAGE_SHIFT;
	unsigned long pfn = __phys_to_pfn(paddr);
	unsigned int busa, busa0;
	iopte_t *iopte, *iopte0;
	int ioptex, i;

	/* XXX So what is maxphys for us and how do drivers know it? */
	if (!len || len > 256 * 1024)
		return DMA_MAPPING_ERROR;

	/*
	 * We expect unmapped highmem pages to be not in the cache.
	 * XXX Is this a good assumption?
	 * XXX What if someone else unmaps it here and races us?
	 */
	if (per_page_flush && !PageHighMem(page)) {
		unsigned long vaddr, p;

		vaddr = (unsigned long)page_address(page) + offset;
		for (p = vaddr & PAGE_MASK; p < vaddr + len; p += PAGE_SIZE)
			flush_page_for_dma(p);
	}

	/* page color = pfn of page */
	ioptex = bit_map_string_get(&iommu->usemap, npages, pfn);
	if (ioptex < 0)
		panic("iommu out");
	busa0 = iommu->start + (ioptex << PAGE_SHIFT);
	iopte0 = &iommu->page_table[ioptex];

	busa = busa0;
	iopte = iopte0;
	for (i = 0; i < npages; i++) {
		iopte_val(*iopte) = MKIOPTE(pfn, IOPERM);
		iommu_invalidate_page(iommu->regs, busa);
		busa += PAGE_SIZE;
		iopte++;
		pfn++;
	}

	iommu_flush_iotlb(iopte0, npages);
	return busa0 + off;
}

static dma_addr_t sbus_iommu_map_page_gflush(struct device *dev,
		struct page *page, unsigned long offset, size_t len,
		enum dma_data_direction dir, unsigned long attrs)
{
	flush_page_for_dma(0);
	return __sbus_iommu_map_page(dev, page, offset, len, false);
}

static dma_addr_t sbus_iommu_map_page_pflush(struct device *dev,
		struct page *page, unsigned long offset, size_t len,
		enum dma_data_direction dir, unsigned long attrs)
{
	return __sbus_iommu_map_page(dev, page, offset, len, true);
}

static int __sbus_iommu_map_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs,
		bool per_page_flush)
{
	struct scatterlist *sg;
	int j;

	for_each_sg(sgl, sg, nents, j) {
		sg->dma_address =__sbus_iommu_map_page(dev, sg_page(sg),
				sg->offset, sg->length, per_page_flush);
		if (sg->dma_address == DMA_MAPPING_ERROR)
			return 0;
		sg->dma_length = sg->length;
	}

	return nents;
}

static int sbus_iommu_map_sg_gflush(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	flush_page_for_dma(0);
	return __sbus_iommu_map_sg(dev, sgl, nents, dir, attrs, false);
}

static int sbus_iommu_map_sg_pflush(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	return __sbus_iommu_map_sg(dev, sgl, nents, dir, attrs, true);
}

static void sbus_iommu_unmap_page(struct device *dev, dma_addr_t dma_addr,
		size_t len, enum dma_data_direction dir, unsigned long attrs)
{
	struct iommu_struct *iommu = dev->archdata.iommu;
	unsigned int busa = dma_addr & PAGE_MASK;
	unsigned long off = dma_addr & ~PAGE_MASK;
	unsigned int npages = (off + len + PAGE_SIZE-1) >> PAGE_SHIFT;
	unsigned int ioptex = (busa - iommu->start) >> PAGE_SHIFT;
	unsigned int i;

	BUG_ON(busa < iommu->start);
	for (i = 0; i < npages; i++) {
		iopte_val(iommu->page_table[ioptex + i]) = 0;
		iommu_invalidate_page(iommu->regs, busa);
		busa += PAGE_SIZE;
	}
	bit_map_clear(&iommu->usemap, ioptex, npages);
}

static void sbus_iommu_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sgl, sg, nents, i) {
		sbus_iommu_unmap_page(dev, sg->dma_address, sg->length, dir,
				attrs);
		sg->dma_address = 0x21212121;
	}
}

#ifdef CONFIG_SBUS
static void *sbus_iommu_alloc(struct device *dev, size_t len,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	struct iommu_struct *iommu = dev->archdata.iommu;
	unsigned long va, addr, page, end, ret;
	iopte_t *iopte = iommu->page_table;
	iopte_t *first;
	int ioptex;

	/* XXX So what is maxphys for us and how do drivers know it? */
	if (!len || len > 256 * 1024)
		return NULL;

	len = PAGE_ALIGN(len);
	va = __get_free_pages(gfp | __GFP_ZERO, get_order(len));
	if (va == 0)
		return NULL;

	addr = ret = sparc_dma_alloc_resource(dev, len);
	if (!addr)
		goto out_free_pages;

	BUG_ON((va & ~PAGE_MASK) != 0);
	BUG_ON((addr & ~PAGE_MASK) != 0);
	BUG_ON((len & ~PAGE_MASK) != 0);

	/* page color = physical address */
	ioptex = bit_map_string_get(&iommu->usemap, len >> PAGE_SHIFT,
		addr >> PAGE_SHIFT);
	if (ioptex < 0)
		panic("iommu out");

	iopte += ioptex;
	first = iopte;
	end = addr + len;
	while(addr < end) {
		page = va;
		{
			pmd_t *pmdp;
			pte_t *ptep;

			if (viking_mxcc_present)
				viking_mxcc_flush_page(page);
			else if (viking_flush)
				viking_flush_page(page);
			else
				__flush_page_to_ram(page);

			pmdp = pmd_off_k(addr);
			ptep = pte_offset_map(pmdp, addr);

			set_pte(ptep, mk_pte(virt_to_page(page), dvma_prot));
		}
		iopte_val(*iopte++) =
		    MKIOPTE(page_to_pfn(virt_to_page(page)), ioperm_noc);
		addr += PAGE_SIZE;
		va += PAGE_SIZE;
	}
	/* P3: why do we need this?
	 *
	 * DAVEM: Because there are several aspects, none of which
	 *        are handled by a single interface.  Some cpus are
	 *        completely not I/O DMA coherent, and some have
	 *        virtually indexed caches.  The driver DMA flushing
	 *        methods handle the former case, but here during
	 *        IOMMU page table modifications, and usage of non-cacheable
	 *        cpu mappings of pages potentially in the cpu caches, we have
	 *        to handle the latter case as well.
	 */
	flush_cache_all();
	iommu_flush_iotlb(first, len >> PAGE_SHIFT);
	flush_tlb_all();
	iommu_invalidate(iommu->regs);

	*dma_handle = iommu->start + (ioptex << PAGE_SHIFT);
	return (void *)ret;

out_free_pages:
	free_pages(va, get_order(len));
	return NULL;
}

static void sbus_iommu_free(struct device *dev, size_t len, void *cpu_addr,
			       dma_addr_t busa, unsigned long attrs)
{
	struct iommu_struct *iommu = dev->archdata.iommu;
	iopte_t *iopte = iommu->page_table;
	struct page *page = virt_to_page(cpu_addr);
	int ioptex = (busa - iommu->start) >> PAGE_SHIFT;
	unsigned long end;

	if (!sparc_dma_free_resource(cpu_addr, len))
		return;

	BUG_ON((busa & ~PAGE_MASK) != 0);
	BUG_ON((len & ~PAGE_MASK) != 0);

	iopte += ioptex;
	end = busa + len;
	while (busa < end) {
		iopte_val(*iopte++) = 0;
		busa += PAGE_SIZE;
	}
	flush_tlb_all();
	iommu_invalidate(iommu->regs);
	bit_map_clear(&iommu->usemap, ioptex, len >> PAGE_SHIFT);

	__free_pages(page, get_order(len));
}
#endif

static const struct dma_map_ops sbus_iommu_dma_gflush_ops = {
#ifdef CONFIG_SBUS
	.alloc			= sbus_iommu_alloc,
	.free			= sbus_iommu_free,
#endif
	.map_page		= sbus_iommu_map_page_gflush,
	.unmap_page		= sbus_iommu_unmap_page,
	.map_sg			= sbus_iommu_map_sg_gflush,
	.unmap_sg		= sbus_iommu_unmap_sg,
};

static const struct dma_map_ops sbus_iommu_dma_pflush_ops = {
#ifdef CONFIG_SBUS
	.alloc			= sbus_iommu_alloc,
	.free			= sbus_iommu_free,
#endif
	.map_page		= sbus_iommu_map_page_pflush,
	.unmap_page		= sbus_iommu_unmap_page,
	.map_sg			= sbus_iommu_map_sg_pflush,
	.unmap_sg		= sbus_iommu_unmap_sg,
};

void __init ld_mmu_iommu(void)
{
	if (viking_mxcc_present || srmmu_modtype == HyperSparc) {
		dvma_prot = __pgprot(SRMMU_CACHE | SRMMU_ET_PTE | SRMMU_PRIV);
		ioperm_noc = IOPTE_CACHE | IOPTE_WRITE | IOPTE_VALID;
	} else {
		dvma_prot = __pgprot(SRMMU_ET_PTE | SRMMU_PRIV);
		ioperm_noc = IOPTE_WRITE | IOPTE_VALID;
	}
}
