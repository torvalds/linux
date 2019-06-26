/*
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
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/dma-direct.h>
#include <linux/dma-noncoherent.h>
#include <linux/export.h>

#include <asm/tlbflush.h>
#include <asm/dma.h>

#include <mm/mmu_decl.h>

/*
 * This address range defaults to a value that is safe for all
 * platforms which currently set CONFIG_NOT_COHERENT_CACHE. It
 * can be further configured for specific applications under
 * the "Advanced Setup" menu. -Matt
 */
#define CONSISTENT_BASE		(IOREMAP_TOP)
#define CONSISTENT_END 		(CONSISTENT_BASE + CONFIG_CONSISTENT_SIZE)
#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - CONSISTENT_BASE) >> PAGE_SHIFT)

/*
 * This is the page table (2MB) covering uncached, DMA consistent allocations
 */
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
 *    struct vm_region	region;
 *    unsigned long	flags;
 *    struct page	**pages;
 *    unsigned int	nr_pages;
 *    unsigned long	phys_addr;
 *  };
 *
 * get_vm_area() would then call vm_region_alloc with an appropriate
 * struct vm_region head (eg):
 *
 *  struct vm_region vmalloc_head = {
 *	.vm_list	= LIST_HEAD_INIT(vmalloc_head.vm_list),
 *	.vm_start	= VMALLOC_START,
 *	.vm_end		= VMALLOC_END,
 *  };
 *
 * However, vmalloc_head.vm_start is variable (typically, it is dependent on
 * the amount of RAM found at boot time.)  I would imagine that get_vm_area()
 * would have to initialise this each time prior to calling vm_region_alloc().
 */
struct ppc_vm_region {
	struct list_head	vm_list;
	unsigned long		vm_start;
	unsigned long		vm_end;
};

static struct ppc_vm_region consistent_head = {
	.vm_list	= LIST_HEAD_INIT(consistent_head.vm_list),
	.vm_start	= CONSISTENT_BASE,
	.vm_end		= CONSISTENT_END,
};

static struct ppc_vm_region *
ppc_vm_region_alloc(struct ppc_vm_region *head, size_t size, gfp_t gfp)
{
	unsigned long addr = head->vm_start, end = head->vm_end - size;
	unsigned long flags;
	struct ppc_vm_region *c, *new;

	new = kmalloc(sizeof(struct ppc_vm_region), gfp);
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

	spin_unlock_irqrestore(&consistent_lock, flags);
	return new;

 nospc:
	spin_unlock_irqrestore(&consistent_lock, flags);
	kfree(new);
 out:
	return NULL;
}

static struct ppc_vm_region *ppc_vm_region_find(struct ppc_vm_region *head, unsigned long addr)
{
	struct ppc_vm_region *c;

