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
	return !!(pmd_raw(pmd) & cpu_to_be64(_PAGE_PTE));
}

static inline int pud_huge(pud_t pud)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pud_raw(pud) & cpu_to_be64(_PAGE_PTE));
}

static inline int pgd_huge(pgd_t pgd)
{
	/*
	 * leaf pte for huge page
	 */
	return !!(pgd_raw(pgd) & cpu_to_be64(_PAGE_PTE));
}
#define pgd_huge pgd_huge

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

#else /* !CONFIG_HUGETLB_PAGE */
static inline int pmd_huge(pmd_t pmd) { return 0; }
static inline int pud_huge(pud_t pud) { return 0; }
#endif /* CONFIG_HUGETLB_PAGE */

static inline int remap_4k_pfn(struct vm_area_struct *vma, unsigned long addr,
			       unsigned long pfn, pgprot_t prot)
{
	if (radix_enabled())
		BUG();
	return hash__remap_4k_pfn(vma, addr, pfn, prot);
}
#endif	/* __ASSEMBLY__ */
#endif /*_ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H */
