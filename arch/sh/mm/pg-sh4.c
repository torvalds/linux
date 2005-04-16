/*
 * arch/sh/mm/pg-sh4.c
 *
 * Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 * Copyright (C) 2002  Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/config.h>
#include <linux/init.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/threads.h>
#include <asm/addrspace.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/processor.h>
#include <asm/cache.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

extern struct semaphore p3map_sem[];

/*
 * clear_user_page
 * @to: P1 address
 * @address: U0 address to be mapped
 * @page: page (virt_to_page(to))
 */
void clear_user_page(void *to, unsigned long address, struct page *page)
{
	__set_bit(PG_mapped, &page->flags);
	if (((address ^ (unsigned long)to) & CACHE_ALIAS) == 0)
		clear_page(to);
	else {
		pgprot_t pgprot = __pgprot(_PAGE_PRESENT | 
					   _PAGE_RW | _PAGE_CACHABLE |
					   _PAGE_DIRTY | _PAGE_ACCESSED | 
					   _PAGE_HW_SHARED | _PAGE_FLAGS_HARD);
		unsigned long phys_addr = PHYSADDR(to);
		unsigned long p3_addr = P3SEG + (address & CACHE_ALIAS);
		pgd_t *dir = pgd_offset_k(p3_addr);
		pmd_t *pmd = pmd_offset(dir, p3_addr);
		pte_t *pte = pte_offset_kernel(pmd, p3_addr);
		pte_t entry;
		unsigned long flags;

		entry = pfn_pte(phys_addr >> PAGE_SHIFT, pgprot);
		down(&p3map_sem[(address & CACHE_ALIAS)>>12]);
		set_pte(pte, entry);
		local_irq_save(flags);
		__flush_tlb_page(get_asid(), p3_addr);
		local_irq_restore(flags);
		update_mmu_cache(NULL, p3_addr, entry);
		__clear_user_page((void *)p3_addr, to);
		pte_clear(&init_mm, p3_addr, pte);
		up(&p3map_sem[(address & CACHE_ALIAS)>>12]);
	}
}

/*
 * copy_user_page
 * @to: P1 address
 * @from: P1 address
 * @address: U0 address to be mapped
 * @page: page (virt_to_page(to))
 */
void copy_user_page(void *to, void *from, unsigned long address, 
		    struct page *page)
{
	__set_bit(PG_mapped, &page->flags);
	if (((address ^ (unsigned long)to) & CACHE_ALIAS) == 0)
		copy_page(to, from);
	else {
		pgprot_t pgprot = __pgprot(_PAGE_PRESENT | 
					   _PAGE_RW | _PAGE_CACHABLE |
					   _PAGE_DIRTY | _PAGE_ACCESSED | 
					   _PAGE_HW_SHARED | _PAGE_FLAGS_HARD);
		unsigned long phys_addr = PHYSADDR(to);
		unsigned long p3_addr = P3SEG + (address & CACHE_ALIAS);
		pgd_t *dir = pgd_offset_k(p3_addr);
		pmd_t *pmd = pmd_offset(dir, p3_addr);
		pte_t *pte = pte_offset_kernel(pmd, p3_addr);
		pte_t entry;
		unsigned long flags;

		entry = pfn_pte(phys_addr >> PAGE_SHIFT, pgprot);
		down(&p3map_sem[(address & CACHE_ALIAS)>>12]);
		set_pte(pte, entry);
		local_irq_save(flags);
		__flush_tlb_page(get_asid(), p3_addr);
		local_irq_restore(flags);
		update_mmu_cache(NULL, p3_addr, entry);
		__copy_user_page((void *)p3_addr, from, to);
		pte_clear(&init_mm, p3_addr, pte);
		up(&p3map_sem[(address & CACHE_ALIAS)>>12]);
	}
}

/*
 * For SH-4, we have our own implementation for ptep_get_and_clear
 */
inline pte_t ptep_get_and_clear(struct mm_struct *mm, unsigned long addr, pte_t *ptep)
{
	pte_t pte = *ptep;

	pte_clear(mm, addr, ptep);
	if (!pte_not_present(pte)) {
		unsigned long pfn = pte_pfn(pte);
		if (pfn_valid(pfn)) {
			struct page *page = pfn_to_page(pfn);
			struct address_space *mapping = page_mapping(page);
			if (!mapping || !mapping_writably_mapped(mapping))
				__clear_bit(PG_mapped, &page->flags);
		}
	}
	return pte;
}

