/*
 * arch/arm/mm/highmem.c -- ARM highmem support
 *
 * Author:	Nicolas Pitre
 * Created:	september 8, 2008
 * Copyright:	Marvell Semiconductors Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <asm/fixmap.h>
#include <asm/cacheflush.h>
#include <asm/tlbflush.h>
#include "mm.h"

void *kmap(struct page *page)
{
	might_sleep();
	if (!PageHighMem(page))
		return page_address(page);
	return kmap_high(page);
}
EXPORT_SYMBOL(kmap);

void kunmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}
EXPORT_SYMBOL(kunmap);

void *__kmap_atomic(struct page *page)
{
	unsigned int idx;
	unsigned long vaddr;
	void *kmap;
	int type;

	pagefault_disable();
	if (!PageHighMem(page))
		return page_address(page);

#ifdef CONFIG_DEBUG_HIGHMEM
	/*
	 * There is no cache coherency issue when non VIVT, so force the
	 * dedicated kmap usage for better debugging purposes in that case.
	 */
	if (!cache_is_vivt())
		kmap = NULL;
	else
#endif
		kmap = kmap_high_get(page);
	if (kmap)
		return kmap;

	type = kmap_atomic_idx_push();

	idx = type + KM_TYPE_NR * smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#ifdef CONFIG_DEBUG_HIGHMEM
	/*
	 * With debugging enabled, kunmap_atomic forces that entry to 0.
	 * Make sure it was indeed properly unmapped.
	 */
	BUG_ON(!pte_none(*(TOP_PTE(vaddr))));
#endif
	set_pte_ext(TOP_PTE(vaddr), mk_pte(page, kmap_prot), 0);
	/*
	 * When debugging is off, kunmap_atomic leaves the previous mapping
	 * in place, so this TLB flush ensures the TLB is updated with the
	 * new mapping.
	 */
	local_flush_tlb_kernel_page(vaddr);

	return (void *)vaddr;
}
EXPORT_SYMBOL(__kmap_atomic);

void __kunmap_atomic(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	int idx, type;

	if (kvaddr >= (void *)FIXADDR_START) {
		type = kmap_atomic_idx();
		idx = type + KM_TYPE_NR * smp_processor_id();

		if (cache_is_vivt())
			__cpuc_flush_dcache_area((void *)vaddr, PAGE_SIZE);
#ifdef CONFIG_DEBUG_HIGHMEM
		BUG_ON(vaddr != __fix_to_virt(FIX_KMAP_BEGIN + idx));
		set_pte_ext(TOP_PTE(vaddr), __pte(0), 0);
		local_flush_tlb_kernel_page(vaddr);
#else
		(void) idx;  /* to kill a warning */
#endif
		kmap_atomic_idx_pop();
	} else if (vaddr >= PKMAP_ADDR(0) && vaddr < PKMAP_ADDR(LAST_PKMAP)) {
		/* this address was obtained through kmap_high_get() */
		kunmap_high(pte_page(pkmap_page_table[PKMAP_NR(vaddr)]));
	}
	pagefault_enable();
}
EXPORT_SYMBOL(__kunmap_atomic);

void *kmap_atomic_pfn(unsigned long pfn)
{
	unsigned long vaddr;
	int idx, type;

	pagefault_disable();

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR * smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#ifdef CONFIG_DEBUG_HIGHMEM
	BUG_ON(!pte_none(*(TOP_PTE(vaddr))));
#endif
	set_pte_ext(TOP_PTE(vaddr), pfn_pte(pfn, kmap_prot), 0);
	local_flush_tlb_kernel_page(vaddr);

	return (void *)vaddr;
}

struct page *kmap_atomic_to_page(const void *ptr)
{
	unsigned long vaddr = (unsigned long)ptr;
	pte_t *pte;

	if (vaddr < FIXADDR_START)
		return virt_to_page(ptr);

	pte = TOP_PTE(vaddr);
	return pte_page(*pte);
}

#ifdef CONFIG_CPU_CACHE_VIPT

#include <linux/percpu.h>

/*
 * The VIVT cache of a highmem page is always flushed before the page
 * is unmapped. Hence unmapped highmem pages need no cache maintenance
 * in that case.
 *
 * However unmapped pages may still be cached with a VIPT cache, and
 * it is not possible to perform cache maintenance on them using physical
 * addresses unfortunately.  So we have no choice but to set up a temporary
 * virtual mapping for that purpose.
 *
 * Yet this VIPT cache maintenance may be triggered from DMA support
 * functions which are possibly called from interrupt context. As we don't
 * want to keep interrupt disabled all the time when such maintenance is
 * taking place, we therefore allow for some reentrancy by preserving and
 * restoring the previous fixmap entry before the interrupted context is
 * resumed.  If the reentrancy depth is 0 then there is no need to restore
 * the previous fixmap, and leaving the current one in place allow it to
 * be reused the next time without a TLB flush (common with DMA).
 */

static DEFINE_PER_CPU(int, kmap_high_l1_vipt_depth);

void *kmap_high_l1_vipt(struct page *page, pte_t *saved_pte)
{
	unsigned int idx, cpu;
	int *depth;
	unsigned long vaddr, flags;
	pte_t pte, *ptep;

	if (!in_interrupt())
		preempt_disable();

	cpu = smp_processor_id();
	depth = &per_cpu(kmap_high_l1_vipt_depth, cpu);

	idx = KM_L1_CACHE + KM_TYPE_NR * cpu;
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	ptep = TOP_PTE(vaddr);
	pte = mk_pte(page, kmap_prot);

	raw_local_irq_save(flags);
	(*depth)++;
	if (pte_val(*ptep) == pte_val(pte)) {
		*saved_pte = pte;
	} else {
		*saved_pte = *ptep;
		set_pte_ext(ptep, pte, 0);
		local_flush_tlb_kernel_page(vaddr);
	}
	raw_local_irq_restore(flags);

	return (void *)vaddr;
}

void kunmap_high_l1_vipt(struct page *page, pte_t saved_pte)
{
	unsigned int idx, cpu = smp_processor_id();
	int *depth = &per_cpu(kmap_high_l1_vipt_depth, cpu);
	unsigned long vaddr, flags;
	pte_t pte, *ptep;

	idx = KM_L1_CACHE + KM_TYPE_NR * cpu;
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	ptep = TOP_PTE(vaddr);
	pte = mk_pte(page, kmap_prot);

	BUG_ON(pte_val(*ptep) != pte_val(pte));
	BUG_ON(*depth <= 0);

	raw_local_irq_save(flags);
	(*depth)--;
	if (*depth != 0 && pte_val(pte) != pte_val(saved_pte)) {
		set_pte_ext(ptep, saved_pte, 0);
		local_flush_tlb_kernel_page(vaddr);
	}
	raw_local_irq_restore(flags);

	if (!in_interrupt())
		preempt_enable();
}

#endif  /* CONFIG_CPU_CACHE_VIPT */
