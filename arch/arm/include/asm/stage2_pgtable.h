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

#ifndef __ARM_S2_PGTABLE_H_
#define __ARM_S2_PGTABLE_H_

#define stage2_pgd_none(pgd)			pgd_none(pgd)
#define stage2_pgd_clear(pgd)			pgd_clear(pgd)
#define stage2_pgd_present(pgd)			pgd_present(pgd)
#define stage2_pgd_populate(pgd, pud)		pgd_populate(NULL, pgd, pud)
#define stage2_pud_offset(pgd, address)		pud_offset(pgd, address)
#define stage2_pud_free(pud)			pud_free(NULL, pud)

#define stage2_pud_none(pud)			pud_none(pud)
#define stage2_pud_clear(pud)			pud_clear(pud)
#define stage2_pud_present(pud)			pud_present(pud)
#define stage2_pud_populate(pud, pmd)		pud_populate(NULL, pud, pmd)
#define stage2_pmd_offset(pud, address)		pmd_offset(pud, address)
#define stage2_pmd_free(pmd)			pmd_free(NULL, pmd)

#define stage2_pud_huge(pud)			pud_huge(pud)

/* Open coded p*d_addr_end that can deal with 64bit addresses */
static inline phys_addr_t stage2_pgd_addr_end(phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t boundary = (addr + PGDIR_SIZE) & PGDIR_MASK;

	return (boundary - 1 < end - 1) ? boundary : end;
}

#define stage2_pud_addr_end(addr, end)		(end)

static inline phys_addr_t stage2_pmd_addr_end(phys_addr_t addr, phys_addr_t end)
{
	phys_addr_t boundary = (addr + PMD_SIZE) & PMD_MASK;

	return (boundary - 1 < end - 1) ? boundary : end;
}

#define stage2_pgd_index(addr)				pgd_index(addr)

#define stage2_pte_table_empty(ptep)			kvm_page_empty(ptep)
#define stage2_pmd_table_empty(pmdp)			kvm_page_empty(pmdp)
#define stage2_pud_table_empty(pudp)			false

#endif	/* __ARM_S2_PGTABLE_H_ */
