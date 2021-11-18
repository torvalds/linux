// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/m68k/mm/memory.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/module.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/gfp.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/traps.h>
#include <asm/machdep.h>


/* invalidate page in both caches */
static inline void clear040(unsigned long paddr)
{
	asm volatile (
		"nop\n\t"
		".chip 68040\n\t"
		"cinvp %%bc,(%0)\n\t"
		".chip 68k"
		: : "a" (paddr));
}

/* invalidate page in i-cache */
static inline void cleari040(unsigned long paddr)
{
	asm volatile (
		"nop\n\t"
		".chip 68040\n\t"
		"cinvp %%ic,(%0)\n\t"
		".chip 68k"
		: : "a" (paddr));
}

/* push page in both caches */
/* RZ: cpush %bc DOES invalidate %ic, regardless of DPI */
static inline void push040(unsigned long paddr)
{
	asm volatile (
		"nop\n\t"
		".chip 68040\n\t"
		"cpushp %%bc,(%0)\n\t"
		".chip 68k"
		: : "a" (paddr));
}

/* push and invalidate page in both caches, must disable ints
 * to avoid invalidating valid data */
static inline void pushcl040(unsigned long paddr)
{
	unsigned long flags;

	local_irq_save(flags);
	push040(paddr);
	if (CPU_IS_060)
		clear040(paddr);
	local_irq_restore(flags);
}

/*
 * 040: Hit every page containing an address in the range paddr..paddr+len-1.
 * (Low order bits of the ea of a CINVP/CPUSHP are "don't care"s).
 * Hit every page until there is a page or less to go. Hit the next page,
 * and the one after that if the range hits it.
 */
/* ++roman: A little bit more care is required here: The CINVP instruction
 * invalidates cache entries WITHOUT WRITING DIRTY DATA BACK! So the beginning
 * and the end of the region must be treated differently if they are not
 * exactly at the beginning or end of a page boundary. Else, maybe too much
 * data becomes invalidated and thus lost forever. CPUSHP does what we need:
 * it invalidates the page after pushing dirty data to memory. (Thanks to Jes
 * for discovering the problem!)
 */
/* ... but on the '060, CPUSH doesn't invalidate (for us, since we have set
 * the DPI bit in the CACR; would it cause problems with temporarily changing
 * this?). So we have to push first and then additionally to invalidate.
 */


/*
 * cache_clear() semantics: Clear any cache entries for the area in question,
 * without writing back dirty entries first. This is useful if the data will
 * be overwritten anyway, e.g. by DMA to memory. The range is defined by a
 * _physical_ address.
 */

void cache_clear (unsigned long paddr, int len)
{
    if (CPU_IS_COLDFIRE) {
	clear_cf_bcache(0, DCACHE_MAX_ADDR);
    } else if (CPU_IS_040_OR_060) {
	int tmp;

	/*
	 * We need special treatment for the first page, in case it
	 * is not page-aligned. Page align the addresses to work
	 * around bug I17 in the 68060.
	 */
	if ((tmp = -paddr & (PAGE_SIZE - 1))) {
	    pushcl040(paddr & PAGE_MASK);
	    if ((len -= tmp) <= 0)
		return;
	    paddr += tmp;
	}
	tmp = PAGE_SIZE;
	paddr &= PAGE_MASK;
	while ((len -= tmp) >= 0) {
	    clear040(paddr);
	    paddr += tmp;
	}
	if ((len += tmp))
	    /* a page boundary gets crossed at the end */
	    pushcl040(paddr);
    }
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I_AND_D)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
	mach_l2_flush(0);
#endif
}
EXPORT_SYMBOL(cache_clear);


/*
 * cache_push() semantics: Write back any dirty cache data in the given area,
 * and invalidate the range in the instruction cache. It needs not (but may)
 * invalidate those entries also in the data cache. The range is defined by a
 * _physical_ address.
 */

void cache_push (unsigned long paddr, int len)
{
    if (CPU_IS_COLDFIRE) {
	flush_cf_bcache(0, DCACHE_MAX_ADDR);
    } else if (CPU_IS_040_OR_060) {
	int tmp = PAGE_SIZE;

	/*
         * on 68040 or 68060, push cache lines for pages in the range;
	 * on the '040 this also invalidates the pushed lines, but not on
	 * the '060!
	 */
	len += paddr & (PAGE_SIZE - 1);

	/*
	 * Work around bug I17 in the 68060 affecting some instruction
	 * lines not being invalidated properly.
	 */
	paddr &= PAGE_MASK;

	do {
	    push040(paddr);
	    paddr += tmp;
	} while ((len -= tmp) > 0);
    }
    /*
     * 68030/68020 have no writeback cache. On the other hand,
     * cache_push is actually a superset of cache_clear (the lines
     * get written back and invalidated), so we should make sure
     * to perform the corresponding actions. After all, this is getting
     * called in places where we've just loaded code, or whatever, so
     * flushing the icache is appropriate; flushing the dcache shouldn't
     * be required.
     */
    else /* 68030 or 68020 */
	asm volatile ("movec %/cacr,%/d0\n\t"
		      "oriw %0,%/d0\n\t"
		      "movec %/d0,%/cacr"
		      : : "i" (FLUSH_I)
		      : "d0");
#ifdef CONFIG_M68K_L2_CACHE
    if(mach_l2_flush)
	mach_l2_flush(1);
#endif
}
EXPORT_SYMBOL(cache_push);

