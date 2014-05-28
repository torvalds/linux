/*
 * include/asm-xtensa/highmem.h
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2003 - 2005 Tensilica Inc.
 * Copyright (C) 2014 Cadence Design Systems Inc.
 */

#ifndef _XTENSA_HIGHMEM_H
#define _XTENSA_HIGHMEM_H

#include <asm/cacheflush.h>
#include <asm/fixmap.h>
#include <asm/kmap_types.h>
#include <asm/pgtable.h>

#define PKMAP_BASE		(FIXADDR_START - PMD_SIZE)
#define LAST_PKMAP		PTRS_PER_PTE
#define LAST_PKMAP_MASK		(LAST_PKMAP - 1)
#define PKMAP_NR(virt)		(((virt) - PKMAP_BASE) >> PAGE_SHIFT)
#define PKMAP_ADDR(nr)		(PKMAP_BASE + ((nr) << PAGE_SHIFT))

#define kmap_prot		PAGE_KERNEL

extern pte_t *pkmap_page_table;

void *kmap_high(struct page *page);
void kunmap_high(struct page *page);

static inline void *kmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return page_address(page);
	return kmap_high(page);
}

static inline void kunmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}

static inline void flush_cache_kmaps(void)
{
	flush_cache_all();
}

void *kmap_atomic(struct page *page);
void __kunmap_atomic(void *kvaddr);

void kmap_init(void);

#endif
