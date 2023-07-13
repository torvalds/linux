// SPDX-License-Identifier: GPL-2.0
/*
 * io-unit.c:  IO-UNIT specific routines for memory management.
 *
 * Copyright (C) 1997,1998 Jakub Jelinek    (jj@sunsite.mff.cuni.cz)
 */
 
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/bitops.h>
#include <linux/dma-map-ops.h>
#include <linux/of.h>
#include <linux/of_device.h>

#include <asm/io.h>
#include <asm/io-unit.h>
#include <asm/mxcc.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include <asm/dma.h>
#include <asm/oplib.h>

#include "mm_32.h"

/* #define IOUNIT_DEBUG */
#ifdef IOUNIT_DEBUG
#define IOD(x) printk(x)
#else
#define IOD(x) do { } while (0)
#endif

#define IOPERM        (IOUPTE_CACHE | IOUPTE_WRITE | IOUPTE_VALID)
#define MKIOPTE(phys) __iopte((((phys)>>4) & IOUPTE_PAGE) | IOPERM)

static const struct dma_map_ops iounit_dma_ops;

static void __init iounit_iommu_init(struct platform_device *op)
{
	struct iounit_struct *iounit;
	iopte_t __iomem *xpt;
	iopte_t __iomem *xptend;

	iounit = kzalloc(sizeof(struct iounit_struct), GFP_ATOMIC);
	if (!iounit) {
		prom_printf("SUN4D: Cannot alloc iounit, halting.\n");
		prom_halt();
	}

	iounit->limit[0] = IOUNIT_BMAP1_START;
	iounit->limit[1] = IOUNIT_BMAP2_START;
	iounit->limit[2] = IOUNIT_BMAPM_START;
	iounit->limit[3] = IOUNIT_BMAPM_END;
	iounit->rotor[1] = IOUNIT_BMAP2_START;
	iounit->rotor[2] = IOUNIT_BMAPM_START;

	xpt = of_ioremap(&op->resource[2], 0, PAGE_SIZE * 16, "XPT");
	if (!xpt) {
		prom_printf("SUN4D: Cannot map External Page Table.");
		prom_halt();
	}
	
	op->dev.archdata.iommu = iounit;
	iounit->page_table = xpt;
	spin_lock_init(&iounit->lock);

	xptend = iounit->page_table + (16 * PAGE_SIZE) / sizeof(iopte_t);
	for (; xpt < xptend; xpt++)
		sbus_writel(0, xpt);

	op->dev.dma_ops = &iounit_dma_ops;
}

static int __init iounit_init(void)
{
	extern void sun4d_init_sbi_irq(void);
	struct device_node *dp;

	for_each_node_by_name(dp, "sbi") {
		struct platform_device *op = of_find_device_by_node(dp);

		iounit_iommu_init(op);
		of_propagate_archdata(op);
	}

	sun4d_init_sbi_irq();

	return 0;
}

subsys_initcall(iounit_init);

/* One has to hold iounit->lock to call this */
static unsigned long iounit_get_area(struct iounit_struct *iounit, unsigned long vaddr, int size)
{
	int i, j, k, npages;
	unsigned long rotor, scan, limit;
	iopte_t iopte;

        npages = ((vaddr & ~PAGE_MASK) + size + (PAGE_SIZE-1)) >> PAGE_SHIFT;

	/* A tiny bit of magic ingredience :) */
	switch (npages) {
	case 1: i = 0x0231; break;
	case 2: i = 0x0132; break;
	default: i = 0x0213; break;
	}
	
	IOD(("iounit_get_area(%08lx,%d[%d])=", vaddr, size, npages));
	
next:	j = (i & 15);
	rotor = iounit->rotor[j - 1];
	limit = iounit->limit[j];
	scan = rotor;
nexti:	scan = find_next_zero_bit(iounit->bmap, limit, scan);
	if (scan + npages > limit) {
		if (limit != rotor) {
			limit = rotor;
			scan = iounit->limit[j - 1];
			goto nexti;
		}
		i >>= 4;
		if (!(i & 15))
			panic("iounit_get_area: Couldn't find free iopte slots for (%08lx,%d)\n", vaddr, size);
		goto next;
	}
	for (k = 1, scan++; k < npages; k++)
		if (test_bit(scan++, iounit->bmap))
			goto nexti;
	iounit->rotor[j - 1] = (scan < limit) ? scan : iounit->limit[j - 1];
	scan -= npages;
	iopte = MKIOPTE(__pa(vaddr & PAGE_MASK));
	vaddr = IOUNIT_DMA_BASE + (scan << PAGE_SHIFT) + (vaddr & ~PAGE_MASK);
	for (k = 0; k < npages; k++, iopte = __iopte(iopte_val(iopte) + 0x100), scan++) {
		set_bit(scan, iounit->bmap);
		sbus_writel(iopte_val(iopte), &iounit->page_table[scan]);
	}
	IOD(("%08lx\n", vaddr));
	return vaddr;
}

