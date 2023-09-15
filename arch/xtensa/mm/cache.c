/*
 * arch/xtensa/mm/cache.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2006 Tensilica Inc.
 *
 * Chris Zankel	<chris@zankel.net>
 * Joe Taylor
 * Marc Gauthier
 *
 */

#include <linux/init.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/memblock.h>
#include <linux/swap.h>
#include <linux/pagemap.h>
#include <linux/pgtable.h>

#include <asm/bootparam.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/tlbflush.h>
#include <asm/page.h>

/* 
 * Note:
 * The kernel provides one architecture bit PG_arch_1 in the page flags that 
 * can be used for cache coherency.
 *
 * I$-D$ coherency.
 *
 * The Xtensa architecture doesn't keep the instruction cache coherent with
 * the data cache. We use the architecture bit to indicate if the caches
 * are coherent. The kernel clears this bit whenever a page is added to the
 * page cache. At that time, the caches might not be in sync. We, therefore,
 * define this flag as 'clean' if set.
 *
 * D-cache aliasing.
 *
 * With cache aliasing, we have to always flush the cache when pages are
 * unmapped (see tlb_start_vma(). So, we use this flag to indicate a dirty
 * page.
 * 
 *
 *
 */

#if (DCACHE_WAY_SIZE > PAGE_SIZE)
static inline void kmap_invalidate_coherent(struct page *page,
					    unsigned long vaddr)
{
	if (!DCACHE_ALIAS_EQ(page_to_phys(page), vaddr)) {
		unsigned long kvaddr;

		if (!PageHighMem(page)) {
			kvaddr = (unsigned long)page_to_virt(page);

			__invalidate_dcache_page(kvaddr);
		} else {
			kvaddr = TLBTEMP_BASE_1 +
				(page_to_phys(page) & DCACHE_ALIAS_MASK);

			preempt_disable();
			__invalidate_dcache_page_alias(kvaddr,
						       page_to_phys(page));
			preempt_enable();
		}
	}
}

static inline void *coherent_kvaddr(struct page *page, unsigned long base,
				    unsigned long vaddr, unsigned long *paddr)
{
	*paddr = page_to_phys(page);
	return (void *)(base + (vaddr & DCACHE_ALIAS_MASK));
}

void clear_user_highpage(struct page *page, unsigned long vaddr)
{
	unsigned long paddr;
	void *kvaddr = coherent_kvaddr(page, TLBTEMP_BASE_1, vaddr, &paddr);

	preempt_disable();
	kmap_invalidate_coherent(page, vaddr);
	set_bit(PG_arch_1, &page->flags);
	clear_page_alias(kvaddr, paddr);
	preempt_enable();
}
EXPORT_SYMBOL(clear_user_highpage);

void copy_user_highpage(struct page *dst, struct page *src,
			unsigned long vaddr, struct vm_area_struct *vma)
{
	unsigned long dst_paddr, src_paddr;
	void *dst_vaddr = coherent_kvaddr(dst, TLBTEMP_BASE_1, vaddr,
					  &dst_paddr);
	void *src_vaddr = coherent_kvaddr(src, TLBTEMP_BASE_2, vaddr,
					  &src_paddr);

	preempt_disable();
	kmap_invalidate_coherent(dst, vaddr);
	set_bit(PG_arch_1, &dst->flags);
	copy_page_alias(dst_vaddr, src_vaddr, dst_paddr, src_paddr);
	preempt_enable();
}
EXPORT_SYMBOL(copy_user_highpage);

/*
 * Any time the kernel writes to a user page cache page, or it is about to
 * read from a page cache page this routine is called.
 *
 */

void flush_dcache_folio(struct folio *folio)
{
	struct address_space *mapping = folio_flush_mapping(folio);

	/*
	 * If we have a mapping but the page is not mapped to user-space
	 * yet, we simply mark this page dirty and defer flushing the 
	 * caches until update_mmu().
	 */

	if (mapping && !mapping_mapped(mapping)) {
		if (!test_bit(PG_arch_1, &folio->flags))
			set_bit(PG_arch_1, &folio->flags);
		return;

	} else {
		unsigned long phys = folio_pfn(folio) * PAGE_SIZE;
		unsigned long temp = folio_pos(folio);
		unsigned int i, nr = folio_nr_pages(folio);
		unsigned long alias = !(DCACHE_ALIAS_EQ(temp, phys));
		unsigned long virt;

		/* 
		 * Flush the page in kernel space and user space.
		 * Note that we can omit that step if aliasing is not
		 * an issue, but we do have to synchronize I$ and D$
		 * if we have a mapping.
		 */

		if (!alias && !mapping)
			return;

		preempt_disable();
		for (i = 0; i < nr; i++) {
			virt = TLBTEMP_BASE_1 + (phys & DCACHE_ALIAS_MASK);
			__flush_invalidate_dcache_page_alias(virt, phys);

			virt = TLBTEMP_BASE_1 + (temp & DCACHE_ALIAS_MASK);

			if (alias)
				__flush_invalidate_dcache_page_alias(virt, phys);

			if (mapping)
				__invalidate_icache_page_alias(virt, phys);
			phys += PAGE_SIZE;
			temp += PAGE_SIZE;
		}
		preempt_enable();
	}

	/* There shouldn't be an entry in the cache for this page anymore. */
}
EXPORT_SYMBOL(flush_dcache_folio);

