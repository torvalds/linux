/* SPDX-License-Identifier: GPL-2.0 */
/*
 * x86 KFENCE support.
 *
 * Copyright (C) 2020, Google LLC.
 */

#ifndef _ASM_X86_KFENCE_H
#define _ASM_X86_KFENCE_H

#ifndef MODULE

#include <linux/bug.h>
#include <linux/kfence.h>

#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/set_memory.h>
#include <asm/tlbflush.h>

/* Force 4K pages for __kfence_pool. */
static inline bool arch_kfence_init_pool(void)
{
	unsigned long addr;

	for (addr = (unsigned long)__kfence_pool; is_kfence_address((void *)addr);
	     addr += PAGE_SIZE) {
		unsigned int level;

		if (!lookup_address(addr, &level))
			return false;

		if (level != PG_LEVEL_4K)
			set_memory_4k(addr, 1);
	}

	return true;
}

/* Protect the given page and flush TLB. */
static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	unsigned int level;
	pte_t *pte = lookup_address(addr, &level);
	pteval_t val, new;

	if (WARN_ON(!pte || level != PG_LEVEL_4K))
		return false;

	val = pte_val(*pte);

	/*
	 * protect requires making the page not-present.  If the PTE is
	 * already in the right state, there's nothing to do.
	 */
	if (protect != !!(val & _PAGE_PRESENT))
		return true;

	/*
	 * Otherwise, flip the Present bit, taking care to avoid writing an
	 * L1TF-vulnerable PTE (not present, without the high address bits
	 * set).
	 */
	new = val ^ _PAGE_PRESENT;
	set_pte(pte, __pte(flip_protnone_guard(val, new, PTE_PFN_MASK)));

	/*
	 * If the page was protected (non-present) and we're making it
	 * present, there is no need to flush the TLB at all.
	 */
	if (!protect)
		return true;

	/*
	 * We need to avoid IPIs, as we may get KFENCE allocations or faults
	 * with interrupts disabled. Therefore, the below is best-effort, and
	 * does not flush TLBs on all CPUs. We can tolerate some inaccuracy;
	 * lazy fault handling takes care of faults after the page is PRESENT.
	 */

	/*
	 * Flush this CPU's TLB, assuming whoever did the allocation/free is
	 * likely to continue running on this CPU.
	 */
	preempt_disable();
	flush_tlb_one_kernel(addr);
	preempt_enable();
	return true;
}

#endif /* !MODULE */

#endif /* _ASM_X86_KFENCE_H */