	list_for_each_entry(c, &head->vm_list, vm_list) {
		if (c->vm_start == addr)
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
void *arch_dma_alloc(struct device *dev, size_t size, dma_addr_t *dma_handle,
		gfp_t gfp, unsigned long attrs)
{
	struct page *page;
	struct ppc_vm_region *c;
	unsigned long order;
	u64 mask = ISA_DMA_THRESHOLD, limit;

	if (dev) {
		mask = dev->coherent_dma_mask;

		/*
		 * Sanity check the DMA mask - it must be non-zero, and
		 * must be able to be satisfied by a DMA allocation.
		 */
		if (mask == 0) {
			dev_warn(dev, "coherent DMA mask is unset\n");
			goto no_page;
		}

		if ((~mask) & ISA_DMA_THRESHOLD) {
			dev_warn(dev, "coherent DMA mask %#llx is smaller "
				 "than system GFP_DMA mask %#llx\n",
				 mask, (unsigned long long)ISA_DMA_THRESHOLD);
			goto no_page;
		}
	}


	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if ((limit && size >= limit) ||
	    size >= (CONSISTENT_END - CONSISTENT_BASE)) {
		printk(KERN_WARNING "coherent allocation too big (requested %#x mask %#Lx)\n",
		       size, mask);
		return NULL;
	}

	order = get_order(size);

	/* Might be useful if we ever have a real legacy DMA zone... */
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
		unsigned long kaddr = (unsigned long)page_address(page);
		memset(page_address(page), 0, size);
		flush_dcache_range(kaddr, kaddr + size);
	}

	/*
	 * Allocate a virtual address in the consistent mapping region.
	 */
	c = ppc_vm_region_alloc(&consistent_head, size,
			    gfp & ~(__GFP_DMA | __GFP_HIGHMEM));
	if (c) {
		unsigned long vaddr = c->vm_start;
		struct page *end = page + (1 << order);

		split_page(page, order);

		/*
		 * Set the "dma handle"
		 */
		*dma_handle = phys_to_dma(dev, page_to_phys(page));

		do {
			SetPageReserved(page);
			map_kernel_page(vaddr, page_to_phys(page),
					pgprot_noncached(PAGE_KERNEL));
			page++;
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
void arch_dma_free(struct device *dev, size_t size, void *vaddr,
		dma_addr_t dma_handle, unsigned long attrs)
{
	struct ppc_vm_region *c;
	unsigned long flags, addr;
	
	size = PAGE_ALIGN(size);

	spin_lock_irqsave(&consistent_lock, flags);

	c = ppc_vm_region_find(&consistent_head, (unsigned long)vaddr);
	if (!c)
		goto no_area;

	if ((c->vm_end - c->vm_start) != size) {
		printk(KERN_ERR "%s: freeing wrong coherent size (%ld != %d)\n",
		       __func__, c->vm_end - c->vm_start, size);
		dump_stack();
		size = c->vm_end - c->vm_start;
	}

	addr = c->vm_start;
	do {
		pte_t *ptep;
		unsigned long pfn;

		ptep = pte_offset_kernel(pmd_offset(pud_offset(pgd_offset_k(addr),
							       addr),
						    addr),
					 addr);
		if (!pte_none(*ptep) && pte_present(*ptep)) {
			pfn = pte_pfn(*ptep);
			pte_clear(&init_mm, addr, ptep);
			if (pfn_valid(pfn)) {
				struct page *page = pfn_to_page(pfn);
				__free_reserved_page(page);
			}
		}
		addr += PAGE_SIZE;
	} while (size -= PAGE_SIZE);

	flush_tlb_kernel_range(c->vm_start, c->vm_end);

	list_del(&c->vm_list);

	spin_unlock_irqrestore(&consistent_lock, flags);

	kfree(c);
	return;

 no_area:
	spin_unlock_irqrestore(&consistent_lock, flags);
	printk(KERN_ERR "%s: trying to free invalid coherent area: %p\n",
	       __func__, vaddr);
	dump_stack();
}

/*
 * make an area consistent.
 */
static void __dma_sync(void *vaddr, size_t size, int direction)
{
	unsigned long start = (unsigned long)vaddr;
	unsigned long end   = start + size;

	switch (direction) {
	case DMA_NONE:
		BUG();
	case DMA_FROM_DEVICE:
		/*
		 * invalidate only when cache-line aligned otherwise there is
		 * the potential for discarding uncommitted data from the cache
		 */
		if ((start | end) & (L1_CACHE_BYTES - 1))
			flush_dcache_range(start, end);
		else
			invalidate_dcache_range(start, end);
		break;
	case DMA_TO_DEVICE:		/* writeback only */
		clean_dcache_range(start, end);
		break;
	case DMA_BIDIRECTIONAL:	/* writeback and invalidate */
		flush_dcache_range(start, end);
		break;
	}
}

#ifdef CONFIG_HIGHMEM
/*
 * __dma_sync_page() implementation for systems using highmem.
 * In this case, each page of a buffer must be kmapped/kunmapped
 * in order to have a virtual address for __dma_sync(). This must
 * not sleep so kmap_atomic()/kunmap_atomic() are used.
 *
 * Note: yes, it is possible and correct to have a buffer extend
 * beyond the first page.
 */
static inline void __dma_sync_page_highmem(struct page *page,
		unsigned long offset, size_t size, int direction)
{
	size_t seg_size = min((size_t)(PAGE_SIZE - offset), size);
	size_t cur_size = seg_size;
	unsigned long flags, start, seg_offset = offset;
	int nr_segs = 1 + ((size - seg_size) + PAGE_SIZE - 1)/PAGE_SIZE;
	int seg_nr = 0;

	local_irq_save(flags);

	do {
		start = (unsigned long)kmap_atomic(page + seg_nr) + seg_offset;

		/* Sync this buffer segment */
		__dma_sync((void *)start, seg_size, direction);
		kunmap_atomic((void *)start);
		seg_nr++;

		/* Calculate next buffer segment size */
		seg_size = min((size_t)PAGE_SIZE, size - cur_size);

		/* Add the segment size to our running total */
		cur_size += seg_size;
		seg_offset = 0;
	} while (seg_nr < nr_segs);

	local_irq_restore(flags);
}
#endif /* CONFIG_HIGHMEM */

/*
 * __dma_sync_page makes memory consistent. identical to __dma_sync, but
 * takes a struct page instead of a virtual address
 */
static void __dma_sync_page(phys_addr_t paddr, size_t size, int dir)
{
	struct page *page = pfn_to_page(paddr >> PAGE_SHIFT);
	unsigned offset = paddr & ~PAGE_MASK;

#ifdef CONFIG_HIGHMEM
	__dma_sync_page_highmem(page, offset, size, dir);
#else
	unsigned long start = (unsigned long)page_address(page) + offset;
	__dma_sync((void *)start, size, dir);
#endif
}

void arch_sync_dma_for_device(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_sync_page(paddr, size, dir);
}

void arch_sync_dma_for_cpu(struct device *dev, phys_addr_t paddr,
		size_t size, enum dma_data_direction dir)
{
	__dma_sync_page(paddr, size, dir);
}

/*
 * Return the PFN for a given cpu virtual address returned by arch_dma_alloc.
 */
long arch_dma_coherent_to_pfn(struct device *dev, void *vaddr,
		dma_addr_t dma_addr)
{
	/* This should always be populated, so we don't test every
	 * level. If that fails, we'll have a nice crash which
	 * will be as good as a BUG_ON()
	 */
	unsigned long cpu_addr = (unsigned long)vaddr;
	pgd_t *pgd = pgd_offset_k(cpu_addr);
	pud_t *pud = pud_offset(pgd, cpu_addr);
	pmd_t *pmd = pmd_offset(pud, cpu_addr);
	pte_t *ptep = pte_offset_kernel(pmd, cpu_addr);

	if (pte_none(*ptep) || !pte_present(*ptep))
		return 0;
	return pte_pfn(*ptep);
}
