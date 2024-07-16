/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_HUGETLB_H
#define _ASM_RISCV_HUGETLB_H

#include <asm-generic/hugetlb.h>
#include <asm/page.h>

static inline void arch_clear_hugepage_flags(struct page *page)
{
	clear_bit(PG_dcache_clean, &page->flags);
}
#define arch_clear_hugepage_flags arch_clear_hugepage_flags

#endif /* _ASM_RISCV_HUGETLB_H */
