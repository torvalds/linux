/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * arch/arm/include/asm/hugetlb.h
 *
 * Copyright (C) 2012 ARM Ltd.
 *
 * Based on arch/x86/include/asm/hugetlb.h
 */

#ifndef _ASM_ARM_HUGETLB_H
#define _ASM_ARM_HUGETLB_H

#include <asm/cacheflush.h>
#include <asm/page.h>
#include <asm/hugetlb-3level.h>
#include <asm-generic/hugetlb.h>

static inline void arch_clear_hugepage_flags(struct page *page)
{
	clear_bit(PG_dcache_clean, &page->flags);
}
#define arch_clear_hugepage_flags arch_clear_hugepage_flags

#endif /* _ASM_ARM_HUGETLB_H */
