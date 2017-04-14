/*
 *  Meta version derived from arch/powerpc/lib/dma-noncoherent.c
 *    Copyright (C) 2008 Imagination Technologies Ltd.
 *
 *  PowerPC version derived from arch/arm/mm/consistent.c
 *    Copyright (C) 2001 Dan Malek (dmalek@jlc.net)
 *
 *  Copyright (C) 2000 Russell King
 *
 * Consistent memory allocators.  Used for DMA devices that want to
 * share uncached memory with the processor core.  The function return
 * is the virtual address and 'dma_handle' is the physical address.
 * Mostly stolen from the ARM port, with some changes for PowerPC.
 *						-- Dan
 *
 * Reorganized to get rid of the arch-specific consistent_* functions
 * and provide non-coherent implementations for the DMA API. -Matt
 *
 * Added in_interrupt() safe dma_alloc_coherent()/dma_free_coherent()
 * implementation. This is pulled straight from ARM and barely
 * modified. -Matt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>
#include <linux/slab.h>

#include <asm/tlbflush.h>
#include <asm/mmu.h>

#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - CONSISTENT_START) \
					>> PAGE_SHIFT)

static u64 get_coherent_dma_mask(struct device *dev)
{
	u64 mask = ~0ULL;

	if (dev) {
		mask = dev->coherent_dma_mask;

		/*
		 * Sanity check the DMA mask - it must be non-zero, and
		 * must be able to be satisfied by a DMA allocation.
		 */
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			return 0;
		}
	}

	return mask;
}
/*
 * This is the page table (2MB) covering uncached, DMA consistent allocations
 */
static pte_t *consistent_pte;
static DEFINE_SPINLOCK(consistent_lock);

/*
 * VM region handling support.
 *
 * This should become something generic, handling VM region allocations for
 * vmalloc and similar (ioremap, module space, etc).
 *
 * I envisage vmalloc()'s supporting vm_struct becoming:
 *
 *  struct vm_struct {
 *    struct metag_vm_region	region;
 *    unsigned long	flags;
 *    struct page	**pages;
 *    unsigned int	nr_pages;
 *    unsigned long	phys_addr;
 *  };
 *
 * get_vm_area() would then call metag_vm_region_alloc with an appropriate
 * struct metag_vm_region head (eg):
 *
 *  struct metag_vm_region vmalloc_head = {
 *	.vm_list	= LIST_HEAD_INIT(vmalloc_head.vm_list),
 *	.vm_start	= VMALLOC_START,
 *	.vm_end		= VMALLOC_END,
 *  };
 *
 * However, vmalloc_head.vm_start is variable (typically, it is dependent on
 * the amount of RAM found at boot time.)  I would imagine that get_vm_area()
 * would have to initialise this each time prior to calling
 * metag_vm_region_alloc().
 */
struct metag_vm_region {
	struct list_head vm_list;
	unsigned long vm_start;
	unsigned long vm_end;
	struct page		*vm_pages;
	int			vm_active;
};

static struct metag_vm_region consistent_head = {
	.vm_list = LIST_HEAD_INIT(consistent_head.vm_list),
	.vm_start = CONSISTENT_START,
	.vm_end = CONSISTENT_END,
};

static struct metag_vm_region *metag_vm_region_alloc(struct metag_vm_region
						     *head, size_t size,
						     gfp_t gfp)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
	unsigned long flags;
	struct metag_vm_region *c, *new;

	new = kmalloc(sizeof(struct metag_vm_region), gfp);
	if (!new)
		goto out;

	spin_lock_irqsave(&consistent_lock, flags);

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if ((addr + size) < addr)
			goto nospc;
		if ((addr + size) <= c->vm_start)
			goto found;
		addr = c->vm_end;
		if (addr > end)
			goto nospc;
	}

found:
	/*
	 * Insert this entry _before_ the one we found.
	 */
	list_add_tail(&new->vm_list, &c->vm_list);
	new->vm_start = addr;
	new->vm_end = addr + size;
	new->vm_active = 1;

	spin_unlock_irqrestore(&consistent_lock, flags);
	return new;

