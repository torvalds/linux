// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2018 Hangzhou C-SKY Microsystems co.,ltd.

#include <linux/module.h>
#include <linux/highmem.h>
#include <linux/smp.h>
#include <linux/memblock.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>
#include <asm/cacheflush.h>

static pte_t *kmap_pte;

unsigned long highstart_pfn, highend_pfn;

void kmap_flush_tlb(unsigned long addr)
{
	flush_tlb_one(addr);
}
EXPORT_SYMBOL(kmap_flush_tlb);

void *kmap_atomic_high_prot(struct page *page, pgprot_t prot)
{
	unsigned long vaddr;
	int idx, type;

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#ifdef CONFIG_DEBUG_HIGHMEM
	BUG_ON(!pte_none(*(kmap_pte - idx)));
#endif
	set_pte(kmap_pte-idx, mk_pte(page, prot));
	flush_tlb_one((unsigned long)vaddr);

	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic_high_prot);

void kunmap_atomic_high(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;
	int idx;

	if (vaddr < FIXADDR_START)
		return;

#ifdef CONFIG_DEBUG_HIGHMEM
	idx = KM_TYPE_NR*smp_processor_id() + kmap_atomic_idx();

	BUG_ON(vaddr != __fix_to_virt(FIX_KMAP_BEGIN + idx));

	pte_clear(&init_mm, vaddr, kmap_pte - idx);
	flush_tlb_one(vaddr);
#else
	(void) idx; /* to kill a warning */
#endif
	kmap_atomic_idx_pop();
}
EXPORT_SYMBOL(kunmap_atomic_high);

/*
 * This is the same as kmap_atomic() but can map memory that doesn't
 * have a struct page associated with it.
 */
void *kmap_atomic_pfn(unsigned long pfn)
{
	unsigned long vaddr;
	int idx, type;

	pagefault_disable();

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	set_pte(kmap_pte-idx, pfn_pte(pfn, PAGE_KERNEL));
	flush_tlb_one(vaddr);

	return (void *) vaddr;
}

static void __init kmap_pages_init(void)
{
	unsigned long vaddr;
	pgd_t *pgd;
	pmd_t *pmd;
	pud_t *pud;
	pte_t *pte;

	vaddr = PKMAP_BASE;
	fixrange_init(vaddr, vaddr + PAGE_SIZE*LAST_PKMAP, swapper_pg_dir);

	pgd = swapper_pg_dir + pgd_index(vaddr);
	pud = (pud_t *)pgd;
	pmd = pmd_offset(pud, vaddr);
	pte = pte_offset_kernel(pmd, vaddr);
	pkmap_page_table = pte;
}

void __init kmap_init(void)
{
	unsigned long vaddr;

	kmap_pages_init();

	vaddr = __fix_to_virt(FIX_KMAP_BEGIN);

	kmap_pte = pte_offset_kernel((pmd_t *)pgd_offset_k(vaddr), vaddr);
}
