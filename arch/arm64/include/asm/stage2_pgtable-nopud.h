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

#ifndef __ARM64_S2_PGTABLE_NOPUD_H_
#define __ARM64_S2_PGTABLE_NOPUD_H_

#define __S2_PGTABLE_PUD_FOLDED

#define S2_PUD_SHIFT		S2_PGDIR_SHIFT
#define S2_PTRS_PER_PUD		1
#define S2_PUD_SIZE		(_AC(1, UL) << S2_PUD_SHIFT)
#define S2_PUD_MASK		(~(S2_PUD_SIZE-1))

#define stage2_pgd_none(pgd)			(0)
#define stage2_pgd_present(pgd)			(1)
#define stage2_pgd_clear(pgd)			do { } while (0)
#define stage2_pgd_populate(pgd, pud)	do { } while (0)

#define stage2_pud_offset(pgd, address)		((pud_t *)(pgd))

#define stage2_pud_free(x)			do { } while (0)

#define stage2_pud_addr_end(addr, end)		(end)
#define stage2_pud_table_empty(pmdp)		(0)

#endif
