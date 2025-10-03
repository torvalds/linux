/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_SH_HUGETLB_H
#define _ASM_SH_HUGETLB_H

#include <asm/cacheflush.h>
#include <asm/page.h>

#define __HAVE_ARCH_HUGE_PTEP_CLEAR_FLUSH
static inline pte_t huge_ptep_clear_flush(struct vm_area_struct *vma,
					  unsigned long addr, pte_t *ptep)
{
	return *ptep;
}

static inline void arch_clear_hugetlb_flags(struct folio *folio)
{
	clear_bit(PG_dcache_clean, &folio->flags.f);
}
#define arch_clear_hugetlb_flags arch_clear_hugetlb_flags

#include <asm-generic/hugetlb.h>

#endif /* _ASM_SH_HUGETLB_H */
