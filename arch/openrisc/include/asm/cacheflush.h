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

/*
 * Other interfaces are not required since we do not have virtually
 * indexed or tagged caches. So we can use the default here.
 */
#define flush_cache_all()				do { } while (0)
#define flush_cache_mm(mm)				do { } while (0)
#define flush_cache_dup_mm(mm)				do { } while (0)
#define flush_cache_range(vma, start, end)		do { } while (0)
#define flush_cache_page(vma, vmaddr, pfn)		do { } while (0)
#define flush_dcache_mmap_lock(mapping)			do { } while (0)
#define flush_dcache_mmap_unlock(mapping)		do { } while (0)
#define flush_icache_range(start, end)			do { } while (0)
#define flush_icache_page(vma, pg)			do { } while (0)
#define flush_icache_user_range(vma, pg, adr, len)	do { } while (0)
#define flush_cache_vmap(start, end)			do { } while (0)
#define flush_cache_vunmap(start, end)			do { } while (0)

#define copy_to_user_page(vma, page, vaddr, dst, src, len)           \
	do {                                                         \
		memcpy(dst, src, len);                               \
		if (vma->vm_flags & VM_EXEC)                         \
			sync_icache_dcache(page);                    \
	} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)         \
	memcpy(dst, src, len)

#endif /* __ASM_CACHEFLUSH_H */
