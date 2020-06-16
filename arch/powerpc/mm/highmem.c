// SPDX-License-Identifier: GPL-2.0
/*
 * highmem.c: virtual kernel memory mappings for high memory
 *
 * PowerPC version, stolen from the i386 version.
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * up to 16 Terrabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 *
 * Reworked for PowerPC by various contributors. Moved from
 * highmem.h by Benjamin Herrenschmidt (c) 2009 IBM Corp.
 */

#include <linux/highmem.h>
#include <linux/module.h>

void *kmap_atomic_high_prot(struct page *page, pgprot_t prot)
{
	unsigned long vaddr;
	int idx, type;

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR*smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
	WARN_ON(IS_ENABLED(CONFIG_DEBUG_HIGHMEM) && !pte_none(*(kmap_pte - idx)));
	__set_pte_at(&init_mm, vaddr, kmap_pte-idx, mk_pte(page, prot), 1);
	local_flush_tlb_page(NULL, vaddr);

	return (void*) vaddr;
}
EXPORT_SYMBOL(kmap_atomic_high_prot);

void kunmap_atomic_high(void *kvaddr)
{
	unsigned long vaddr = (unsigned long) kvaddr & PAGE_MASK;

	if (vaddr < __fix_to_virt(FIX_KMAP_END))
		return;

	if (IS_ENABLED(CONFIG_DEBUG_HIGHMEM)) {
		int type = kmap_atomic_idx();
		unsigned int idx;

		idx = type + KM_TYPE_NR * smp_processor_id();
		WARN_ON(vaddr != __fix_to_virt(FIX_KMAP_BEGIN + idx));

		/*
		 * force other mappings to Oops if they'll try to access
		 * this pte without first remap it
		 */
		pte_clear(&init_mm, vaddr, kmap_pte-idx);
		local_flush_tlb_page(NULL, vaddr);
	}

	kmap_atomic_idx_pop();
}
EXPORT_SYMBOL(kunmap_atomic_high);
