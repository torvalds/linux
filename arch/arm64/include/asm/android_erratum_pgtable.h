/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2023 - Google LLC
 * Author: Will Deacon <willdeacon@google.com>
 */
#ifndef _ASM_ARM64_ANDROID_ERRATUM_PGTABLE_H
#define _ASM_ARM64_ANDROID_ERRATUM_PGTABLE_H

#ifndef __ASM_PGTABLE_H
#error "Please don't include this header directly."
#endif

#ifdef CONFIG_ANDROID_ARM64_WORKAROUND_DMA_BEYOND_POC
extern void pkvm_host_set_stage2_memattr(phys_addr_t addr, bool force_nc);
extern __init int pkvm_register_early_nc_mappings(void);

DECLARE_STATIC_KEY_FALSE(pkvm_force_nc);
static inline bool prot_needs_stage2_update(pgprot_t prot)
{
	pteval_t val = pgprot_val(prot);

	if (!static_branch_unlikely(&pkvm_force_nc))
		return 0;

	return (val & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL_NC);
}

static inline void arm64_update_cacheable_aliases(pte_t *ptep, pte_t pte)
{
	pte_t old_pte = READ_ONCE(*ptep);
	bool force_nc;

	if (!static_branch_unlikely(&pkvm_force_nc))
		return;

	if (pte_valid(old_pte) == pte_valid(pte))
		return;

	if (!pte_valid(pte)) {
		force_nc = false;
		pte = old_pte;
	} else {
		force_nc = true;
	}

	if ((pte_val(pte) & PTE_ATTRINDX_MASK) == PTE_ATTRINDX(MT_NORMAL_NC))
		pkvm_host_set_stage2_memattr(__pte_to_phys(pte), force_nc);
}
#else
static inline void arm64_update_cacheable_aliases(pte_t *ptep, pte_t pte) { }
static inline bool prot_needs_stage2_update(pgprot_t prot) { return false; }
#endif /* CONFIG_ANDROID_ARM64_WORKAROUND_DMA_BEYOND_POC */
#endif /* _ASM_ARM64_ANDROID_ERRATUM_PGTABLE_H */
