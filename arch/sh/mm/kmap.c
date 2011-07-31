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

	/* cache the first coherent kmap pte */
	vaddr = __fix_to_virt(FIX_CMAP_BEGIN);
	kmap_coherent_pte = kmap_get_fixmap_pte(vaddr);
}

void *kmap_coherent(struct page *page, unsigned long addr)
{
	enum fixed_addresses idx;
	unsigned long vaddr;

	BUG_ON(test_bit(PG_dcache_dirty, &page->flags));

	pagefault_disable();

	idx = FIX_CMAP_END -
		(((addr >> PAGE_SHIFT) & (FIX_N_COLOURS - 1)) +
		 (FIX_N_COLOURS * smp_processor_id()));

	vaddr = __fix_to_virt(idx);

	BUG_ON(!pte_none(*(kmap_coherent_pte - idx)));
	set_pte(kmap_coherent_pte - idx, mk_pte(page, PAGE_KERNEL));

	return (void *)vaddr;
}

void kunmap_coherent(void *kvaddr)
{
	if (kvaddr >= (void *)FIXADDR_START) {
		unsigned long vaddr = (unsigned long)kvaddr & PAGE_MASK;
		enum fixed_addresses idx = __virt_to_fix(vaddr);

		/* XXX.. Kill this later, here for sanity at the moment.. */
		__flush_purge_region((void *)vaddr, PAGE_SIZE);

		pte_clear(&init_mm, vaddr, kmap_coherent_pte - idx);
		local_flush_tlb_one(get_asid(), vaddr);
	}

	pagefault_enable();
}
