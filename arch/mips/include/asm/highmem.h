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
#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <asm/cpu-features.h>
#include <asm/kmap_size.h>

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *pkmap_page_table;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#ifdef CONFIG_PHYS_ADDR_T_64BIT
#define LAST_PKMAP 512
#else
#define LAST_PKMAP 1024
#endif

#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)	((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)	(PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define ARCH_HAS_KMAP_FLUSH_TLB
extern void kmap_flush_tlb(unsigned long addr);

#define flush_cache_kmaps()	BUG_ON(cpu_has_dc_aliases)

#define arch_kmap_local_post_map(vaddr, pteval)	local_flush_tlb_one(vaddr)
#define arch_kmap_local_post_unmap(vaddr)	local_flush_tlb_one(vaddr)

#endif /* __KERNEL__ */

#endif /* _ASM_HIGHMEM_H */
