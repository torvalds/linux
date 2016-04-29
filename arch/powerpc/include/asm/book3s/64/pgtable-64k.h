#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H

#ifndef __ASSEMBLY__
#ifdef CONFIG_HUGETLB_PAGE
/*
 * We have PGD_INDEX_SIZ = 12 and PTE_INDEX_SIZE = 8, so that we can have
 * 16GB hugepage pte in PGD and 16MB hugepage pte at PMD;
 *
 * Defined in such a way that we can optimize away code block at build time
 * if CONFIG_HUGETLB_PAGE=n.
 */
static inline int pmd_huge(pmd_t pmd)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pmd_val(pmd) & _PAGE_PTE);
}

static inline int pud_huge(pud_t pud)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pud_val(pud) & _PAGE_PTE);
}

static inline int pgd_huge(pgd_t pgd)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pgd_val(pgd) & _PAGE_PTE);
}
#define pgd_huge pgd_huge

#ifdef CONFIG_DEBUG_VM
extern int hugepd_ok(hugepd_t hpd);
#define is_hugepd(hpd)               (hugepd_ok(hpd))
#else
/*
 * With 64k page size, we have hugepage ptes in the pgd and pmd entries. We don't
 * need to setup hugepage directory for them. Our pte and page directory format
 * enable us to have this enabled.
 */
static inline int hugepd_ok(hugepd_t hpd)
{
	return 0;
}
#define is_hugepd(pdep)			0
#endif /* CONFIG_DEBUG_VM */

#endif /* CONFIG_HUGETLB_PAGE */

#ifdef CONFIG_TRANSPARENT_HUGEPAGE
static inline int pmd_large(pmd_t pmd)
{
	return !!(pmd_val(pmd) & _PAGE_PTE);
}

static inline pmd_t pmd_mknotpresent(pmd_t pmd)
{
	return __pmd(pmd_val(pmd) & ~_PAGE_PRESENT);
}
/*
 * For radix we should always find H_PAGE_HASHPTE zero. Hence
 * the below will work for radix too
 */
static inline int __pmdp_test_and_clear_young(struct mm_struct *mm,
					      unsigned long addr, pmd_t *pmdp)
{
	unsigned long old;

	if ((pmd_val(*pmdp) & (_PAGE_ACCESSED | H_PAGE_HASHPTE)) == 0)
		return 0;
	old = pmd_hugepage_update(mm, addr, pmdp, _PAGE_ACCESSED, 0);
	return ((old & _PAGE_ACCESSED) != 0);
}

#define __HAVE_ARCH_PMDP_SET_WRPROTECT
static inline void pmdp_set_wrprotect(struct mm_struct *mm, unsigned long addr,
				      pmd_t *pmdp)
{

	if ((pmd_val(*pmdp) & _PAGE_WRITE) == 0)
		return;

	pmd_hugepage_update(mm, addr, pmdp, _PAGE_WRITE, 0);
}

#endif /*  CONFIG_TRANSPARENT_HUGEPAGE */
#endif	/* __ASSEMBLY__ */
#endif /*_ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H */
