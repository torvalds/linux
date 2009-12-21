#ifndef __ASM_SH_PGALLOC_H
#define __ASM_SH_PGALLOC_H

#include <linux/quicklist.h>
#include <asm/page.h>

#define QUICK_PT 1	/* Other page table pages that are zero on free */

#ifdef CONFIG_PGTABLE_LEVELS_3
#include <asm/pgalloc_pmd.h>
#else
#include <asm/pgalloc_nopmd.h>
#endif

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
				       pte_t *pte)
{
	set_pmd(pmd, __pmd((unsigned long)pte));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
				pgtable_t pte)
{
	set_pmd(pmd, __pmd((unsigned long)page_address(pte)));
}
#define pmd_pgtable(pmd) pmd_page(pmd)

/*
 * Allocate and free page tables.
 */
static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return quicklist_alloc(QUICK_PT, GFP_KERNEL | __GFP_REPEAT, NULL);
}

static inline pgtable_t pte_alloc_one(struct mm_struct *mm,
					unsigned long address)
{
	struct page *page;
	void *pg;

	pg = quicklist_alloc(QUICK_PT, GFP_KERNEL | __GFP_REPEAT, NULL);
	if (!pg)
		return NULL;
	page = virt_to_page(pg);
	pgtable_page_ctor(page);
	return page;
}

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	quicklist_free(QUICK_PT, NULL, pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t pte)
{
	pgtable_page_dtor(pte);
	quicklist_free_page(QUICK_PT, NULL, pte);
}

#define __pte_free_tlb(tlb,pte,addr)			\
do {							\
	pgtable_page_dtor(pte);				\
	tlb_remove_page((tlb), (pte));			\
} while (0)

static inline void check_pgt_cache(void)
{
	__check_pgt_cache();
	quicklist_trim(QUICK_PT, NULL, 25, 16);
}

#endif /* __ASM_SH_PGALLOC_H */
