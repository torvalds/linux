/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _M68KNOMMU_CACHEFLUSH_H
#define _M68KNOMMU_CACHEFLUSH_H

/*
 * (C) Copyright 2000-2010, Greg Ungerer <gerg@snapgear.com>
 */
#include <linux/mm.h>
#include <asm/mcfsim.h>

#define flush_cache_all()			__flush_cache_all()
#define flush_dcache_range(start, len)		__flush_dcache_all()
#define flush_icache_range(start, len)		__flush_icache_all()

void mcf_cache_push(void);

static inline void __clear_cache_all(void)
{
#ifdef CACHE_INVALIDATE
	__asm__ __volatile__ (
		"movec	%0, %%CACR\n\t"
		"nop\n\t"
		: : "r" (CACHE_INVALIDATE) );
#endif
}

static inline void __flush_cache_all(void)
{
#ifdef CACHE_PUSH
	mcf_cache_push();
#endif
	__clear_cache_all();
}

/*
 * Some ColdFire parts implement separate instruction and data caches,
 * on those we should just flush the appropriate cache. If we don't need
 * to do any specific flushing then this will be optimized away.
 */
static inline void __flush_icache_all(void)
{
#ifdef CACHE_INVALIDATEI
	__asm__ __volatile__ (
		"movec	%0, %%CACR\n\t"
		"nop\n\t"
		: : "r" (CACHE_INVALIDATEI) );
#endif
}

static inline void __flush_dcache_all(void)
{
#ifdef CACHE_PUSH
	mcf_cache_push();
#endif
#ifdef CACHE_INVALIDATED
	__asm__ __volatile__ (
		"movec	%0, %%CACR\n\t"
		"nop\n\t"
		: : "r" (CACHE_INVALIDATED) );
#else
	/* Flush the write buffer */
	__asm__ __volatile__ ( "nop" );
#endif
}

/*
 * Push cache entries at supplied address. We want to write back any dirty
 * data and then invalidate the cache lines associated with this address.
 */
static inline void cache_push(unsigned long paddr, int len)
{
	__flush_cache_all();
}

/*
 * Clear cache entries at supplied address (that is don't write back any
 * dirty data).
 */
static inline void cache_clear(unsigned long paddr, int len)
{
	__clear_cache_all();
}

#include <asm-generic/cacheflush.h>

#endif /* _M68KNOMMU_CACHEFLUSH_H */
