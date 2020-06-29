// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/export.h>
#include <linux/highmem.h>
#include <linux/sched.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/memblock.h>
#include <asm/fixmap.h>
#include <asm/tlbflush.h>

void *kmap_atomic_high_prot(struct page *page, pgprot_t prot)
{
	unsigned int idx;
	unsigned long vaddr, pte;
	int type;
	pte_t *ptep;

	type = kmap_atomic_idx_push();

	idx = type + KM_TYPE_NR * smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	pte = (page_to_pfn(page) << PAGE_SHIFT) | prot;
	ptep = pte_offset_kernel(pmd_off_k(vaddr), vaddr);
	set_pte(ptep, pte);

	__nds32__tlbop_inv(vaddr);
	__nds32__mtsr_dsb(vaddr, NDS32_SR_TLB_VPN);
	__nds32__tlbop_rwr(pte);
	__nds32__isb();
	return (void *)vaddr;
}
EXPORT_SYMBOL(kmap_atomic_high_prot);

void kunmap_atomic_high(void *kvaddr)
{
	if (kvaddr >= (void *)FIXADDR_START) {
		unsigned long vaddr = (unsigned long)kvaddr;
		pte_t *ptep;
		kmap_atomic_idx_pop();
		__nds32__tlbop_inv(vaddr);
		__nds32__isb();
		ptep = pte_offset_kernel(pmd_off_k(vaddr), vaddr);
		set_pte(ptep, 0);
	}
}
EXPORT_SYMBOL(kunmap_atomic_high);
