// SPDX-License-Identifier: GPL-2.0
/*
 * fixmaps for parisc
 *
 * Copyright (c) 2019 Sven Schnelle <svens@stackframe.org>
 */

#include <linux/kprobes.h>
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include <asm/fixmap.h>

void notrace set_fixmap(enum fixed_addresses idx, phys_addr_t phys)
{
	unsigned long vaddr = __fix_to_virt(idx);
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = pmd_offset(pgd, vaddr);
	pte_t *pte;

	if (pmd_none(*pmd))
		pmd = pmd_alloc(NULL, pgd, vaddr);

	pte = pte_offset_kernel(pmd, vaddr);
	if (pte_none(*pte))
		pte = pte_alloc_kernel(pmd, vaddr);

	set_pte_at(&init_mm, vaddr, pte, __mk_pte(phys, PAGE_KERNEL_RWX));
	flush_tlb_kernel_range(vaddr, vaddr + PAGE_SIZE);
}

void notrace clear_fixmap(enum fixed_addresses idx)
{
	unsigned long vaddr = __fix_to_virt(idx);
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = pmd_offset(pgd, vaddr);
	pte_t *pte = pte_offset_kernel(pmd, vaddr);

	if (WARN_ON(pte_none(*pte)))
		return;

	pte_clear(&init_mm, vaddr, pte);

	flush_tlb_kernel_range(vaddr, vaddr + PAGE_SIZE);
}
