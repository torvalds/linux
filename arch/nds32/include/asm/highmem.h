/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#include <asm/proc-fns.h>
#include <asm/fixmap.h>

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
/*
 * Ordering is (from lower to higher memory addresses):
 *
 * high_memory
 *			Persistent kmap area
 * PKMAP_BASE
 *			fixed_addresses
 * FIXADDR_START
 * FIXADDR_TOP
 *			Vmalloc area
 * VMALLOC_START
 * VMALLOC_END
 */
#define PKMAP_BASE		((FIXADDR_START - PGDIR_SIZE) & (PGDIR_MASK))
#define LAST_PKMAP		PTRS_PER_PTE
#define LAST_PKMAP_MASK		(LAST_PKMAP - 1)
#define PKMAP_NR(virt)		(((virt) - (PKMAP_BASE)) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)		(PKMAP_BASE + ((nr) << PAGE_SHIFT))

static inline void flush_cache_kmaps(void)
{
	cpu_dcache_wbinval_all();
}

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *pkmap_page_table;

extern void kmap_init(void);

/*
 * FIXME: The below looks broken vs. a kmap_atomic() in task context which
 * is interupted and another kmap_atomic() happens in interrupt context.
 * But what do I know about nds32. -- tglx
 */
#define arch_kmap_local_post_map(vaddr, pteval)			\
	do {							\
		__nds32__tlbop_inv(vaddr);			\
		__nds32__mtsr_dsb(vaddr, NDS32_SR_TLB_VPN);	\
		__nds32__tlbop_rwr(pteval);			\
		__nds32__isb();					\
	} while (0)

#define arch_kmap_local_pre_unmap(vaddr)			\
	do {							\
		__nds32__tlbop_inv(vaddr);			\
		__nds32__isb();					\
	} while (0)

#endif
