#ifndef _ASM_POWERPC_PGALLOC_H
#define _ASM_POWERPC_PGALLOC_H
#ifdef __KERNEL__

#include <linux/mm.h>

#ifdef CONFIG_PPC_BOOK3E
extern void tlb_flush_pgtable(struct mmu_gather *tlb, unsigned long address);
#else /* CONFIG_PPC_BOOK3E */
static inline void tlb_flush_pgtable(struct mmu_gather *tlb,
				     unsigned long address)
{
}
#endif /* !CONFIG_PPC_BOOK3E */

static inline void pte_free_kernel(struct mm_struct *mm, pte_t *pte)
{
	free_page((unsigned long)pte);
}

static inline void pte_free(struct mm_struct *mm, pgtable_t ptepage)
{
	pgtable_page_dtor(ptepage);
	__free_page(ptepage);
}

typedef struct pgtable_free {
	unsigned long val;
} pgtable_free_t;

/* This needs to be big enough to allow for MMU_PAGE_COUNT + 2 to be stored
 * and small enough to fit in the low bits of any naturally aligned page
 * table cache entry. Arbitrarily set to 0x1f, that should give us some
 * room to grow
 */
#define PGF_CACHENUM_MASK	0x1f

static inline pgtable_free_t pgtable_free_cache(void *p, int cachenum,
						unsigned long mask)
{
	BUG_ON(cachenum > PGF_CACHENUM_MASK);

	return (pgtable_free_t){.val = ((unsigned long) p & ~mask) | cachenum};
}

#ifdef CONFIG_PPC64
#include <asm/pgalloc-64.h>
#else
#include <asm/pgalloc-32.h>
#endif

#ifdef CONFIG_SMP
extern void pgtable_free_tlb(struct mmu_gather *tlb, pgtable_free_t pgf);
extern void pte_free_finish(void);
#else /* CONFIG_SMP */
static inline void pgtable_free_tlb(struct mmu_gather *tlb, pgtable_free_t pgf)
{
	pgtable_free(pgf);
}
static inline void pte_free_finish(void) { }
#endif /* !CONFIG_SMP */

static inline void __pte_free_tlb(struct mmu_gather *tlb, struct page *ptepage,
				  unsigned long address)
{
	pgtable_free_t pgf = pgtable_free_cache(page_address(ptepage),
						PTE_NONCACHE_NUM,
						PTE_TABLE_SIZE-1);
	tlb_flush_pgtable(tlb, address);
	pgtable_page_dtor(ptepage);
	pgtable_free_tlb(tlb, pgf);
}

#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PGALLOC_H */