static dma_addr_t iounit_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t len, enum dma_data_direction dir,
		unsigned long attrs)
{
	void *vaddr = page_address(page) + offset;
	struct iounit_struct *iounit = dev->archdata.iommu;
	unsigned long ret, flags;
	
	/* XXX So what is maxphys for us and how do drivers know it? */
	if (!len || len > 256 * 1024)
		return DMA_MAPPING_ERROR;

	spin_lock_irqsave(&iounit->lock, flags);
	ret = iounit_get_area(iounit, (unsigned long)vaddr, len);
	spin_unlock_irqrestore(&iounit->lock, flags);
	return ret;
}

static int iounit_map_sg(struct device *dev, struct scatterlist *sgl, int nents,
		enum dma_data_direction dir, unsigned long attrs)
{
	struct iounit_struct *iounit = dev->archdata.iommu;
	struct scatterlist *sg;
	unsigned long flags;
	int i;

	/* FIXME: Cache some resolved pages - often several sg entries are to the same page */
	spin_lock_irqsave(&iounit->lock, flags);
	for_each_sg(sgl, sg, nents, i) {
		sg->dma_address = iounit_get_area(iounit, (unsigned long) sg_virt(sg), sg->length);
		sg->dma_length = sg->length;
	}
	spin_unlock_irqrestore(&iounit->lock, flags);
	return nents;
}

static void iounit_unmap_page(struct device *dev, dma_addr_t vaddr, size_t len,
		enum dma_data_direction dir, unsigned long attrs)
{
	struct iounit_struct *iounit = dev->archdata.iommu;
	unsigned long flags;
	
	spin_lock_irqsave(&iounit->lock, flags);
	len = ((vaddr & ~PAGE_MASK) + len + (PAGE_SIZE-1)) >> PAGE_SHIFT;
	vaddr = (vaddr - IOUNIT_DMA_BASE) >> PAGE_SHIFT;
	IOD(("iounit_release %08lx-%08lx\n", (long)vaddr, (long)len+vaddr));
	for (len += vaddr; vaddr < len; vaddr++)
		clear_bit(vaddr, iounit->bmap);
	spin_unlock_irqrestore(&iounit->lock, flags);
}

static void iounit_unmap_sg(struct device *dev, struct scatterlist *sgl,
		int nents, enum dma_data_direction dir, unsigned long attrs)
{
	struct iounit_struct *iounit = dev->archdata.iommu;
	unsigned long flags, vaddr, len;
	struct scatterlist *sg;
	int i;

	spin_lock_irqsave(&iounit->lock, flags);
	for_each_sg(sgl, sg, nents, i) {
		len = ((sg->dma_address & ~PAGE_MASK) + sg->length + (PAGE_SIZE-1)) >> PAGE_SHIFT;
		vaddr = (sg->dma_address - IOUNIT_DMA_BASE) >> PAGE_SHIFT;
		IOD(("iounit_release %08lx-%08lx\n", (long)vaddr, (long)len+vaddr));
		for (len += vaddr; vaddr < len; vaddr++)
			clear_bit(vaddr, iounit->bmap);
	}
	spin_unlock_irqrestore(&iounit->lock, flags);
}

#ifdef CONFIG_SBUS
static void *iounit_alloc(struct device *dev, size_t len,
		dma_addr_t *dma_handle, gfp_t gfp, unsigned long attrs)
{
	struct iounit_struct *iounit = dev->archdata.iommu;
	unsigned long va, addr, page, end, ret;
	pgprot_t dvma_prot;
	iopte_t __iomem *iopte;

	/* XXX So what is maxphys for us and how do drivers know it? */
	if (!len || len > 256 * 1024)
		return NULL;

	len = PAGE_ALIGN(len);
	va = __get_free_pages(gfp | __GFP_ZERO, get_order(len));
	if (!va)
		return NULL;

	addr = ret = sparc_dma_alloc_resource(dev, len);
	if (!addr)
		goto out_free_pages;
	*dma_handle = addr;

	dvma_prot = __pgprot(SRMMU_CACHE | SRMMU_ET_PTE | SRMMU_PRIV);
	end = PAGE_ALIGN((addr + len));
	while(addr < end) {
		page = va;
		{
			pmd_t *pmdp;
			pte_t *ptep;
			long i;

			pmdp = pmd_off_k(addr);
			ptep = pte_offset_kernel(pmdp, addr);

			set_pte(ptep, mk_pte(virt_to_page(page), dvma_prot));

			i = ((addr - IOUNIT_DMA_BASE) >> PAGE_SHIFT);

			iopte = iounit->page_table + i;
			sbus_writel(iopte_val(MKIOPTE(__pa(page))), iopte);
		}
		addr += PAGE_SIZE;
		va += PAGE_SIZE;
	}
	flush_cache_all();
	flush_tlb_all();

	return (void *)ret;

out_free_pages:
	free_pages(va, get_order(len));
	return NULL;
}

static void iounit_free(struct device *dev, size_t size, void *cpu_addr,
		dma_addr_t dma_addr, unsigned long attrs)
{
	/* XXX Somebody please fill this in */
}
#endif

static const struct dma_map_ops iounit_dma_ops = {
#ifdef CONFIG_SBUS
	.alloc			= iounit_alloc,
	.free			= iounit_free,
#endif
	.map_page		= iounit_map_page,
	.unmap_page		= iounit_unmap_page,
	.map_sg			= iounit_map_sg,
	.unmap_sg		= iounit_unmap_sg,
};
