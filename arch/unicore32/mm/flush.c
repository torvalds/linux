// SPDX-License-Identifier: GPL-2.0-only
/*
 * linux/arch/unicore32/mm/flush.c
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#include <linux/module.h>
#include <linux/mm.h>
#include <linux/pagemap.h>

#include <asm/cacheflush.h>
#include <asm/tlbflush.h>

void flush_cache_mm(struct mm_struct *mm)
{
}

void flush_cache_range(struct vm_area_struct *vma, unsigned long start,
		unsigned long end)
{
	if (vma->vm_flags & VM_EXEC)
		__flush_icache_all();
}

void flush_cache_page(struct vm_area_struct *vma, unsigned long user_addr,
		unsigned long pfn)
{
}

static void flush_ptrace_access(struct vm_area_struct *vma, struct page *page,
			 unsigned long uaddr, void *kaddr, unsigned long len)
{
	/* VIPT non-aliasing D-cache */
	if (vma->vm_flags & VM_EXEC) {
		unsigned long addr = (unsigned long)kaddr;

		__cpuc_coherent_kern_range(addr, addr + len);
	}
}

/*
 * Copy user data from/to a page which is mapped into a different
 * processes address space.  Really, we want to allow our "user
 * space" model to handle this.
 *
 * Note that this code needs to run on the current CPU.
 */
void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long uaddr, void *dst, const void *src,
		       unsigned long len)
{
	memcpy(dst, src, len);
	flush_ptrace_access(vma, page, uaddr, dst, len);
}

void __flush_dcache_page(struct address_space *mapping, struct page *page)
{
	/*
	 * Writeback any data associated with the kernel mapping of this
	 * page.  This ensures that data in the physical page is mutually
	 * coherent with the kernels mapping.
	 */
	__cpuc_flush_kern_dcache_area(page_address(page), PAGE_SIZE);
}

/*
 * Ensure cache coherency between kernel mapping and userspace mapping
 * of this page.
 */
void flush_dcache_page(struct page *page)
{
	struct address_space *mapping;

	/*
	 * The zero page is never written to, so never has any dirty
	 * cache lines, and therefore never needs to be flushed.
	 */
	if (page == ZERO_PAGE(0))
		return;

	mapping = page_mapping_file(page);

	if (mapping && !mapping_mapped(mapping))
		clear_bit(PG_dcache_clean, &page->flags);
	else {
		__flush_dcache_page(mapping, page);
		if (mapping)
			__flush_icache_all();
		set_bit(PG_dcache_clean, &page->flags);
	}
}
EXPORT_SYMBOL(flush_dcache_page);
