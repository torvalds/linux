/*
 * arch/sh/mm/kmap.c
 *
 * Copyright (C) 1999, 2000, 2002  Niibe Yutaka
 * Copyright (C) 2002 - 2009  Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/mutex.h>
#include <linux/fs.h>
#include <linux/highmem.h>
#include <linux/module.h>
#include <asm/mmu_context.h>
#include <asm/cacheflush.h>

#define kmap_get_fixmap_pte(vaddr)                                     \
	pte_offset_kernel(pmd_offset(pud_offset(pgd_offset_k(vaddr), (vaddr)), (vaddr)), (vaddr))

static pte_t *kmap_coherent_pte;

void __init kmap_coherent_init(void)
{
	unsigned long vaddr;

	if (!boot_cpu_data.dcache.n_aliases)
		return;

	/* cache the first coherent kmap pte */
	vaddr = __fix_to_virt(FIX_CMAP_BEGIN);
	kmap_coherent_pte = kmap_get_fixmap_pte(vaddr);
}

void *kmap_coherent(struct page *page, unsigned long addr)
{
	enum fixed_addresses idx;
	unsigned long vaddr, flags;
	pte_t pte;

	BUG_ON(test_bit(PG_dcache_dirty, &page->flags));

	inc_preempt_count();

	idx = (addr & current_cpu_data.dcache.alias_mask) >> PAGE_SHIFT;
	vaddr = __fix_to_virt(FIX_CMAP_END - idx);
	pte = mk_pte(page, PAGE_KERNEL);

	local_irq_save(flags);
	flush_tlb_one(get_asid(), vaddr);
	local_irq_restore(flags);

	update_mmu_cache(NULL, vaddr, pte);

	set_pte(kmap_coherent_pte - (FIX_CMAP_END - idx), pte);

	return (void *)vaddr;
}

void kunmap_coherent(void)
{
	dec_preempt_count();
	preempt_check_resched();
}
