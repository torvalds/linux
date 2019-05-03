/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_HUGETLB_H
#define _ASM_X86_HUGETLB_H

#include <asm/page.h>
#include <asm-generic/hugetlb.h>

#define hugepages_supported() boot_cpu_has(X86_FEATURE_PSE)

static inline int is_hugepage_only_range(struct mm_struct *mm,
					 unsigned long addr,
					 unsigned long len) {
	return 0;
}

static inline void arch_clear_hugepage_flags(struct page *page)
{
}

#ifdef CONFIG_ARCH_HAS_GIGANTIC_PAGE
static inline bool gigantic_page_supported(void) { return true; }
#endif

#endif /* _ASM_X86_HUGETLB_H */
