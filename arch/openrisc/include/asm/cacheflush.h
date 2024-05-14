/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * OpenRISC Linux
 *
 * Linux architectural port borrowing liberally from similar works of
 * others.  All original copyrights apply as per the original source
 * declaration.
 *
 * OpenRISC implementation:
 * Copyright (C) Jan Henrik Weinstock <jan.weinstock@rwth-aachen.de>
 * et al.
 */

#ifndef __ASM_CACHEFLUSH_H
#define __ASM_CACHEFLUSH_H

#include <linux/mm.h>

/*
 * Helper function for flushing or invalidating entire pages from data
 * and instruction caches. SMP needs a little extra work, since we need
 * to flush the pages on all cpus.
 */
extern void local_dcache_page_flush(struct page *page);
extern void local_icache_page_inv(struct page *page);

/*
 * Data cache flushing always happen on the local cpu. Instruction cache
 * invalidations need to be broadcasted to all other cpu in the system in
 * case of SMP configurations.
 */
#ifndef CONFIG_SMP
#define dcache_page_flush(page)      local_dcache_page_flush(page)
#define icache_page_inv(page)        local_icache_page_inv(page)
#else  /* CONFIG_SMP */
#define dcache_page_flush(page)      local_dcache_page_flush(page)
#define icache_page_inv(page)        smp_icache_page_inv(page)
extern void smp_icache_page_inv(struct page *page);
#endif /* CONFIG_SMP */

/*
 * Synchronizes caches. Whenever a cpu writes executable code to memory, this
 * should be called to make sure the processor sees the newly written code.
 */
static inline void sync_icache_dcache(struct page *page)
{
	if (!IS_ENABLED(CONFIG_DCACHE_WRITETHROUGH))
		dcache_page_flush(page);
	icache_page_inv(page);
}

/*
 * Pages with this bit set need not be flushed/invalidated, since
 * they have not changed since last flush. New pages start with
 * PG_arch_1 not set and are therefore dirty by default.
 */
#define PG_dc_clean                  PG_arch_1

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
static inline void flush_dcache_page(struct page *page)
{
	clear_bit(PG_dc_clean, &page->flags);
}

#define flush_icache_user_page(vma, page, addr, len)	\
do {							\
	if (vma->vm_flags & VM_EXEC)			\
		sync_icache_dcache(page);		\
} while (0)

#include <asm-generic/cacheflush.h>

#endif /* __ASM_CACHEFLUSH_H */
