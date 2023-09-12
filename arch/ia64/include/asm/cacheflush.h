/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_IA64_CACHEFLUSH_H
#define _ASM_IA64_CACHEFLUSH_H

/*
 * Copyright (C) 2002 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 */

#include <linux/page-flags.h>
#include <linux/bitops.h>

#include <asm/page.h>

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
static inline void flush_dcache_folio(struct folio *folio)
{
	clear_bit(PG_arch_1, &folio->flags);
}
#define flush_dcache_folio flush_dcache_folio

static inline void flush_dcache_page(struct page *page)
{
	flush_dcache_folio(page_folio(page));
}

extern void flush_icache_range(unsigned long start, unsigned long end);
#define flush_icache_range flush_icache_range
extern void clflush_cache_range(void *addr, int size);

#define flush_icache_user_page(vma, page, user_addr, len)					\
do {												\
	unsigned long _addr = (unsigned long) page_address(page) + ((user_addr) & ~PAGE_MASK);	\
	flush_icache_range(_addr, _addr + (len));						\
} while (0)

#include <asm-generic/cacheflush.h>

#endif /* _ASM_IA64_CACHEFLUSH_H */
