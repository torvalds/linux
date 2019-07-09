/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/hugetlb-3level.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * Based on arch/x86/include/asm/hugetlb.h.
 */

#ifndef _ASM_ARM_HUGETLB_3LEVEL_H
#define _ASM_ARM_HUGETLB_3LEVEL_H


/*
 * If our huge pte is non-zero then mark the valid bit.
 * This allows pte_present(huge_ptep_get(ptep)) to return true for non-zero
 * ptes.
 * (The valid bit is automatically cleared by set_pte_at for PROT_NONE ptes).
 */
#define __HAVE_ARCH_HUGE_PTEP_GET
static inline pte_t huge_ptep_get(pte_t *ptep)
{
	pte_t retval = *ptep;
	if (pte_val(retval))
		pte_val(retval) |= L_PTE_VALID;
	return retval;
}

#endif /* _ASM_ARM_HUGETLB_3LEVEL_H */
