/*
 * arch/arm/include/asm/hugetlb-3level.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * Based on arch/x86/include/asm/hugetlb.h.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
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
