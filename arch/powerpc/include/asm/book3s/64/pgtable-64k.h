/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H
#define _ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H

#ifndef __ASSEMBLER__
#ifdef CONFIG_HUGETLB_PAGE

#endif /* CONFIG_HUGETLB_PAGE */

static inline int remap_4k_pfn(struct vm_area_struct *vma, unsigned long addr,
			       unsigned long pfn, pgprot_t prot)
{
	if (radix_enabled())
		BUG();
	return hash__remap_4k_pfn(vma, addr, pfn, prot);
}
#endif	/* __ASSEMBLER__ */
#endif /*_ASM_POWERPC_BOOK3S_64_PGTABLE_64K_H */
