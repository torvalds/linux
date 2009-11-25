#ifndef __ASM_SH_PGALLOC_NOPMD_H
#define __ASM_SH_PGALLOC_NOPMD_H

#define QUICK_PGD 0	/* We preserve special mappings over free */

static inline void pgd_ctor(void *x)
{
	pgd_t *pgd = x;

	memcpy(pgd + USER_PTRS_PER_PGD,
	       swapper_pg_dir + USER_PTRS_PER_PGD,
	       (PTRS_PER_PGD - USER_PTRS_PER_PGD) * sizeof(pgd_t));
}

static inline pgd_t *pgd_alloc(struct mm_struct *mm)
{
	return quicklist_alloc(QUICK_PGD, GFP_KERNEL | __GFP_REPEAT, pgd_ctor);
}

static inline void pgd_free(struct mm_struct *mm, pgd_t *pgd)
{
	quicklist_free(QUICK_PGD, NULL, pgd);
}

static inline void __check_pgt_cache(void)
{
	quicklist_trim(QUICK_PGD, NULL, 25, 16);
}

#endif /* __ASM_SH_PGALLOC_NOPMD_H */
