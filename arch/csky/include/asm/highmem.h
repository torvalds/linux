/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __ASM_CSKY_HIGHMEM_H
#define __ASM_CSKY_HIGHMEM_H

#ifdef __KERNEL__

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/uaccess.h>
#include <asm/kmap_size.h>
#include <asm/cache.h>

/* undef for production */
#define HIGHMEM_DEBUG 1

/* declarations for highmem.c */
extern unsigned long highstart_pfn, highend_pfn;

extern pte_t *pkmap_page_table;

/*
 * Right now we initialize only a single pte table. It can be extended
 * easily, subsequent pte tables have to be allocated in one physical
 * chunk of RAM.
 */
#define LAST_PKMAP 1024
#define LAST_PKMAP_MASK (LAST_PKMAP-1)
#define PKMAP_NR(virt)  ((virt-PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)  (PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define ARCH_HAS_KMAP_FLUSH_TLB
extern void kmap_flush_tlb(unsigned long addr);

#define flush_cache_kmaps() do {} while (0)

#define arch_kmap_local_post_map(vaddr, pteval)	kmap_flush_tlb(vaddr)
#define arch_kmap_local_post_unmap(vaddr)	kmap_flush_tlb(vaddr)

extern void kmap_init(void);

#endif /* __KERNEL__ */

#endif /* __ASM_CSKY_HIGHMEM_H */
