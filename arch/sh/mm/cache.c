/*
 * arch/sh/mm/pg-mmu.c
 *
 * Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 * Copyright (C) 2002 - 2009  Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long vaddr, void *dst, const void *src,
		       unsigned long len)
{
	if (boot_cpu_data.dcache.n_aliases && page_mapped(page) &&
	    !test_bit(PG_dcache_dirty, &page->flags)) {
		void *vto = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(vto, src, len);
		kunmap_coherent();
	} else {
		memcpy(dst, src, len);
		if (boot_cpu_data.dcache.n_aliases)
			set_bit(PG_dcache_dirty, &page->flags);
	}

	if (vma->vm_flags & VM_EXEC)
		flush_cache_page(vma, vaddr, page_to_pfn(page));
}

void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
			 unsigned long vaddr, void *dst, const void *src,
			 unsigned long len)
{
	if (boot_cpu_data.dcache.n_aliases && page_mapped(page) &&
	    !test_bit(PG_dcache_dirty, &page->flags)) {
		void *vfrom = kmap_coherent(page, vaddr) + (vaddr & ~PAGE_MASK);
		memcpy(dst, vfrom, len);
		kunmap_coherent();
	} else {
		memcpy(dst, src, len);
		if (boot_cpu_data.dcache.n_aliases)
			set_bit(PG_dcache_dirty, &page->flags);
	}
}

void copy_user_highpage(struct page *to, struct page *from,
			unsigned long vaddr, struct vm_area_struct *vma)
{
	void *vfrom, *vto;

	vto = kmap_atomic(to, KM_USER1);

	if (boot_cpu_data.dcache.n_aliases && page_mapped(from) &&
	    !test_bit(PG_dcache_dirty, &from->flags)) {
		vfrom = kmap_coherent(from, vaddr);
		copy_page(vto, vfrom);
		kunmap_coherent();
	} else {
		vfrom = kmap_atomic(from, KM_USER0);
		copy_page(vto, vfrom);
		kunmap_atomic(vfrom, KM_USER0);
	}

	if (pages_do_alias((unsigned long)vto, vaddr & PAGE_MASK))
		__flush_wback_region(vto, PAGE_SIZE);

	kunmap_atomic(vto, KM_USER1);
	/* Make sure this page is cleared on other CPU's too before using it */
	smp_wmb();
}
EXPORT_SYMBOL(copy_user_highpage);

void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	void *kaddr = kmap_atomic(page, KM_USER0);

	clear_page(kaddr);

	if (pages_do_alias((unsigned long)kaddr, vaddr & PAGE_MASK))
		__flush_wback_region(kaddr, PAGE_SIZE);

	kunmap_atomic(kaddr, KM_USER0);
}
EXPORT_SYMBOL(clear_user_highpage);

void __update_cache(struct vm_area_struct *vma,
		    unsigned long address, pte_t pte)
{
	struct page *page;
	unsigned long pfn = pte_pfn(pte);

	if (!boot_cpu_data.dcache.n_aliases)
		return;

	page = pfn_to_page(pfn);
	if (pfn_valid(pfn) && page_mapping(page)) {
		int dirty = test_and_clear_bit(PG_dcache_dirty, &page->flags);
		if (dirty) {
			unsigned long addr = (unsigned long)page_address(page);

			if (pages_do_alias(addr, address & PAGE_MASK))
				__flush_wback_region((void *)addr, PAGE_SIZE);
		}
	}
}

void __flush_anon_page(struct page *page, unsigned long vmaddr)
{
	unsigned long addr = (unsigned long) page_address(page);

	if (pages_do_alias(addr, vmaddr)) {
		if (boot_cpu_data.dcache.n_aliases && page_mapped(page) &&
		    !test_bit(PG_dcache_dirty, &page->flags)) {
			void *kaddr;

			kaddr = kmap_coherent(page, vmaddr);
			__flush_wback_region((void *)kaddr, PAGE_SIZE);
			kunmap_coherent();
		} else
			__flush_wback_region((void *)addr, PAGE_SIZE);
	}
}

void __init cpu_cache_init(void)
{
	if ((boot_cpu_data.family == CPU_FAMILY_SH4) ||
	    (boot_cpu_data.family == CPU_FAMILY_SH4A) ||
	    (boot_cpu_data.family == CPU_FAMILY_SH4AL_DSP)) {
		extern void __weak sh4_cache_init(void);

		sh4_cache_init();
	}
}