nospc:
	spin_unlock_irqrestore(&consistent_lock, flags);
	kfree(new);
out:
	return NULL;
}

static struct metag_vm_region *metag_vm_region_find(struct metag_vm_region
						    *head, unsigned long addr)
{
	struct metag_vm_region *c;

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if (c->vm_active && c->vm_start == addr)
			goto out;
	}
	c = NULL;
out:
	return c;
}

/*
 * Allocate DMA-coherent memory space and return both the kernel remapped
 * virtual and bus address for that space.
 */
static void *metag_dma_alloc(struct device *dev, size_t size,
		dma_addr_t *handle, gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	struct metag_vm_region *c;
	unsigned long order;
	u64 mask = get_coherent_dma_mask(dev);
	u64 limit;

	if (!consistent_pte) {
		pr_err("%s: not initialised\n", __func__);
		dump_stack();
		return NULL;
	}

	if (!mask)
		goto no_page;
	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if ((limit && size >= limit)
	    || size >= (CONSISTENT_END - CONSISTENT_START)) {
		pr_warn("coherent allocation too big (requested %#x mask %#Lx)\n",
			size, mask);
		return NULL;
	}

	order = get_order(size);

	if (mask != 0xffffffff)
		gfp |= GFP_DMA;

	page = alloc_pages(gfp, order);
	if (!page)
		goto no_page;

	/*
	 * Invalidate any data that might be lurking in the
	 * kernel direct-mapped region for device DMA.
	 */
	{
		void *kaddr = page_address(page);
		memset(kaddr, 0, size);
		flush_dcache_region(kaddr, size);
	}

	/*
	 * Allocate a virtual address in the consistent mapping region.
	 */
	c = metag_vm_region_alloc(&consistent_head, size,
				  gfp & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (c) {
		unsigned long vaddr = c->vm_start;
		pte_t *pte = consistent_pte + CONSISTENT_OFFSET(vaddr);
		struct page *end = page + (1 << order);

		c->vm_pages = page;
		split_page(page, order);

		/*
		 * Set the "dma handle"
		 */
		*handle = page_to_bus(page);

		do {
			BUG_ON(!pte_none(*pte));

			SetPageReserved(page);
			set_pte_at(&init_mm, vaddr,
				   pte, mk_pte(page,
					       pgprot_writecombine
					       (PAGE_KERNEL)));
			page++;
			pte++;
			vaddr += PAGE_SIZE;
		} while (size -= PAGE_SIZE);

		/*
		 * Free the otherwise unused pages.
		 */
		while (page < end) {
			__free_page(page);
			page++;
		}

		return (void *)c->vm_start;
	}

	if (page)
		__free_pages(page, order);
no_page:
	return NULL;
}

/*
 * free a page as defined by the above mapping.
 */
static void metag_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	struct metag_vm_region *c;
	unsigned long flags, addr;
	pte_t *ptep;

	size = PAGE_ALIGN(size);

	spin_lock_irqsave(&consistent_lock, flags);

	c = metag_vm_region_find(&consistent_head, (unsigned long)vaddr);
	if (!c)
		goto no_area;

	c->vm_active = 0;
	if ((c->vm_end - c->vm_start) != size) {
		pr_err("%s: freeing wrong coherent size (%ld != %d)\n",
		       __func__, c->vm_end - c->vm_start, size);
		dump_stack();
		size = c->vm_end - c->vm_start;
	}

	ptep = consistent_pte + CONSISTENT_OFFSET(c->vm_start);
	addr = c->vm_start;
	do {
		pte_t pte = ptep_get_and_clear(&init_mm, addr, ptep);
		unsigned long pfn;

		ptep++;
		addr += PAGE_SIZE;

		if (!pte_none(pte) && pte_present(pte)) {
			pfn = pte_pfn(pte);

			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);
				__free_reserved_page(page);
				continue;
			}
		}

		pr_crit("%s: bad page in kernel page table\n",
			__func__);
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	list_del(&c->vm_list);

	spin_unlock_irqrestore(&consistent_lock, flags);

	kfree(c);
	return;

