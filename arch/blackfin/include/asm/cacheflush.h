/*
 * Blackfin low-level cache routines
 *
 * Copyright 2004-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#ifndef _BLACKFIN_CACHEFLUSH_H
#define _BLACKFIN_CACHEFLUSH_H

#include <asm/blackfin.h>	/* for SSYNC() */

extern void blackfin_icache_flush_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_dcache_flush_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_dcache_invalidate_range(unsigned long start_address, unsigned long end_address);
extern void blackfin_dflush_page(void *page);
extern void blackfin_invalidate_entire_dcache(void);
extern void blackfin_invalidate_entire_icache(void);

#define flush_dcache_mmap_lock(mapping)		do { } while (0)
#define flush_dcache_mmap_unlock(mapping)	do { } while (0)
#define flush_cache_mm(mm)			do { } while (0)
#define flush_cache_range(vma, start, end)	do { } while (0)
#define flush_cache_page(vma, vmaddr)		do { } while (0)
#define flush_cache_vmap(start, end)		do { } while (0)
#define flush_cache_vunmap(start, end)		do { } while (0)

#ifdef CONFIG_SMP
#define flush_icache_range_others(start, end)	\
	smp_icache_flush_range_others((start), (end))
#else
#define flush_icache_range_others(start, end)	do { } while (0)
#endif

static inline void flush_icache_range(unsigned start, unsigned end)
{
#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK)
	blackfin_dcache_flush_range(start, end);
#endif

	/* Make sure all write buffers in the data side of the core
	 * are flushed before trying to invalidate the icache.  This
	 * needs to be after the data flush and before the icache
	 * flush so that the SSYNC does the right thing in preventing
	 * the instruction prefetcher from hitting things in cached
	 * memory at the wrong time -- it runs much further ahead than
	 * the pipeline.
	 */
	SSYNC();
#if defined(CONFIG_BFIN_ICACHE)
	blackfin_icache_flush_range(start, end);
	flush_icache_range_others(start, end);
#endif
}

#define copy_to_user_page(vma, page, vaddr, dst, src, len)		\
do { memcpy(dst, src, len);						\
     flush_icache_range((unsigned) (dst), (unsigned) (dst) + (len));	\
} while (0)

#define copy_from_user_page(vma, page, vaddr, dst, src, len)	memcpy(dst, src, len)

#if defined(CONFIG_BFIN_DCACHE)
# define invalidate_dcache_range(start,end)	blackfin_dcache_invalidate_range((start), (end))
#else
# define invalidate_dcache_range(start,end)	do { } while (0)
#endif
#if defined(CONFIG_BFIN_EXTMEM_WRITEBACK) || defined(CONFIG_BFIN_L2_WRITEBACK)
# define flush_dcache_range(start,end)		blackfin_dcache_flush_range((start), (end))
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 1
# define flush_dcache_page(page)		blackfin_dflush_page(page_address(page))
#else
# define flush_dcache_range(start,end)		do { } while (0)
#define ARCH_IMPLEMENTS_FLUSH_DCACHE_PAGE 0
# define flush_dcache_page(page)		do { } while (0)
#endif

extern unsigned long reserved_mem_dcache_on;
extern unsigned long reserved_mem_icache_on;

static inline int bfin_addr_dcacheable(unsigned long addr)
{
#ifdef CONFIG_BFIN_EXTMEM_DCACHEABLE
	if (addr < (_ramend - DMA_UNCACHED_REGION))
		return 1;
#endif

	if (reserved_mem_dcache_on &&
		addr >= _ramend && addr < physical_mem_end)
		return 1;

#ifdef CONFIG_BFIN_L2_DCACHEABLE
	if (addr >= L2_START && addr < L2_START + L2_LENGTH)
		return 1;
#endif

	return 0;
}

#endif				/* _BLACKFIN_ICACHEFLUSH_H */
