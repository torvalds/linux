/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 */
#ifndef _ASM_POWERPC_CACHEFLUSH_H
#define _ASM_POWERPC_CACHEFLUSH_H

#include <linux/mm.h>
#include <asm/cputable.h>
#include <asm/cpu_has_feature.h>

/*
 * This flag is used to indicate that the page pointed to by a pte is clean
 * and does not require cleaning before returning it to the user.
 */
#define PG_dcache_clean PG_arch_1

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * Book3s has no ptesync after setting a pte, so without this ptesync it's
 * possible for a kernel virtual mapping access to return a spurious fault
 * if it's accessed right after the pte is set. The page fault handler does
 * not expect this type of fault. flush_cache_vmap is not exactly the right
 * place to put this, but it seems to work well enough.
 */
static inline void flush_cache_vmap(unsigned long start, unsigned long end)
{
	asm volatile("ptesync" ::: "memory");
}
#define flush_cache_vmap flush_cache_vmap
#endif /* CONFIG_PPC_BOOK3S_64 */

#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
/*
 * This is called when a page has been modified by the kernel.
 * It just marks the page as not i-cache clean.  We do the i-cache
 * flush later when the page is given to a user process, if necessary.
 */
static inline void flush_dcache_folio(struct folio *folio)
{
	if (cpu_has_feature(CPU_FTR_COHERENT_ICACHE))
		return;
	/* avoid an atomic op if possible */
	if (test_bit(PG_dcache_clean, &folio->flags))
		clear_bit(PG_dcache_clean, &folio->flags);
}
#define flush_dcache_folio flush_dcache_folio

static inline void flush_dcache_page(struct page *page)
{
	flush_dcache_folio(page_folio(page));
}

void flush_icache_range(unsigned long start, unsigned long stop);
#define flush_icache_range flush_icache_range

void flush_icache_user_page(struct vm_area_struct *vma, struct page *page,
		unsigned long addr, int len);
#define flush_icache_user_page flush_icache_user_page

void flush_dcache_icache_folio(struct folio *folio);

/**
 * flush_dcache_range(): Write any modified data cache blocks out to memory and
 * invalidate them. Does not invalidate the corresponding instruction cache
 * blocks.
 *
 * @start: the start address
 * @stop: the stop address (exclusive)
 */
static inline void flush_dcache_range(unsigned long start, unsigned long stop)
{
	unsigned long shift = l1_dcache_shift();
	unsigned long bytes = l1_dcache_bytes();
	void *addr = (void *)(start & ~(bytes - 1));
	unsigned long size = stop - (unsigned long)addr + (bytes - 1);
	unsigned long i;

	if (IS_ENABLED(CONFIG_PPC64))
		mb();	/* sync */

	for (i = 0; i < size >> shift; i++, addr += bytes)
		dcbf(addr);
	mb();	/* sync */

}

/*
 * Write any modified data cache blocks out to memory.
 * Does not invalidate the corresponding cache lines (especially for
 * any corresponding instruction cache).
 */
static inline void clean_dcache_range(unsigned long start, unsigned long stop)
{
	unsigned long shift = l1_dcache_shift();
	unsigned long bytes = l1_dcache_bytes();
	void *addr = (void *)(start & ~(bytes - 1));
	unsigned long size = stop - (unsigned long)addr + (bytes - 1);
	unsigned long i;

	for (i = 0; i < size >> shift; i++, addr += bytes)
		dcbst(addr);
	mb();	/* sync */
}

/*
 * Like above, but invalidate the D-cache.  This is used by the 8xx
 * to invalidate the cache so the PPC core doesn't get stale data
 * from the CPM (no cache snooping here :-).
 */
static inline void invalidate_dcache_range(unsigned long start,
					   unsigned long stop)
{
	unsigned long shift = l1_dcache_shift();
	unsigned long bytes = l1_dcache_bytes();
	void *addr = (void *)(start & ~(bytes - 1));
	unsigned long size = stop - (unsigned long)addr + (bytes - 1);
	unsigned long i;

	for (i = 0; i < size >> shift; i++, addr += bytes)
		dcbi(addr);
	mb();	/* sync */
}

#ifdef CONFIG_44x
static inline void flush_instruction_cache(void)
{
	iccci((void *)KERNELBASE);
	isync();
}
#else
void flush_instruction_cache(void);
#endif

#include <asm-generic/cacheflush.h>

#endif /* _ASM_POWERPC_CACHEFLUSH_H */
