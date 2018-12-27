#ifndef _ASM_POWERPC_BOOK3S_64_PGALLOC_H
#define _ASM_POWERPC_BOOK3S_64_PGALLOC_H
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/slab.h>
#include <linux/cpumask.h>
#include <linux/kmemleak.h>
#include <linux/percpu.h>

struct vmemmap_backing {
	struct vmemmap_backing *list;
	unsigned long phys;
	unsigned long virt_addr;
};
extern struct vmemmap_backing *vmemmap_list;

/*
 * Functions that deal with pagetables that could be at any level of
 * the table need to be passed an "index_size" so they know how to
 * handle allocation.  For PTE pages (which are linked to a struct
 * page for now, and drawn from the main get_free_pages() pool), the
 * allocation size will be (2^index_size * sizeof(pointer)) and
 * allocations are drawn from the kmem_cache in PGT_CACHE(index_size).
 *
 * The maximum index size needs to be big enough to allow any
 * pagetable sizes we need, but small enough to fit in the low bits of
 * any page table pointer.  In other words all pagetables, even tiny
 * ones, must be aligned to allow at least enough low 0 bits to
 * contain this value.  This value is also used as a mask, so it must
 * be one less than a power of two.
 */
#define MAX_PGTABLE_INDEX_SIZE	0xf

extern struct kmem_cache *pgtable_cache[];
#define PGT_CACHE(shift) pgtable_cache[shift]

extern pte_t *pte_fragment_alloc(struct mm_struct *, unsigned long, int);
extern pmd_t *pmd_fragment_alloc(struct mm_struct *, unsigned long);
extern void pte_fragment_free(unsigned long *, int);
extern void pmd_fragment_free(unsigned long *);
extern void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift);
#ifdef CONFIG_SMP
extern void __tlb_remove_table(void *_table);
#endif
void pte_frag_destroy(void *pte_frag);

static inline pgd_t *radix__pgd_alloc(struct mm_struct *mm)
{
#ifdef CONFIG_PPC_64K_PAGES
	return (pgd_t *)__get_free_page(pgtable_gfp_flags(mm, PGALLOC_GFP));
#else
	struct page *page;
	page = alloc_pages(pgtable_gfp_flags(mm, PGALLOC_GFP | __GFP_RETRY_MAYFAIL),
				4);
	if (!page)
		return NULL;
	return (pgd_t *) page_address(page);
#endif
}

static inline void radix__pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
#ifdef CONFIG_PPC_64K_PAGES
	free_page((unsigned long)pgd);
#else
	free_pages((unsigned long)pgd, 4);
#endif
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;

	if (radix_enabled())
		return radix__pgd_alloc(mm);

	pgd = kmem_cache_alloc(PGT_CACHE(PGD_INDEX_SIZE),
			       pgtable_gfp_flags(mm, GFP_KERNEL));
	/*
	 * Don't scan the PGD for pointers, it contains references to PUDs but
	 * those references are not full pointers and so can't be recognised by
	 * kmemleak.
	 */
	kmemleak_no_scan(pgd);

	/*
	 * With hugetlb, we don't clear the second half of the page table.
	 * If we share the same slab cache with the pmd or pud level table,
	 * we need to make sure we zero out the full table on alloc.
	 * With 4K we don't store slot in the second half. Hence we don't
	 * need to do this for 4k.
	 */
#if defined(CONFIG_HUGETLB_PAGE) && defined(CONFIG_PPC_64K_PAGES) && \
	(H_PGD_INDEX_SIZE == H_PUD_CACHE_INDEX)
	memset(pgd, 0, PGD_TABLE_SIZE);
#endif
	return pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	if (radix_enabled())
		return radix__pgd_free(mm, pgd);
	kmem_cache_free(PGT_CACHE(PGD_INDEX_SIZE), pgd);
}

static inline void pgd_populate(struct mm_struct *mm, pgd_t *pgd, pud_t *pud)
{
	pgd_set(pgd, __pgtable_ptr_val(pud) | PGD_VAL_BITS);
}

static inline pud_t *pud_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	pud_t *pud;

	pud = kmem_cache_alloc(PGT_CACHE(PUD_CACHE_INDEX),
			       pgtable_gfp_flags(mm, GFP_KERNEL));
	/*
	 * Tell kmemleak to ignore the PUD, that means don't scan it for
	 * pointers and don't consider it a leak. PUDs are typically only
	 * referred to by their PGD, but kmemleak is not able to recognise those
	 * as pointers, leading to false leak reports.
	 */
	kmemleak_ignore(pud);

	return pud;
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	kmem_cache_free(PGT_CACHE(PUD_CACHE_INDEX), pud);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	pud_set(pud, __pgtable_ptr_val(pmd) | PUD_VAL_BITS);
}

static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud,
				  unsigned long address)
{
	/*
	 * By now all the pud entries should be none entries. So go
	 * ahead and flush the page walk cache
	 */
	flush_tlb_pgtable(tlb, address);
	pgtable_free_tlb(tlb, pud, PUD_INDEX);
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long addr)
{
	return pmd_fragment_alloc(mm, addr);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	pmd_fragment_free((unsigned long *)pmd);
}

static inline void __pmd_free_tlb(struct mmu_gather *tlb, pmd_t *pmd,
				  unsigned long address)
{
	/*
	 * By now all the pud entries should be none entries. So go
	 * ahead and flush the page walk cache
	 */
	flush_tlb_pgtable(tlb, address);
	return pgtable_free_tlb(tlb, pmd, PMD_INDEX);
}

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	pmd_set(pmd, __pgtable_ptr_val(pte) | PMD_VAL_BITS);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte_page)
{
	pmd_set(pmd, __pgtable_ptr_val(pte_page) | PMD_VAL_BITS);
}

static inline pgtable_t pmd_pgtable(pmd_t pmd)
{
	return (pgtable_t)pmd_page_vaddr(pmd);
}

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return (pte_t *)pte_fragment_alloc(mm, address, 1);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
				      unsigned long address)
{
	return (pgtable_t)pte_fragment_alloc(mm, address, 0);
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	pte_fragment_free((unsigned long *)pte, 1);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
	pte_fragment_free((unsigned long *)ptepage, 0);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t table,
				  unsigned long address)
{
	/*
	 * By now all the pud entries should be none entries. So go
	 * ahead and flush the page walk cache
	 */
	flush_tlb_pgtable(tlb, address);
	pgtable_free_tlb(tlb, table, PTE_INDEX);
}

#define check_pgt_cache()	do { } while (0)

extern atomic_long_t direct_pages_count[MMU_PAGE_COUNT];
static inline void update_page_count(int psize, long count)
{
	if (IS_ENABLED(CONFIG_PROC_FS))
		atomic_long_add(count, &direct_pages_count[psize]);
}

#endif /* _ASM_POWERPC_BOOK3S_64_PGALLOC_H */
