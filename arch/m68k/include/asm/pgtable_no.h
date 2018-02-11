/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68KNOMMU_PGTABLE_H
#define _M68KNOMMU_PGTABLE_H

#include <asm-generic/4level-fixup.h>

/*
 * (C) Copyright 2000-2002, Greg Ungerer <gerg@snapgear.com>
 */

#include <linux/slab.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/io.h>

/*
 * Trivial page table functions.
 */
#define pgd_present(pgd)	(1)
#define pgd_none(pgd)		(0)
#define pgd_bad(pgd)		(0)
#define pgd_clear(pgdp)
#define kern_addr_valid(addr)	(1)
#define	pmd_offset(a, b)	((void *)0)

#define PAGE_NONE	__pgprot(0)
#define PAGE_SHARED	__pgprot(0)
#define PAGE_COPY	__pgprot(0)
#define PAGE_READONLY	__pgprot(0)
#define PAGE_KERNEL	__pgprot(0)

extern void paging_init(void);
#define swapper_pg_dir ((pgd_t *) 0)

#define __swp_type(x)		(0)
#define __swp_offset(x)		(0)
#define __swp_entry(typ,off)	((swp_entry_t) { ((typ) | ((off) << 7)) })
#define __pte_to_swp_entry(pte)	((swp_entry_t) { pte_val(pte) })
#define __swp_entry_to_pte(x)	((pte_t) { (x).val })

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
#define ZERO_PAGE(vaddr)	(virt_to_page(0))

/*
 * No page table caches to initialise.
 */
#define pgtable_cache_init()	do { } while (0)

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0
#define	VMALLOC_END	0xffffffff
#define	KMAP_START	0
#define	KMAP_END	0xffffffff

#include <asm-generic/pgtable.h>

#define check_pgt_cache()	do { } while (0)

#endif /* _M68KNOMMU_PGTABLE_H */
