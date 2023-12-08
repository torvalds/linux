/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PA-RISC KFENCE support.
 *
 * Copyright (C) 2021, Helge Deller <deller@gmx.de>
 */

#ifndef _ASM_PARISC_KFENCE_H
#define _ASM_PARISC_KFENCE_H

#include <linux/kfence.h>

#include <asm/pgtable.h>
#include <asm/tlbflush.h>

static inline bool arch_kfence_init_pool(void)
{
	return true;
}

/* Protect the given page and flush TLB. */
static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	pte_t *pte = virt_to_kpte(addr);

	if (WARN_ON(!pte))
		return false;

	/*
	 * We need to avoid IPIs, as we may get KFENCE allocations or faults
	 * with interrupts disabled.
	 */

	if (protect)
		set_pte(pte, __pte(pte_val(*pte) & ~_PAGE_PRESENT));
	else
		set_pte(pte, __pte(pte_val(*pte) | _PAGE_PRESENT));

	flush_tlb_kernel_range(addr, addr + PAGE_SIZE);

	return true;
}

#endif /* _ASM_PARISC_KFENCE_H */
