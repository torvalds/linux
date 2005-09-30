/*
 *  linux/arch/arm/mm/flush.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/cacheflush.h>
#include <asm/system.h>
#include <asm/tlbflush.h>

#ifdef CONFIG_CPU_CACHE_VIPT

#define ALIAS_FLUSH_START	0xffff4000

#define TOP_PTE(x)	pte_offset_kernel(top_pmd, x)

static void flush_pfn_alias(unsigned long pfn, unsigned long vaddr)
{
	unsigned long to = ALIAS_FLUSH_START + (CACHE_COLOUR(vaddr) << PAGE_SHIFT);

	set_pte(TOP_PTE(to), pfn_pte(pfn, PAGE_KERNEL));
	flush_tlb_kernel_page(to);

	asm(	"mcrr	p15, 0, %1, %0, c14\n"
	"	mcrr	p15, 0, %1, %0, c5\n"
	    :
	    : "r" (to), "r" (to + PAGE_SIZE - L1_CACHE_BYTES)
	    : "cc");
}

void flush_cache_mm(struct mm_struct *mm)
{
	if (cache_is_vivt()) {
		if (cpu_isset(smp_processor_id(), mm->cpu_vm_mask))
			__cpuc_flush_user_all();
		return;
	}

	if (cache_is_vipt_aliasing()) {
		asm(	"mcr	p15, 0, %0, c7, c14, 0\n"
		"	mcr	p15, 0, %0, c7, c5, 0\n"
		"	mcr	p15, 0, %0, c7, c10, 4"
		    :
		    : "r" (0)
		    : "cc");
	}
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	if (cache_is_vivt()) {
		if (cpu_isset(smp_processor_id(), vma->vm_mm->cpu_vm_mask))
			__cpuc_flush_user_range(start & PAGE_MASK, PAGE_ALIGN(end),
						vma->vm_flags);
		return;
	}

	if (cache_is_vipt_aliasing()) {
		asm(	"mcr	p15, 0, %0, c7, c14, 0\n"
		"	mcr	p15, 0, %0, c7, c5, 0\n"
		"	mcr	p15, 0, %0, c7, c10, 4"
		    :
		    : "r" (0)
		    : "cc");
	}
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr, unsigned long pfn)
{
	if (cache_is_vivt()) {
		if (cpu_isset(smp_processor_id(), vma->vm_mm->cpu_vm_mask)) {
			unsigned long addr = user_addr & PAGE_MASK;
			__cpuc_flush_user_range(addr, addr + PAGE_SIZE, vma->vm_flags);
		}
		return;
	}

	if (cache_is_vipt_aliasing())
		flush_pfn_alias(pfn, user_addr);
}
#else
#define flush_pfn_alias(pfn,vaddr)	do { } while (0)
#endif

void __flush_dcache_page(struct address_space *mapping, struct page *page)
{
	/*
	 * Writeback any data associated with the kernel mapping of this
	 * page.  This ensures that data in the physical page is mutually
	 * coherent with the kernels mapping.
	 */
	__cpuc_flush_dcache_page(page_address(page));

	/*
	 * If this is a page cache page, and we have an aliasing VIPT cache,
	 * we only need to do one flush - which would be at the relevant
	 * userspace colour, which is congruent with page->index.
	 */
	if (mapping && cache_is_vipt_aliasing())
		flush_pfn_alias(page_to_pfn(page),
				page->index << PAGE_CACHE_SHIFT);
}

static void __flush_dcache_aliases(struct address_space *mapping, struct page *page)
{
	struct mm_struct *mm = current->active_mm;
	struct vm_area_struct *mpnt;
	struct prio_tree_iter iter;
	pgoff_t pgoff;

	/*
	 * There are possible user space mappings of this page:
	 * - VIVT cache: we need to also write back and invalidate all user
	 *   data in the current VM view associated with this page.
	 * - aliasing VIPT: we only need to find one mapping of this page.
	 */
	pgoff = page->index << (PAGE_CACHE_SHIFT - PAGE_SHIFT);

	flush_dcache_mmap_lock(mapping);
	vma_prio_tree_foreach(mpnt, &iter, &mapping->i_mmap, pgoff, pgoff) {
		unsigned long offset;

		/*
		 * If this VMA is not in our MM, we can ignore it.
		 */
		if (mpnt->vm_mm != mm)
			continue;
		if (!(mpnt->vm_flags & VM_MAYSHARE))
			continue;
		offset = (pgoff - mpnt->vm_pgoff) << PAGE_SHIFT;
		flush_cache_page(mpnt, mpnt->vm_start + offset, page_to_pfn(page));
	}
	flush_dcache_mmap_unlock(mapping);
}

/*
 * Ensure cache coherency between kernel mapping and userspace mapping
 * of this page.
 *
 * We have three cases to consider:
 *  - VIPT non-aliasing cache: fully coherent so nothing required.
 *  - VIVT: fully aliasing, so we need to handle every alias in our
 *          current VM view.
 *  - VIPT aliasing: need to handle one alias in our current VM view.
 *
 * If we need to handle aliasing:
 *  If the page only exists in the page cache and there are no user
 *  space mappings, we can be lazy and remember that we may have dirty
 *  kernel cache lines for later.  Otherwise, we assume we have
 *  aliasing mappings.
 */
void flush_dcache_page(struct page *page)
{
	struct address_space *mapping = page_mapping(page);

	if (mapping && !mapping_mapped(mapping))
		set_bit(PG_dcache_dirty, &page->flags);
	else {
		__flush_dcache_page(mapping, page);
		if (mapping && cache_is_vivt())
			__flush_dcache_aliases(mapping, page);
	}
}
EXPORT_SYMBOL(flush_dcache_page);
