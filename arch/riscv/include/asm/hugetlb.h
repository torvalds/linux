/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_RISCV_HUGETLB_H
#define _ASM_RISCV_HUGETLB_H

#include <asm-generic/hugetlb.h>
#include <asm/page.h>

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#endif /* _ASM_RISCV_HUGETLB_H */
