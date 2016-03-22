/*
 * Copyright (C) 2016 - ARM Ltd
 *
 * stage2 page table helpers
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __ARM64_S2_PGTABLE_H_
#define __ARM64_S2_PGTABLE_H_

#include <asm/pgtable.h>

/*
 * In the case where PGDIR_SHIFT is larger than KVM_PHYS_SHIFT, we can address
 * the entire IPA input range with a single pgd entry, and we would only need
 * one pgd entry.  Note that in this case, the pgd is actually not used by
 * the MMU for Stage-2 translations, but is merely a fake pgd used as a data
 * structure for the kernel pgtable macros to work.
 */
#if PGDIR_SHIFT > KVM_PHYS_SHIFT
#define PTRS_PER_S2_PGD_SHIFT	0
#else
#define PTRS_PER_S2_PGD_SHIFT	(KVM_PHYS_SHIFT - PGDIR_SHIFT)
#endif
#define PTRS_PER_S2_PGD		(1 << PTRS_PER_S2_PGD_SHIFT)

/*
 * If we are concatenating first level stage-2 page tables, we would have less
 * than or equal to 16 pointers in the fake PGD, because that's what the
 * architecture allows.  In this case, (4 - CONFIG_PGTABLE_LEVELS)
 * represents the first level for the host, and we add 1 to go to the next
 * level (which uses contatenation) for the stage-2 tables.
 */
#if PTRS_PER_S2_PGD <= 16
#define KVM_PREALLOC_LEVEL	(4 - CONFIG_PGTABLE_LEVELS + 1)
#else
#define KVM_PREALLOC_LEVEL	(0)
#endif

#define stage2_pgd_none(pgd)				pgd_none(pgd)
#define stage2_pgd_clear(pgd)				pgd_clear(pgd)
#define stage2_pgd_present(pgd)				pgd_present(pgd)
#define stage2_pgd_populate(pgd, pud)			pgd_populate(NULL, pgd, pud)
#define stage2_pud_offset(pgd, address)			pud_offset(pgd, address)
#define stage2_pud_free(pud)				pud_free(NULL, pud)

#define stage2_pud_none(pud)				pud_none(pud)
#define stage2_pud_clear(pud)				pud_clear(pud)
#define stage2_pud_present(pud)				pud_present(pud)
#define stage2_pud_populate(pud, pmd)			pud_populate(NULL, pud, pmd)
#define stage2_pmd_offset(pud, address)			pmd_offset(pud, address)
#define stage2_pmd_free(pmd)				pmd_free(NULL, pmd)

#define stage2_pud_huge(pud)				pud_huge(pud)

#define stage2_pgd_addr_end(address, end)		pgd_addr_end(address, end)
#define stage2_pud_addr_end(address, end)		pud_addr_end(address, end)
#define stage2_pmd_addr_end(address, end)		pmd_addr_end(address, end)

#define stage2_pte_table_empty(ptep)			kvm_page_empty(ptep)
#ifdef __PGTABLE_PMD_FOLDED
#define stage2_pmd_table_empty(pmdp)			(0)
#else
#define stage2_pmd_table_empty(pmdp)			((KVM_PREALLOC_LEVEL < 2) && kvm_page_empty(pmdp))
#endif

#ifdef __PGTABLE_PUD_FOLDED
#define stage2_pud_table_empty(pudp)			(0)
#else
#define stage2_pud_table_empty(pudp)			((KVM_PREALLOC_LEVEL < 1) && kvm_page_empty(pudp))
#endif

#define stage2_pgd_index(addr)				(((addr) >> PGDIR_SHIFT) & (PTRS_PER_S2_PGD - 1))

#endif	/* __ARM64_S2_PGTABLE_H_ */
