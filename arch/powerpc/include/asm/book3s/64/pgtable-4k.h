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
		return !!(pmd_val(pmd) & _PAGE_PTE);
	return 0;
}

static inline int pud_huge(pud_t pud)
{
	/*
	 * leaf pte for huge page
	 */
	if (radix_enabled())
		return !!(pud_val(pud) & _PAGE_PTE);
	return 0;
}

static inline int pgd_huge(pgd_t pgd)
{
	/*
	 * leaf pte for huge page
	 */
	if (radix_enabled())
		return !!(pgd_val(pgd) & _PAGE_PTE);
	return 0;
}
#define pgd_huge pgd_huge
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
#endif /* CONFIG_HUGETLB_PAGE */
#endif /* __ASSEMBLY__ */

#endif /*_ASM_POWERPC_BOOK3S_64_PGTABLE_4K_H */
