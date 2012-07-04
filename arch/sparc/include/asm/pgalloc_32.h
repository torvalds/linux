#ifndef _SPARC_PGALLOC_H
#define _SPARC_PGALLOC_H

#include <linux/kernel.h>
#include <linux/sched.h>

#include <asm/pgtsrmmu.h>
#include <asm/pgtable.h>
#include <asm/vaddrs.h>
#include <asm/page.h>

struct page;

extern struct pgtable_cache_struct {
	unsigned long *pgd_cache;
	unsigned long *pte_cache;
	unsigned long pgtable_cache_sz;
	unsigned long pgd_cache_sz;
} pgt_quicklists;

unsigned long srmmu_get_nocache(int size, int align);
void srmmu_free_nocache(unsigned long vaddr, int size);

#define pgd_quicklist           (pgt_quicklists.pgd_cache)
#define pmd_quicklist           ((unsigned long *)0)
#define pte_quicklist           (pgt_quicklists.pte_cache)
#define pgtable_cache_size      (pgt_quicklists.pgtable_cache_sz)
#define pgd_cache_size		(pgt_quicklists.pgd_cache_sz)

#define check_pgt_cache()	do { } while (0)

pgd_t *get_pgd_fast(void);
static inline void free_pgd_fast(pgd_t *pgd)
{
	srmmu_free_nocache((unsigned long)pgd, SRMMU_PGD_TABLE_SIZE);
}

#define pgd_free(mm, pgd)	free_pgd_fast(pgd)
#define pgd_alloc(mm)	get_pgd_fast()

static inline void pgd_set(pgd_t * pgdp, pmd_t * pmdp)
{
	unsigned long pa = __nocache_pa((unsigned long)pmdp);

	set_pte((pte_t *)pgdp, (SRMMU_ET_PTD | (pa >> 4)));
}

#define pgd_populate(MM, PGD, PMD)      pgd_set(PGD, PMD)

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm,
				   unsigned long address)
{
	return (pmd_t *)srmmu_get_nocache(SRMMU_PMD_TABLE_SIZE,
					  SRMMU_PMD_TABLE_SIZE);
}

static inline void free_pmd_fast(pmd_t * pmd)
{
	srmmu_free_nocache((unsigned long)pmd, SRMMU_PMD_TABLE_SIZE);
}

#define pmd_free(mm, pmd)		free_pmd_fast(pmd)
#define __pmd_free_tlb(tlb, pmd, addr)	pmd_free((tlb)->mm, pmd)

void pmd_populate(struct mm_struct *mm, pmd_t *pmdp, struct page *ptep);
#define pmd_pgtable(pmd) pmd_page(pmd)

void pmd_set(pmd_t *pmdp, pte_t *ptep);
#define pmd_populate_kernel(MM, PMD, PTE) pmd_set(PMD, PTE)

pgtable_t pte_alloc_one(struct mm_struct *mm, unsigned long address);

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm,
					  unsigned long address)
{
	return (pte_t *)srmmu_get_nocache(PTE_SIZE, PTE_SIZE);
}


static inline void free_pte_fast(pte_t *pte)
{
	srmmu_free_nocache((unsigned long)pte, PTE_SIZE);
}

#define pte_free_kernel(mm, pte)	free_pte_fast(pte)

void pte_free(struct mm_struct * mm, pgtable_t pte);
#define __pte_free_tlb(tlb, pte, addr)	pte_free((tlb)->mm, pte)

#endif /* _SPARC_PGALLOC_H */
