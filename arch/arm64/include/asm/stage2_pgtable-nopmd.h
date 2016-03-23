/*
 * Copyright (C) 2016 - ARM Ltd
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

#ifndef __ARM64_S2_PGTABLE_NOPMD_H_
#define __ARM64_S2_PGTABLE_NOPMD_H_

#include <asm/stage2_pgtable-nopud.h>

#define __S2_PGTABLE_PMD_FOLDED

#define S2_PMD_SHIFT		S2_PUD_SHIFT
#define S2_PTRS_PER_PMD		1
#define S2_PMD_SIZE		(1UL << S2_PMD_SHIFT)
#define S2_PMD_MASK		(~(S2_PMD_SIZE-1))

#define stage2_pud_none(pud)			(0)
#define stage2_pud_present(pud)			(1)
#define stage2_pud_clear(pud)			do { } while (0)
#define stage2_pud_populate(pud, pmd)		do { } while (0)
#define stage2_pmd_offset(pud, address)		((pmd_t *)(pud))

#define stage2_pmd_free(pmd)			do { } while (0)

#define stage2_pmd_addr_end(addr, end)		(end)

#define stage2_pud_huge(pud)			(0)
#define stage2_pmd_table_empty(pmdp)		(0)

#endif
