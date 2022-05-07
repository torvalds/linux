/* SPDX-License-Identifier: GPL-2.0 */
/*
 * page.h:  Various defines and such for MMU operations on the Sparc for
 *          the Linux kernel.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#ifndef _SPARC_PAGE_H
#define _SPARC_PAGE_H

#include <linux/const.h>

#define PAGE_SHIFT   12
#define PAGE_SIZE    (_AC(1, UL) << PAGE_SHIFT)
#define PAGE_MASK    (~(PAGE_SIZE-1))

#ifndef __ASSEMBLY__

#define clear_page(page)	 memset((void *)(page), 0, PAGE_SIZE)
#define copy_page(to,from) 	memcpy((void *)(to), (void *)(from), PAGE_SIZE)
#define clear_user_page(addr, vaddr, page)	\
	do { 	clear_page(addr);		\
		sparc_flush_page_to_ram(page);	\
	} while (0)
#define copy_user_page(to, from, vaddr, page)	\
	do {	copy_page(to, from);		\
		sparc_flush_page_to_ram(page);	\
	} while (0)

/* The following structure is used to hold the physical
 * memory configuration of the machine.  This is filled in
 * prom_meminit() and is later used by mem_init() to set up
 * mem_map[].  We statically allocate SPARC_PHYS_BANKS+1 of
 * these structs, this is arbitrary.  The entry after the
 * last valid one has num_bytes==0.
 */
struct sparc_phys_banks {
  unsigned long base_addr;
  unsigned long num_bytes;
};

#define SPARC_PHYS_BANKS 32

extern struct sparc_phys_banks sp_banks[SPARC_PHYS_BANKS+1];

/* passing structs on the Sparc slow us down tremendously... */

/* #define STRICT_MM_TYPECHECKS */

#ifdef STRICT_MM_TYPECHECKS
/*
 * These are used to make use of C type-checking..
 */
typedef struct { unsigned long pte; } pte_t;
typedef struct { unsigned long iopte; } iopte_t;
typedef struct { unsigned long pmd; } pmd_t;
typedef struct { unsigned long pgd; } pgd_t;
typedef struct { unsigned long ctxd; } ctxd_t;
typedef struct { unsigned long pgprot; } pgprot_t;
typedef struct { unsigned long iopgprot; } iopgprot_t;

#define pte_val(x)	((x).pte)
#define iopte_val(x)	((x).iopte)
#define pmd_val(x)      ((x).pmd)
#define pgd_val(x)	((x).pgd)
#define ctxd_val(x)	((x).ctxd)
#define pgprot_val(x)	((x).pgprot)
#define iopgprot_val(x)	((x).iopgprot)

#define __pte(x)	((pte_t) { (x) } )
#define __pmd(x)	((pmd_t) { { (x) }, })
#define __iopte(x)	((iopte_t) { (x) } )
#define __pgd(x)	((pgd_t) { (x) } )
#define __ctxd(x)	((ctxd_t) { (x) } )
#define __pgprot(x)	((pgprot_t) { (x) } )
#define __iopgprot(x)	((iopgprot_t) { (x) } )

#else
/*
 * .. while these make it easier on the compiler
 */
typedef unsigned long pte_t;
typedef unsigned long iopte_t;
typedef unsigned long pmd_t;
typedef unsigned long pgd_t;
typedef unsigned long ctxd_t;
typedef unsigned long pgprot_t;
typedef unsigned long iopgprot_t;

#define pte_val(x)	(x)
#define iopte_val(x)	(x)
#define pmd_val(x)      (x)
#define pgd_val(x)	(x)
#define ctxd_val(x)	(x)
#define pgprot_val(x)	(x)
#define iopgprot_val(x)	(x)

#define __pte(x)	(x)
#define __pmd(x)	(x)
#define __iopte(x)	(x)
#define __pgd(x)	(x)
#define __ctxd(x)	(x)
#define __pgprot(x)	(x)
#define __iopgprot(x)	(x)

#endif

typedef pte_t *pgtable_t;

#define TASK_UNMAPPED_BASE	0x50000000

#else /* !(__ASSEMBLY__) */

#define __pgprot(x)	(x)

#endif /* !(__ASSEMBLY__) */

#define PAGE_OFFSET	0xf0000000
#ifndef __ASSEMBLY__
extern unsigned long phys_base;
extern unsigned long pfn_base;
#endif
#define __pa(x)			((unsigned long)(x) - PAGE_OFFSET + phys_base)
#define __va(x)			((void *)((unsigned long) (x) - phys_base + PAGE_OFFSET))

#define virt_to_phys		__pa
#define phys_to_virt		__va

#define ARCH_PFN_OFFSET		(pfn_base)
#define virt_to_page(kaddr)	pfn_to_page(__pa(kaddr) >> PAGE_SHIFT)

#define pfn_valid(pfn)		(((pfn) >= (pfn_base)) && (((pfn)-(pfn_base)) < max_mapnr))
#define virt_addr_valid(kaddr)	((((unsigned long)(kaddr)-PAGE_OFFSET)>>PAGE_SHIFT) < max_mapnr)

#include <asm-generic/memory_model.h>
#include <asm-generic/getorder.h>

#endif /* _SPARC_PAGE_H */
