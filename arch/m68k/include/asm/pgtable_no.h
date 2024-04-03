/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68KNOMMU_PGTABLE_H
#define _M68KNOMMU_PGTABLE_H

#include <asm-generic/pgtable-nopud.h>

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
#define	pmd_offset(a, b)	((void *)0)

#define PAGE_NONE	__pgprot(0)
#define PAGE_SHARED	__pgprot(0)
#define PAGE_COPY	__pgprot(0)
#define PAGE_READONLY	__pgprot(0)
#define PAGE_KERNEL	__pgprot(0)

#define swapper_pg_dir ((pgd_t *) 0)

/*
 * ZERO_PAGE is a global shared page that is always zero: used
 * for zero-mapped memory areas etc..
 */
extern void *empty_zero_page;
#define ZERO_PAGE(vaddr)	(virt_to_page(empty_zero_page))

/*
 * All 32bit addresses are effectively valid for vmalloc...
 * Sort of meaningless for non-VM targets.
 */
#define	VMALLOC_START	0
#define	VMALLOC_END	0xffffffff
#define	KMAP_START	0
#define	KMAP_END	0xffffffff

#endif /* _M68KNOMMU_PGTABLE_H */
