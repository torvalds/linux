/*
 * Copyright (C) 2015 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#ifndef _ASM_HIGHMEM_H
#define _ASM_HIGHMEM_H

#ifdef CONFIG_HIGHMEM

#include <uapi/asm/page.h>
#include <asm/kmap_types.h>

/* start after vmalloc area */
#define FIXMAP_BASE		(PAGE_OFFSET - FIXMAP_SIZE - PKMAP_SIZE)
#define FIXMAP_SIZE		PGDIR_SIZE	/* only 1 PGD worth */
#define KM_TYPE_NR		((FIXMAP_SIZE >> PAGE_SHIFT)/NR_CPUS)
#define FIXMAP_ADDR(nr)		(FIXMAP_BASE + ((nr) << PAGE_SHIFT))

/* start after fixmap area */
#define PKMAP_BASE		(FIXMAP_BASE + FIXMAP_SIZE)
#define PKMAP_SIZE		PGDIR_SIZE
#define LAST_PKMAP		(PKMAP_SIZE >> PAGE_SHIFT)
#define LAST_PKMAP_MASK		(LAST_PKMAP - 1)
#define PKMAP_ADDR(nr)		(PKMAP_BASE + ((nr) << PAGE_SHIFT))
#define PKMAP_NR(virt)		(((virt) - PKMAP_BASE) >> PAGE_SHIFT)

#define kmap_prot		PAGE_KERNEL


#include <asm/cacheflush.h>

extern void *kmap(struct page *page);
extern void *kmap_high(struct page *page);
extern void *kmap_atomic(struct page *page);
extern void __kunmap_atomic(void *kvaddr);
extern void kunmap_high(struct page *page);

extern void kmap_init(void);

static inline void flush_cache_kmaps(void)
{
	flush_cache_all();
}

static inline void kunmap(struct page *page)
{
	BUG_ON(in_interrupt());
	if (!PageHighMem(page))
		return;
	kunmap_high(page);
}


#endif

#endif
