/* MN10300 Virtual kernel memory mappings for high memory
 *
 * Copyright (C) 2007 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 * - Derived from include/asm-i386/highmem.h
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/highmem.h>
#include <asm/kmap_types.h>
#include <asm/pgtable.h>

/* undef for production */
#undef HIGHMEM_DEBUG

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *kmap_pte;
extern pgprot_t kmap_prot;
extern pte_t *pkmap_page_table;

extern void __init kmap_init(void);

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#define PKMAP_BASE	0xfe000000UL
#define LAST_PKMAP	1024
#define LAST_PKMAP_MASK (LAST_PKMAP - 1)
#define PKMAP_NR(virt)  ((virt - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

extern unsigned long kmap_high(struct page *page);
extern void kunmap_high(struct page *page);

static inline unsigned long kmap(struct page *page)
{
	if (in_interrupt())
		BUG();
	if (page < highmem_start_page)
		return page_address(page);
	return kmap_high(page);
}

static inline void kunmap(struct page *page)
{
	if (in_interrupt())
		BUG();
	if (page < highmem_start_page)
		return;
	kunmap_high(page);
}

/*
 * The use of kmap_atomic/kunmap_atomic is discouraged - kmap/kunmap
 * gives a more generic (and caching) interface. But kmap_atomic can
 * be used in IRQ contexts, so in some (very limited) cases we need
 * it.
 */
static inline unsigned long kmap_atomic(struct page *page)
{
	unsigned long vaddr;
	int idx, type;

	pagefault_disable();
	if (page < highmem_start_page)
		return page_address(page);

	type = kmap_atomic_idx_push();
	idx = type + KM_TYPE_NR * smp_processor_id();
	vaddr = __fix_to_virt(FIX_KMAP_BEGIN + idx);
#if HIGHMEM_DEBUG
	if (!pte_none(*(kmap_pte - idx)))
		BUG();
#endif
	set_pte(kmap_pte - idx, mk_pte(page, kmap_prot));
	local_flush_tlb_one(vaddr);

	return vaddr;
}

static inline void __kunmap_atomic(unsigned long vaddr)
{
	int type;

	if (vaddr < FIXADDR_START) { /* FIXME */
		pagefault_enable();
		return;
	}

	type = kmap_atomic_idx();

#if HIGHMEM_DEBUG
	{
		unsigned int idx;
		idx = type + KM_TYPE_NR * smp_processor_id();

		if (vaddr != __fix_to_virt(FIX_KMAP_BEGIN + idx))
			BUG();

		/*
		 * force other mappings to Oops if they'll try to access
		 * this pte without first remap it
		 */
		pte_clear(kmap_pte - idx);
		local_flush_tlb_one(vaddr);
	}
#endif

	kmap_atomic_idx_pop();
	pagefault_enable();
}
#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
