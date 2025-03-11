/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _ASM_RISCV_KFENCE_H
#define _ASM_RISCV_KFENCE_H

#include <linux/kfence.h>
#include <linux/pfn.h>
#include <asm-generic/pgalloc.h>
#include <asm/pgtable.h>

static inline bool arch_kfence_init_pool(void)
{
	return true;
}

static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	pte_t *pte = virt_to_kpte(addr);

	if (protect)
		set_pte(pte, __pte(pte_val(ptep_get(pte)) & ~_PAGE_PRESENT));
	else
		set_pte(pte, __pte(pte_val(ptep_get(pte)) | _PAGE_PRESENT));

	preempt_disable();
	local_flush_tlb_kernel_range(addr, addr + PAGE_SIZE);
	preempt_enable();

	return true;
}

#endif /* _ASM_RISCV_KFENCE_H */
