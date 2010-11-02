/* Functions for global dcache flush when writeback caching in SMP
 *
 * Copyright (C) 2010 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */
#include <linux/mm.h>
#include <asm/cacheflush.h>
#include "cache-smp.h"

/**
 * mn10300_dcache_flush - Globally flush data cache
 *
 * Flush the data cache on all CPUs.
 */
void mn10300_dcache_flush(void)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_flush();
	smp_cache_call(SMP_DCACHE_FLUSH, 0, 0);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_page - Globally flush a page of data cache
 * @start: The address of the page of memory to be flushed.
 *
 * Flush a range of addresses in the data cache on all CPUs covering
 * the page that includes the given address.
 */
void mn10300_dcache_flush_page(unsigned long start)
{
	unsigned long flags;

	start &= ~(PAGE_SIZE-1);

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_page(start);
	smp_cache_call(SMP_DCACHE_FLUSH_RANGE, start, start + PAGE_SIZE);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_range - Globally flush range of data cache
 * @start: The start address of the region to be flushed.
 * @end: The end address of the region to be flushed.
 *
 * Flush a range of addresses in the data cache on all CPUs, between start and
 * end-1 inclusive.
 */
void mn10300_dcache_flush_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_range(start, end);
	smp_cache_call(SMP_DCACHE_FLUSH_RANGE, start, end);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_range2 - Globally flush range of data cache
 * @start: The start address of the region to be flushed.
 * @size: The size of the region to be flushed.
 *
 * Flush a range of addresses in the data cache on all CPUs, between start and
 * start+size-1 inclusive.
 */
void mn10300_dcache_flush_range2(unsigned long start, unsigned long size)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_range2(start, size);
	smp_cache_call(SMP_DCACHE_FLUSH_RANGE, start, start + size);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_inv - Globally flush and invalidate data cache
 *
 * Flush and invalidate the data cache on all CPUs.
 */
void mn10300_dcache_flush_inv(void)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_inv();
	smp_cache_call(SMP_DCACHE_FLUSH_INV, 0, 0);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_inv_page - Globally flush and invalidate a page of data
 *	cache
 * @start: The address of the page of memory to be flushed and invalidated.
 *
 * Flush and invalidate a range of addresses in the data cache on all CPUs
 * covering the page that includes the given address.
 */
void mn10300_dcache_flush_inv_page(unsigned long start)
{
	unsigned long flags;

	start &= ~(PAGE_SIZE-1);

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_inv_page(start);
	smp_cache_call(SMP_DCACHE_FLUSH_INV_RANGE, start, start + PAGE_SIZE);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_inv_range - Globally flush and invalidate range of data
 *	cache
 * @start: The start address of the region to be flushed and invalidated.
 * @end: The end address of the region to be flushed and invalidated.
 *
 * Flush and invalidate a range of addresses in the data cache on all CPUs,
 * between start and end-1 inclusive.
 */
void mn10300_dcache_flush_inv_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_inv_range(start, end);
	smp_cache_call(SMP_DCACHE_FLUSH_INV_RANGE, start, end);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_flush_inv_range2 - Globally flush and invalidate range of data
 *	cache
 * @start: The start address of the region to be flushed and invalidated.
 * @size: The size of the region to be flushed and invalidated.
 *
 * Flush and invalidate a range of addresses in the data cache on all CPUs,
 * between start and start+size-1 inclusive.
 */
void mn10300_dcache_flush_inv_range2(unsigned long start, unsigned long size)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_flush_inv_range2(start, size);
	smp_cache_call(SMP_DCACHE_FLUSH_INV_RANGE, start, start + size);
	smp_unlock_cache(flags);
}
