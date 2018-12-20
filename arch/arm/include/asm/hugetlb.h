/*
 * arch/arm/include/asm/hugetlb.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * Based on arch/x86/include/asm/hugetlb.h
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

#ifndef _ASM_ARM_HUGETLB_H
#define _ASM_ARM_HUGETLB_H

#include <asm/page.h>
#include <asm/hugetlb-3level.h>
#include <asm-generic/hugetlb.h>

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr, unsigned long len)
{
	return 0;
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
	clear_bit(PG_dcache_clean, &page->flags);
}

#endif /* _ASM_ARM_HUGETLB_H */