no_area:
	spin_unlock_irqrestore(&consistent_lock, flags);
	pr_err("%s: trying to free invalid coherent area: %p\n",
	       __func__, vaddr);
	dump_stack();
}

static int metag_dma_mmap(struct device *dev, struct vm_area_struct *vma,
		void *cpu_addr, dma_addr_t dma_addr, size_t size,
		unsigned long attrs)
{
	unsigned long flags, user_size, kern_size;
	struct metag_vm_region *c;
	int ret = -ENXIO;

	if (attrs & DMA_ATTR_WRITE_COMBINE)
		vma->vm_page_prot = pgprot_writecombine(vma->vm_page_prot);
	else
		vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

	user_size = (vma->vm_end - vma->vm_start) >> PAGE_SHIFT;

	spin_lock_irqsave(&consistent_lock, flags);
	c = metag_vm_region_find(&consistent_head, (unsigned long)cpu_addr);
	spin_unlock_irqrestore(&consistent_lock, flags);

	if (c) {
		unsigned long off = vma->vm_pgoff;

		kern_size = (c->vm_end - c->vm_start) >> PAGE_SHIFT;

		if (off < kern_size &&
		    user_size <= (kern_size - off)) {
			ret = remap_pfn_range(vma, vma->vm_start,
					      page_to_pfn(c->vm_pages) + off,
					      user_size << PAGE_SHIFT,
					      vma->vm_page_prot);
		}
	}


	return ret;
}

/*
 * Initialise the consistent memory allocation.
 */
static int __init dma_alloc_init(void)
{
	pgd_t *pgd, *pgd_k;
	pud_t *pud, *pud_k;
	pmd_t *pmd, *pmd_k;
	pte_t *pte;
	int ret = 0;

	do {
		int offset = pgd_index(CONSISTENT_START);
		pgd = pgd_offset(&init_mm, CONSISTENT_START);
		pud = pud_alloc(&init_mm, pgd, CONSISTENT_START);
		pmd = pmd_alloc(&init_mm, pud, CONSISTENT_START);
		WARN_ON(!pmd_none(*pmd));

		pte = pte_alloc_kernel(pmd, CONSISTENT_START);
		if (!pte) {
			pr_err("%s: no pte tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		pgd_k = ((pgd_t *) mmu_get_base()) + offset;
		pud_k = pud_offset(pgd_k, CONSISTENT_START);
		pmd_k = pmd_offset(pud_k, CONSISTENT_START);
		set_pmd(pmd_k, *pmd);

		consistent_pte = pte;
	} while (0);

	return ret;
}
early_initcall(dma_alloc_init);

/*
 * make an area consistent to devices.
 */
static void dma_sync_for_device(void *vaddr, size_t size, int dma_direction)
{
	/*
	 * Ensure any writes get through the write combiner. This is necessary
	 * even with DMA_FROM_DEVICE, or the write may dirty the cache after
	 * we've invalidated it and get written back during the DMA.
	 */

	barrier();

	switch (dma_direction) {
	case DMA_BIDIRECTIONAL:
		/*
		 * Writeback to ensure the device can see our latest changes and
		 * so that we have no dirty lines, and invalidate the cache
		 * lines too in preparation for receiving the buffer back
		 * (dma_sync_for_cpu) later.
		 */
		flush_dcache_region(vaddr, size);
		break;
	case DMA_TO_DEVICE:
		/*
		 * Writeback to ensure the device can see our latest changes.
		 * There's no need to invalidate as the device shouldn't write
		 * to the buffer.
		 */
		writeback_dcache_region(vaddr, size);
		break;
	case DMA_FROM_DEVICE:
		/*
		 * Invalidate to ensure we have no dirty lines that could get
		 * written back during the DMA. It's also safe to flush
		 * (writeback) here if necessary.
		 */
		invalidate_dcache_region(vaddr, size);
		break;
	case DMA_NONE:
		BUG();
	}

	wmb();
}

/*
 * make an area consistent to the core.
 */
static void dma_sync_for_cpu(void *vaddr, size_t size, int dma_direction)
{
	/*
	 * Hardware L2 cache prefetch doesn't occur across 4K physical
	 * boundaries, however according to Documentation/DMA-API-HOWTO.txt
	 * kmalloc'd memory is DMA'able, so accesses in nearby memory could
	 * trigger a cache fill in the DMA buffer.
	 *
	 * This should never cause dirty lines, so a flush or invalidate should
	 * be safe to allow us to see data from the device.
	 */
	if (_meta_l2c_pf_is_enabled()) {
		switch (dma_direction) {
		case DMA_BIDIRECTIONAL:
		case DMA_FROM_DEVICE:
			invalidate_dcache_region(vaddr, size);
			break;
		case DMA_TO_DEVICE:
			/* The device shouldn't have written to the buffer */
			break;
		case DMA_NONE:
			BUG();
		}
	}

	rmb();
}

static dma_addr_t metag_dma_map_page(struct device *dev, struct page *page,
		unsigned long offset, size_t size,
		enum dma_data_direction direction, unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_sync_for_device((void *)(page_to_phys(page) + offset),
				    size, direction);
	return page_to_phys(page) + offset;
}

static void metag_dma_unmap_page(struct device *dev, dma_addr_t dma_address,
		size_t size, enum dma_data_direction direction,
		unsigned long attrs)
{
	if (!(attrs & DMA_ATTR_SKIP_CPU_SYNC))
		dma_sync_for_cpu(phys_to_virt(dma_address), size, direction);
}

static int metag_dma_map_sg(struct device *dev, struct scatterlist *sglist,
		int nents, enum dma_data_direction direction,
		unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nents, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);

		if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
			continue;

		dma_sync_for_device(sg_virt(sg), sg->length, direction);
	}

	return nents;
}


