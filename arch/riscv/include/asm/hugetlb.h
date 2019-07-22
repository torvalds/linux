/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_HUGETLB_H
#define _ASM_RISCV_HUGETLB_H

#include <asm-generic/hugetlb.h>
#include <asm/page.h>

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len) {
	return 0;
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#endif /* _ASM_RISCV_HUGETLB_H */
