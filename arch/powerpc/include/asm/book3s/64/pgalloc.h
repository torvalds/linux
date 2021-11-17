/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_BOOK3S_64_PGALLOC_H
#define _ASM_POWERPC_BOOK3S_64_PGALLOC_H
/*
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

extern pmd_t *pmd_fragment_alloc(struct mm_struct *, unsigned long);
extern void pmd_fragment_free(unsigned long *);
extern void pgtable_free_tlb(struct mmu_gather *tlb, void *table, int shift);
extern void __tlb_remove_table(void *_table);
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
	if (unlikely(!pgd))
		return pgd;

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

static inline void p4d_populate(struct mm_struct *mm, p4d_t *pgd, pud_t *pud)
{
	*pgd =  __p4d(__pgtable_ptr_val(pud) | PGD_VAL_BITS);
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

static inline void __pud_free(pud_t *pud)
{
	struct page *page = virt_to_page(pud);

	/*
	 * Early pud pages allocated via memblock allocator
	 * can't be directly freed to slab
	 */
	if (PageReserved(page))
		free_reserved_page(page);
	else
		kmem_cache_free(PGT_CACHE(PUD_CACHE_INDEX), pud);
}

static inline void pud_free(struct mm_struct *mm, pud_t *pud)
{
	return __pud_free(pud);
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	*pud = __pud(__pgtable_ptr_val(pmd) | PUD_VAL_BITS);
}

static inline void __pud_free_tlb(struct mmu_gather *tlb, pud_t *pud,
				  unsigned long address)
{
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
	return pgtable_free_tlb(tlb, pmd, PMD_INDEX);
}

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	*pmd = __pmd(__pgtable_ptr_val(pte) | PMD_VAL_BITS);
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte_page)
{
	*pmd = __pmd(__pgtable_ptr_val(pte_page) | PMD_VAL_BITS);
}

static inline void __pte_free_tlb(struct mmu_gather *tlb, pgtable_t table,
				  unsigned long address)
{
	pgtable_free_tlb(tlb, table, PTE_INDEX);
}

extern atomic_long_t direct_pages_count[MMU_PAGE_COUNT];
static inline void update_page_count(int psize, long count)
{
	if (IS_ENABLED(CONFIG_PROC_FS))
		atomic_long_add(count, &direct_pages_count[psize]);
}

#endif /* _ASM_POWERPC_BOOK3S_64_PGALLOC_H */