static void metag_dma_unmap_sg(struct device *dev, struct scatterlist *sglist,
		int nhwentries, enum dma_data_direction direction,
		unsigned long attrs)
{
	struct scatterlist *sg;
	int i;

	for_each_sg(sglist, sg, nhwentries, i) {
		BUG_ON(!sg_page(sg));

		sg->dma_address = sg_phys(sg);

		if (attrs & DMA_ATTR_SKIP_CPU_SYNC)
			continue;

		dma_sync_for_cpu(sg_virt(sg), sg->length, direction);
	}
}

static void metag_dma_sync_single_for_cpu(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	dma_sync_for_cpu(phys_to_virt(dma_handle), size, direction);
}

static void metag_dma_sync_single_for_device(struct device *dev,
		dma_addr_t dma_handle, size_t size,
		enum dma_data_direction direction)
{
	dma_sync_for_device(phys_to_virt(dma_handle), size, direction);
}

static void metag_dma_sync_sg_for_cpu(struct device *dev,
		struct scatterlist *sglist, int nelems,
		enum dma_data_direction direction)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i)
		dma_sync_for_cpu(sg_virt(sg), sg->length, direction);
}

static void metag_dma_sync_sg_for_device(struct device *dev,
		struct scatterlist *sglist, int nelems,
		enum dma_data_direction direction)
{
	int i;
	struct scatterlist *sg;

	for_each_sg(sglist, sg, nelems, i)
		dma_sync_for_device(sg_virt(sg), sg->length, direction);
}

const struct dma_map_ops metag_dma_ops = {
	.alloc			= metag_dma_alloc,
	.free			= metag_dma_free,
	.map_page		= metag_dma_map_page,
	.map_sg			= metag_dma_map_sg,
	.sync_single_for_device	= metag_dma_sync_single_for_device,
	.sync_single_for_cpu	= metag_dma_sync_single_for_cpu,
	.sync_sg_for_cpu	= metag_dma_sync_sg_for_cpu,
	.mmap			= metag_dma_mmap,
};
EXPORT_SYMBOL(metag_dma_ops);
