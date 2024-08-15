/* SPDX-License-Identifier: GPL-2.0 */
/*
 * highmem.h: virtual kernel memory mappings for high memory
 *
 * Used in CONFIG_HIGHMEM systems for memory pages which
 * are not addressable by direct kernel virtual addresses.
 *
 * Copyright (C) 1999 Gerhard Wichert, Siemens AG
 *		      Gerhard.Wichert@pdb.siemens.de
 *
 *
 * Redesigned the x86 32-bit VM architecture to deal with
 * up to 16 Terabyte physical memory. With current x86 CPUs
 * we now support up to 64 Gigabytes physical RAM.
 *
 * Copyright (C) 1999 Ingo Molnar <mingo@redhat.com>
 */

#ifndef _ASM_X86_HIGHMEM_H
#define _ASM_X86_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/interrupt.h>
#include <linux/threads.h>
#include <asm/tlbflush.h>
#include <asm/paravirt.h>
#include <asm/fixmap.h>
#include <asm/pgtable_areas.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
/*
 * Ordering is:
 *
 * high memory on:			              high_memory off:
 *    FIXADDR_TOP                                        FIXADDR_TOP
 *        fixed addresses                                    fixed addresses
 *    FIXADDR_START                                      FIXADDR_START
 *        temp fixed addresses/persistent kmap area      VMALLOC_END
 *    PKMAP_BASE                                             temp fixed addresses/vmalloc area
 *    VMALLOC_END                                        VMALLOC_START
 *        vmalloc area                                   high_memory
 *    VMALLOC_START
 *    high_memory
 *
 * The temp fixed area is only used during boot for early_ioremap(), and
 * it is unused when the ioremap() is functional. vmalloc/pkmap area become
 * available after early boot so the temp fixed area is available for re-use.
 */
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define flush_cache_kmaps()	do { } while (0)

#define	arch_kmap_local_post_map(vaddr, pteval)		\
	arch_flush_lazy_mmu_mode()

#define	arch_kmap_local_post_unmap(vaddr)		\
	do {						\
		flush_tlb_one_kernel((vaddr));		\
		arch_flush_lazy_mmu_mode();		\
	} while (0)

extern void add_highpages_with_active_regions(int nid, unsigned long start_pfn,
					unsigned long end_pfn);

#endif /* __KERNEL__ */

#endif /* _ASM_X86_HIGHMEM_H */
