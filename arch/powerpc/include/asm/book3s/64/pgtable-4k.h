/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_4K_H
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_4K_H
/*
 * hash 4k can't share hugetlb and also doesn't support THP
 */
#ifndef __ASSEMBLY__
#ifdef CONFIG_HUGETLB_PAGE
static inline int pmd_huge(pmd_t pmd)
{
	/*
	 * leaf pte for huge page
	 */
	if (radix_enabled())
		return !!(pmd_raw(pmd) & cpu_to_be64(_PAGE_PTE));
	return 0;
}

static inline int pud_huge(pud_t pud)
{
	/*
	 * leaf pte for huge page
	 */
	if (radix_enabled())
		return !!(pud_raw(pud) & cpu_to_be64(_PAGE_PTE));
	return 0;
}

/*
 * With radix , we have hugepage ptes in the pud and pmd entries. We don't
 * need to setup hugepage directory for them. Our pte and page directory format
 * enable us to have this enabled.
 */
static inline int hugepd_ok(hugepd_t hpd)
{
	if (radix_enabled())
		return 0;
	return hash__hugepd_ok(hpd);
}
#define is_hugepd(hpd)		(hugepd_ok(hpd))

/*
 * 16M and 16G huge page directory tables are allocated from slab cache
 *
 */
#define H_16M_CACHE_INDEX (PAGE_SHIFT + H_PTE_INDEX_SIZE + H_PMD_INDEX_SIZE - 24)
#define H_16G_CACHE_INDEX                                                      \
	(PAGE_SHIFT + H_PTE_INDEX_SIZE + H_PMD_INDEX_SIZE + H_PUD_INDEX_SIZE - 34)

static inline int get_hugepd_cache_index(int index)
{
	switch (index) {
	case H_16M_CACHE_INDEX:
		return HTLB_16M_INDEX;
	case H_16G_CACHE_INDEX:
		return HTLB_16G_INDEX;
	default:
		BUG();
	}
	/* should not reach */
}

#endif /* CONFIG_HUGETLB_PAGE */

#endif /* __ASSEMBLY__ */

#endif /*_ASM_POWERPC_BOOK3S_64_PGTABLE_4K_H */
