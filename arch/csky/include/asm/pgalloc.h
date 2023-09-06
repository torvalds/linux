/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_PGALLOC_H
#define __ASM_CSKY_PGALLOC_H

#include <linux/highmem.h>
#include <linux/mm.h>
#include <linux/sched.h>

#define __HAVE_ARCH_PTE_ALLOC_ONE_KERNEL
#include <asm-generic/pgalloc.h>

static inline void pmd_populate_kernel(struct mm_struct *mm, pmd_t *pmd,
					pte_t *pte)
{
	set_pmd(pmd, __pmd(__pa(pte)));
}

static inline void pmd_populate(struct mm_struct *mm, pmd_t *pmd,
					pgtable_t pte)
{
	set_pmd(pmd, __pmd(__pa(page_address(pte))));
}

extern void pgd_init(unsigned long *p);

static inline pte_t *pte_alloc_one_kernel(struct mm_struct *mm)
{
	pte_t *pte;
	unsigned long i;

	pte = (pte_t *) __get_free_page(GFP_KERNEL);
	if (!pte)
		return NULL;

	for (i = 0; i < PAGE_SIZE/sizeof(pte_t); i++)
		(pte + i)->pte_low = _PAGE_GLOBAL;

	return pte;
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *ret;
	pgd_t *init;

	ret = (pgd_t *) __get_free_page(GFP_KERNEL);
	if (ret) {
		init = pgd_offset(&init_mm, 0UL);
		pgd_init((unsigned long *)ret);
		memcpy(ret + USER_PTRS_PER_PGD, init + USER_PTRS_PER_PGD,
			(PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
		/* prevent out of order excute */
		smp_mb();
#ifdef CONFIG_CPU_NEED_TLBSYNC
		dcache_wb_range((unsigned int)ret,
				(unsigned int)(ret + PTRS_PER_PGD));
#endif
	}

	return ret;
}

#define __pte_free_tlb(tlb, pte, address)		\
do {							\
	pagetable_pte_dtor(page_ptdesc(pte));		\
	tlb_remove_page_ptdesc(tlb, page_ptdesc(pte));	\
} while (0)

extern void pagetable_init(void);
extern void mmu_init(unsigned long min_pfn, unsigned long max_pfn);
extern void pre_trap_init(void);

#endif /* __ASM_CSKY_PGALLOC_H */