/*
 * For now, flush the whole cache. FIXME??
 */

void local_flush_cache_range(struct vm_area_struct *vma,
		       unsigned long start, unsigned long end)
{
	__flush_invalidate_dcache_all();
	__invalidate_icache_all();
}
EXPORT_SYMBOL(local_flush_cache_range);

/* 
 * Remove any entry in the cache for this page. 
 *
 * Note that this function is only called for user pages, so use the
 * alias versions of the cache flush functions.
 */

void local_flush_cache_page(struct vm_area_struct *vma, unsigned long address,
		      unsigned long pfn)
{
	/* Note that we have to use the 'alias' address to avoid multi-hit */

	unsigned long phys = page_to_phys(pfn_to_page(pfn));
	unsigned long virt = TLBTEMP_BASE_1 + (address & DCACHE_ALIAS_MASK);

	preempt_disable();
	__flush_invalidate_dcache_page_alias(virt, phys);
	__invalidate_icache_page_alias(virt, phys);
	preempt_enable();
}
EXPORT_SYMBOL(local_flush_cache_page);

#endif /* DCACHE_WAY_SIZE > PAGE_SIZE */

void update_mmu_cache_range(struct vm_fault *vmf, struct vm_area_struct *vma,
		unsigned long addr, pte_t *ptep, unsigned int nr)
{
	unsigned long pfn = pte_pfn(*ptep);
	struct folio *folio;
	unsigned int i;

	if (!pfn_valid(pfn))
		return;

	folio = page_folio(pfn_to_page(pfn));

	/* Invalidate old entries in TLBs */
	for (i = 0; i < nr; i++)
		flush_tlb_page(vma, addr + i * PAGE_SIZE);
	nr = folio_nr_pages(folio);

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

	if (!folio_test_reserved(folio) && test_bit(PG_arch_1, &folio->flags)) {
		unsigned long phys = folio_pfn(folio) * PAGE_SIZE;
		unsigned long tmp;

		preempt_disable();
		for (i = 0; i < nr; i++) {
			tmp = TLBTEMP_BASE_1 + (phys & DCACHE_ALIAS_MASK);
			__flush_invalidate_dcache_page_alias(tmp, phys);
			tmp = TLBTEMP_BASE_1 + (addr & DCACHE_ALIAS_MASK);
			__flush_invalidate_dcache_page_alias(tmp, phys);
			__invalidate_icache_page_alias(tmp, phys);
			phys += PAGE_SIZE;
		}
		preempt_enable();

		clear_bit(PG_arch_1, &folio->flags);
	}
#else
	if (!folio_test_reserved(folio) && !test_bit(PG_arch_1, &folio->flags)
	    && (vma->vm_flags & VM_EXEC) != 0) {
		for (i = 0; i < nr; i++) {
			void *paddr = kmap_local_folio(folio, i * PAGE_SIZE);
			__flush_dcache_page((unsigned long)paddr);
			__invalidate_icache_page((unsigned long)paddr);
			kunmap_local(paddr);
		}
		set_bit(PG_arch_1, &folio->flags);
	}
#endif
}

/*
 * access_process_vm() has called get_user_pages(), which has done a
 * flush_dcache_page() on the page.
 */

#if (DCACHE_WAY_SIZE > PAGE_SIZE)

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		unsigned long vaddr, void *dst, const void *src,
		unsigned long len)
{
	unsigned long phys = page_to_phys(page);
	unsigned long alias = !(DCACHE_ALIAS_EQ(vaddr, phys));

	/* Flush and invalidate user page if aliased. */

	if (alias) {
		unsigned long t = TLBTEMP_BASE_1 + (vaddr & DCACHE_ALIAS_MASK);
		preempt_disable();
		__flush_invalidate_dcache_page_alias(t, phys);
		preempt_enable();
	}

	/* Copy data */
	
	memcpy(dst, src, len);

	/*
	 * Flush and invalidate kernel page if aliased and synchronize 
	 * data and instruction caches for executable pages. 
	 */

	if (alias) {
		unsigned long t = TLBTEMP_BASE_1 + (vaddr & DCACHE_ALIAS_MASK);

		preempt_disable();
		__flush_invalidate_dcache_range((unsigned long) dst, len);
		if ((vma->vm_flags & VM_EXEC) != 0)
			__invalidate_icache_page_alias(t, phys);
		preempt_enable();

	} else if ((vma->vm_flags & VM_EXEC) != 0) {
		__flush_dcache_range((unsigned long)dst,len);
		__invalidate_icache_range((unsigned long) dst, len);
	}
}

extern void copy_from_user_page(struct vm_area_struct *vma, struct page *page,
		unsigned long vaddr, void *dst, const void *src,
		unsigned long len)
{
	unsigned long phys = page_to_phys(page);
	unsigned long alias = !(DCACHE_ALIAS_EQ(vaddr, phys));

	/*
	 * Flush user page if aliased. 
	 * (Note: a simply flush would be sufficient) 
	 */

	if (alias) {
		unsigned long t = TLBTEMP_BASE_1 + (vaddr & DCACHE_ALIAS_MASK);
		preempt_disable();
		__flush_invalidate_dcache_page_alias(t, phys);
		preempt_enable();
	}

	memcpy(dst, src, len);
}

#endif
