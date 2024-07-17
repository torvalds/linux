/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H

#ifndef __ASSEMBLY__
#ifdef CONFIG_HUGETLB_PAGE

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

/*
 * This should never get called
 */
static __always_inline int get_hugepd_cache_index(int index)
{
	BUILD_BUG();
}

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
