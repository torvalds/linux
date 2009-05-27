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
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/highmem.h>
#include <linux/dma-mapping.h>

#include <asm/tlbflush.h>

/*
 * This address range defaults to a value that is safe for all
 * platforms which currently set CONFIG_NOT_COHERENT_CACHE. It
 * can be further configured for specific applications under
 * the "Advanced Setup" menu. -Matt
 */
#define CONSISTENT_BASE	(CONFIG_CONSISTENT_START)
#define CONSISTENT_END	(CONFIG_CONSISTENT_START + CONFIG_CONSISTENT_SIZE)
#define CONSISTENT_OFFSET(x)	(((unsigned long)(x) - CONSISTENT_BASE) >> PAGE_SHIFT)

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
void *
__dma_alloc_coherent(size_t size, dma_addr_t *handle, gfp_t gfp)
{
	struct page *page;
	struct ppc_vm_region *c;
	unsigned long order;
	u64 mask = 0x00ffffff, limit; /* ISA default */

	if (!consistent_pte) {
		printk(KERN_ERR "%s: not initialised\n", __func__);
		dump_stack();
		return NULL;
	}

	size = PAGE_ALIGN(size);
	limit = (mask + 1) & ~mask;
	if ((limit && size >= limit) || size >= (CONSISTENT_END - CONSISTENT_BASE)) {
		printk(KERN_WARNING "coherent allocation too big (requested %#x mask %#Lx)\n",
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
		pte_t *pte = consistent_pte + CONSISTENT_OFFSET(vaddr);
		struct page *end = page + (1 << order);

		split_page(page, order);

		/*
		 * Set the "dma handle"
		 */
		*handle = page_to_phys(page);

		do {
			BUG_ON(!pte_none(*pte));

			SetPageReserved(page);
			set_pte_at(&init_mm, vaddr,
				   pte, mk_pte(page, pgprot_noncached(PAGE_KERNEL)));
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
EXPORT_SYMBOL(__dma_alloc_coherent);

/*
 * free a page as defined by the above mapping.
 */
void __dma_free_coherent(size_t size, void *vaddr)
{
	struct ppc_vm_region *c;
	unsigned long flags, addr;
	pte_t *ptep;

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
				ClearPageReserved(page);

				__free_page(page);
				continue;
			}
		}

		printk(KERN_CRIT "%s: bad page in kernel page table\n",
		       __func__);
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
EXPORT_SYMBOL(__dma_free_coherent);

/*
 * Initialise the consistent memory allocation.
 */
static int __init dma_alloc_init(void)
{
	pgd_t *pgd;
	pud_t *pud;
	pmd_t *pmd;
	pte_t *pte;
	int ret = 0;

	do {
		pgd = pgd_offset(&init_mm, CONSISTENT_BASE);
		pud = pud_alloc(&init_mm, pgd, CONSISTENT_BASE);
		pmd = pmd_alloc(&init_mm, pud, CONSISTENT_BASE);
		if (!pmd) {
			printk(KERN_ERR "%s: no pmd tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		pte = pte_alloc_kernel(pmd, CONSISTENT_BASE);
		if (!pte) {
			printk(KERN_ERR "%s: no pte tables\n", __func__);
			ret = -ENOMEM;
			break;
		}

		consistent_pte = pte;
	} while (0);

	return ret;
}

core_initcall(dma_alloc_init);

/*
 * make an area consistent.
 */
void __dma_sync(void *vaddr, size_t size, int direction)
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
		if ((start & (L1_CACHE_BYTES - 1)) || (size & (L1_CACHE_BYTES - 1)))
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
EXPORT_SYMBOL(__dma_sync);

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
		start = (unsigned long)kmap_atomic(page + seg_nr,
				KM_PPC_SYNC_PAGE) + seg_offset;

		/* Sync this buffer segment */
		__dma_sync((void *)start, seg_size, direction);
		kunmap_atomic((void *)start, KM_PPC_SYNC_PAGE);
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
void __dma_sync_page(struct page *page, unsigned long offset,
	size_t size, int direction)
{
#ifdef CONFIG_HIGHMEM
	__dma_sync_page_highmem(page, offset, size, direction);
#else
	unsigned long start = (unsigned long)page_address(page) + offset;
	__dma_sync((void *)start, size, direction);
#endif
}
EXPORT_SYMBOL(__dma_sync_page);
