/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Cache flush operations for the Hexagon architecture
 *
 * Copyright (c) 2010-2011, The Linux Foundation. All rights reserved.
 */

#ifndef _ASM_CACHEFLUSH_H
#define _ASM_CACHEFLUSH_H

#include <linux/mm_types.h>

/* Cache flushing:
 *
 *  - flush_cache_all() flushes entire cache
 *  - flush_cache_mm(mm) flushes the specified mm context's cache lines
 *  - flush_cache_page(mm, vmaddr, pfn) flushes a single page
 *  - flush_cache_range(vma, start, end) flushes a range of pages
 *  - flush_icache_range(start, end) flush a range of instructions
 *  - flush_dcache_page(pg) flushes(wback&invalidates) a page for dcache
 *  - flush_icache_page(vma, pg) flushes(invalidates) a page for icache
 *
 *  Need to doublecheck which one is really needed for ptrace stuff to work.
 */
#define LINESIZE	32
#define LINEBITS	5

/*
 * Flush Dcache range through current map.
 */
extern void flush_dcache_range(unsigned long start, unsigned long end);
#define flush_dcache_range flush_dcache_range

/*
 * Flush Icache range through current map.
 */
extern void flush_icache_range(unsigned long start, unsigned long end);
#define flush_icache_range flush_icache_range

/*
 * Memory-management related flushes are there to ensure in non-physically
 * indexed cache schemes that stale lines belonging to a given ASID aren't
 * in the cache to confuse things.  The prototype Hexagon Virtual Machine
 * only uses a single ASID for all user-mode maps, which should
 * mean that they aren't necessary.  A brute-force, flush-everything
 * implementation, with the name xxxxx_hexagon() is present in
 * arch/hexagon/mm/cache.c, but let's not wire it up until we know
 * it is needed.
 */
extern void flush_cache_all_hexagon(void);

/*
 * This may or may not ever have to be non-null, depending on the
 * virtual machine MMU.  For a native kernel, it's definitiely  a no-op
 *
 * This is also the place where deferred cache coherency stuff seems
 * to happen, classically...  but instead we do it like ia64 and
 * clean the cache when the PTE is set.
 *
 */
static inline void update_mmu_cache_range(struct vm_fault *vmf,
		struct vm_area_struct *vma, unsigned long address,
		pte_t *ptep, unsigned int nr)
{
	/*  generic_ptrace_pokedata doesn't wind up here, does it?  */
}

#define update_mmu_cache(vma, addr, ptep) \
	update_mmu_cache_range(NULL, vma, addr, ptep, 1)

void copy_to_user_page(struct vm_area_struct *vma, struct page *page,
		       unsigned long vaddr, void *dst, void *src, int len);
#define copy_to_user_page copy_to_user_page

#define copy_from_user_page(vma, page, vaddr, dst, src, len) \
	memcpy(dst, src, len)

extern void hexagon_inv_dcache_range(unsigned long start, unsigned long end);
extern void hexagon_clean_dcache_range(unsigned long start, unsigned long end);

#include <asm-generic/cacheflush.h>

#endif
