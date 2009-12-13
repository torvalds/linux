#ifndef __ASM_SH_PGALLOC_PMD_H
#define __ASM_SH_PGALLOC_PMD_H

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	pgd_t *pgd;
	int i;

	pgd = kzalloc(sizeof(*pgd) * PTRS_PER_PGD, GFP_KERNEL | __GFP_REPEAT);

	for (i = USER_PTRS_PER_PGD; i < PTRS_PER_PGD; i++)
		pgd[i] = swapper_pg_dir[i];

	return pgd;
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	kfree(pgd);
}

static inline void __check_pgt_cache(void)
{
}

static inline void pud_populate(struct mm_struct *mm, pud_t *pud, pmd_t *pmd)
{
	set_pud(pud, __pud((unsigned long)pmd));
}

static inline pmd_t *pmd_alloc_one(struct mm_struct *mm, unsigned long address)
{
	return quicklist_alloc(QUICK_PT, GFP_KERNEL | __GFP_REPEAT, NULL);
}

static inline void pmd_free(struct mm_struct *mm, pmd_t *pmd)
{
	quicklist_free(QUICK_PT, NULL, pmd);
}

#endif /* __ASM_SH_PGALLOC_PMD_H */
