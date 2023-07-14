/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Page management definitions for the Hexagon architecture
 *
 * Copyright (c) 2010-2013, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_PAGE_H
#define _ASM_PAGE_H

#include <linux/const.h>

/*  This is probably not the most graceful way to handle this.  */

#ifdef CONFIG_PAGE_SIZE_4KB
#define PAGE_SHIFT 12
#define HEXAGON_L1_PTE_SIZE __HVM_PDE_S_4KB
#endif

#ifdef CONFIG_PAGE_SIZE_16KB
#define PAGE_SHIFT 14
#define HEXAGON_L1_PTE_SIZE __HVM_PDE_S_16KB
#endif

#ifdef CONFIG_PAGE_SIZE_64KB
#define PAGE_SHIFT 16
#define HEXAGON_L1_PTE_SIZE __HVM_PDE_S_64KB
#endif

#ifdef CONFIG_PAGE_SIZE_256KB
#define PAGE_SHIFT 18
#define HEXAGON_L1_PTE_SIZE __HVM_PDE_S_256KB
#endif

#ifdef CONFIG_PAGE_SIZE_1MB
#define PAGE_SHIFT 20
#define HEXAGON_L1_PTE_SIZE __HVM_PDE_S_1MB
#endif

/*
 *  These should be defined in hugetlb.h, but apparently not.
 *  "Huge" for us should be 4MB or 16MB, which are both represented
 *  in L1 PTE's.  Right now, it's set up for 4MB.
 */
#ifdef CONFIG_HUGETLB_PAGE
#define HPAGE_SHIFT 22
#define HPAGE_SIZE (1UL << HPAGE_SHIFT)
#define HPAGE_MASK (~(HPAGE_SIZE-1))
#define HUGETLB_PAGE_ORDER (HPAGE_SHIFT-PAGE_SHIFT)
#define HVM_HUGEPAGE_SIZE 0x5
#endif

#define PAGE_SIZE  (1UL << PAGE_SHIFT)
#define PAGE_MASK  (~((1 << PAGE_SHIFT) - 1))

#ifdef __KERNEL__
#ifndef __ASSEMBLY__

/*
 * This is for PFN_DOWN, which mm.h needs.  Seems the right place to pull it in.
 */
#include <linux/pfn.h>

/*
 * We implement a two-level architecture-specific page table structure.
 * Null intermediate page table level (pmd, pud) definitions will come from
 * asm-generic/pagetable-nopmd.h and asm-generic/pagetable-nopud.h
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct page *pgtable_t;

#define pte_val(x)     ((x).pte)
#define pgd_val(x)     ((x).pgd)
#define pgprot_val(x)  ((x).pgprot)
#define __pte(x)       ((pte_t) { (x) })
#define __pgd(x)       ((pgd_t) { (x) })
#define __pgprot(x)    ((pgprot_t) { (x) })

/*
 * We need a __pa and a __va routine for kernel space.
 * MIPS says they're only used during mem_init.
 * also, check if we need a PHYS_OFFSET.
 */
#define __pa(x) ((unsigned long)(x) - PAGE_OFFSET + PHYS_OFFSET)
#define __va(x) ((void *)((unsigned long)(x) - PHYS_OFFSET + PAGE_OFFSET))

/* The "page frame" descriptor is defined in linux/mm.h */
struct page;

/* Returns page frame descriptor for virtual address. */
#define virt_to_page(kaddr) pfn_to_page(PFN_DOWN(__pa(kaddr)))

/* Default vm area behavior is non-executable.  */
#define VM_DATA_DEFAULT_FLAGS	VM_DATA_FLAGS_NON_EXEC

#define virt_addr_valid(kaddr) pfn_valid(__pa(kaddr) >> PAGE_SHIFT)

/*  Need to not use a define for linesize; may move this to another file.  */
static inline void clear_page(void *page)
{
	/*  This can only be done on pages with L1 WB cache */
	asm volatile(
		"	loop0(1f,%1);\n"
		"1:	{ dczeroa(%0);\n"
		"	  %0 = add(%0,#32); }:endloop0\n"
		: "+r" (page)
		: "r" (PAGE_SIZE/32)
		: "lc0", "sa0", "memory"
	);
}

#define copy_page(to, from)	memcpy((to), (from), PAGE_SIZE)

/*
 * Under assumption that kernel always "sees" user map...
 */
#define clear_user_page(page, vaddr, pg)	clear_page(page)
#define copy_user_page(to, from, vaddr, pg)	copy_page(to, from)

/*
 * page_to_phys - convert page to physical address
 * @page - pointer to page entry in mem_map
 */
#define page_to_phys(page)      (page_to_pfn(page) << PAGE_SHIFT)

#define virt_to_pfn(kaddr)      (__pa(kaddr) >> PAGE_SHIFT)
#define pfn_to_virt(pfn)        __va((pfn) << PAGE_SHIFT)

#define page_to_virt(page)	__va(page_to_phys(page))

#include <asm/mem-layout.h>
#include <asm-generic/memory_model.h>
/* XXX Todo: implement assembly-optimized version of getorder. */
#include <asm-generic/getorder.h>

#endif /* ifdef __ASSEMBLY__ */
#endif /* ifdef __KERNEL__ */

#endif
