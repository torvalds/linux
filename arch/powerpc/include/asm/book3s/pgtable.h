/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_POWERPC_BOOK3S_PGTABLE_H
#define _ASM_POWERPC_BOOK3S_PGTABLE_H

#ifdef CONFIG_PPC64
#include <asm/book3s/64/pgtable.h>
#else
#include <asm/book3s/32/pgtable.h>
#endif

#ifndef __ASSEMBLY__
void __update_mmu_cache(struct vm_area_struct *vma, unsigned long address, pte_t *ptep);

/*
 * This gets called at the end of handling a page fault, when
 * the kernel has put a new PTE into the page table for the process.
 * We use it to ensure coherency between the i-cache and d-cache
 * for the page which has just been mapped in.
 * On machines which use an MMU hash table, we use this to put a
 * corresponding HPTE into the hash table ahead of time, instead of
 * waiting for the inevitable extra hash-table miss exception.
 */
static inline void update_mmu_cache_range(struct vm_fault *vmf,
		struct vm_area_struct *vma, unsigned long address,
		pte_t *ptep, unsigned int nr)
{
	if (IS_ENABLED(CONFIG_PPC32) && !mmu_has_feature(MMU_FTR_HPTE_TABLE))
		return;
	if (radix_enabled())
		return;
	__update_mmu_cache(vma, address, ptep);
}

#endif /* __ASSEMBLY__ */
#endif
