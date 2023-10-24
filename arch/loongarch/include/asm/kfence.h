/* SPDX-License-Identifier: GPL-2.0 */
/*
 * KFENCE support for LoongArch.
 *
 * Author: Enze Li <lienze@kylinos.cn>
 * Copyright (C) 2022-2023 KylinSoft Corporation.
 */

#ifndef _ASM_LOONGARCH_KFENCE_H
#define _ASM_LOONGARCH_KFENCE_H

#include <linux/kfence.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

static inline bool arch_kfence_init_pool(void)
{
	int err;
	char *kfence_pool = __kfence_pool;
	struct vm_struct *area;

	area = __get_vm_area_caller(KFENCE_POOL_SIZE, VM_IOREMAP,
				    KFENCE_AREA_START, KFENCE_AREA_END,
				    __builtin_return_address(0));
	if (!area)
		return false;

	__kfence_pool = (char *)area->addr;
	err = ioremap_page_range((unsigned long)__kfence_pool,
				 (unsigned long)__kfence_pool + KFENCE_POOL_SIZE,
				 virt_to_phys((void *)kfence_pool), PAGE_KERNEL);
	if (err) {
		free_vm_area(area);
		__kfence_pool = kfence_pool;
		return false;
	}

	return true;
}

/* Protect the given page and flush TLB. */
static inline bool kfence_protect_page(unsigned long addr, bool protect)
{
	pte_t *pte = virt_to_kpte(addr);

	if (WARN_ON(!pte) || pte_none(*pte))
		return false;

	if (protect)
		set_pte(pte, __pte(pte_val(*pte) & ~(_PAGE_VALID | _PAGE_PRESENT)));
	else
		set_pte(pte, __pte(pte_val(*pte) | (_PAGE_VALID | _PAGE_PRESENT)));

	preempt_disable();
	local_flush_tlb_one(addr);
	preempt_enable();

	return true;
}

#endif /* _ASM_LOONGARCH_KFENCE_H */
