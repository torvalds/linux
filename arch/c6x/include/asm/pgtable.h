/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#ifndef _ASM_C6X_PGTABLE_H
#define _ASM_C6X_PGTABLE_H

#include <asm-generic/4level-fixup.h>

#include <asm/setup.h>
#include <asm/page.h>

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0
#define	VMALLOC_END	0xffffffff

#define pgd_present(pgd)	(1)
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr) (1)

#define pmd_offset(a, b)	((void *)0)
#define pmd_none(x)		(!pmd_val(x))
#define pmd_present(x)		(pmd_val(x))
#define pmd_clear(xp)		do { set_pmd(xp, __pmd(0)); } while (0)
#define pmd_bad(x)		(pmd_val(x) & ~PAGE_MASK)

#define PAGE_NONE		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_SHARED		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_COPY		__pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_READONLY	        __pgprot(0)    /* these mean nothing to NO_MM */
#define PAGE_KERNEL		__pgprot(0)    /* these mean nothing to NO_MM */
#define pgprot_noncached(prot)	(prot)

extern void paging_init(void);

#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ, off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

#define set_pte(pteptr, pteval) (*(pteptr) = pteval)
#define set_pte_at(mm, addr, ptep, pteval) set_pte(ptep, pteval)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)	virt_to_page(empty_zero_page)
extern unsigned long empty_zero_page;

#define swapper_pg_dir ((pgd_t *) 0)

/*
 * No page table caches to initialise
 */
#define pgtable_cache_init()   do { } while (0)

/*
 * c6x is !MMU, so define the simpliest implementation
 */
#define pgprot_writecombine pgprot_noncached

#include <asm-generic/pgtable.h>

#endif /* _ASM_C6X_PGTABLE_H */
