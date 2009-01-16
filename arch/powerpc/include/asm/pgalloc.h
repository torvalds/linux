#ifndef _ASM_POWERPC_PGALLOC_H
#define _ASM_POWERPC_PGALLOC_H
#ifdef __KERNEL__

#include <linux/mm.h>

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

#define PGF_CACHENUM_MASK	0x7

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

extern void pgtable_free_tlb(struct mmu_gather *tlb, pgtable_free_t pgf);

#ifdef CONFIG_SMP
#define __pte_free_tlb(tlb,ptepage)	\
do { \
	pgtable_page_dtor(ptepage); \
	pgtable_free_tlb(tlb, pgtable_free_cache(page_address(ptepage), \
		PTE_NONCACHE_NUM, PTE_TABLE_SIZE-1)); \
} while (0)
#else
#define __pte_free_tlb(tlb, pte)	pte_free((tlb)->mm, (pte))
#endif


#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_PGALLOC_H */
