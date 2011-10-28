/* Functions for global i/dcache invalidation when caching in SMP
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
 * mn10300_icache_inv - Globally invalidate instruction cache
 *
 * Invalidate the instruction cache on all CPUs.
 */
void mn10300_icache_inv(void)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_icache_inv();
	smp_cache_call(SMP_ICACHE_INV, 0, 0);
	smp_unlock_cache(flags);
}

/**
 * mn10300_icache_inv_page - Globally invalidate a page of instruction cache
 * @start: The address of the page of memory to be invalidated.
 *
 * Invalidate a range of addresses in the instruction cache on all CPUs
 * covering the page that includes the given address.
 */
void mn10300_icache_inv_page(unsigned long start)
{
	unsigned long flags;

	start &= ~(PAGE_SIZE-1);

	flags = smp_lock_cache();
	mn10300_local_icache_inv_page(start);
	smp_cache_call(SMP_ICACHE_INV_RANGE, start, start + PAGE_SIZE);
	smp_unlock_cache(flags);
}

/**
 * mn10300_icache_inv_range - Globally invalidate range of instruction cache
 * @start: The start address of the region to be invalidated.
 * @end: The end address of the region to be invalidated.
 *
 * Invalidate a range of addresses in the instruction cache on all CPUs,
 * between start and end-1 inclusive.
 */
void mn10300_icache_inv_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_icache_inv_range(start, end);
	smp_cache_call(SMP_ICACHE_INV_RANGE, start, end);
	smp_unlock_cache(flags);
}

/**
 * mn10300_icache_inv_range2 - Globally invalidate range of instruction cache
 * @start: The start address of the region to be invalidated.
 * @size: The size of the region to be invalidated.
 *
 * Invalidate a range of addresses in the instruction cache on all CPUs,
 * between start and start+size-1 inclusive.
 */
void mn10300_icache_inv_range2(unsigned long start, unsigned long size)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_icache_inv_range2(start, size);
	smp_cache_call(SMP_ICACHE_INV_RANGE, start, start + size);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_inv - Globally invalidate data cache
 *
 * Invalidate the data cache on all CPUs.
 */
void mn10300_dcache_inv(void)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_inv();
	smp_cache_call(SMP_DCACHE_INV, 0, 0);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_inv_page - Globally invalidate a page of data cache
 * @start: The address of the page of memory to be invalidated.
 *
 * Invalidate a range of addresses in the data cache on all CPUs covering the
 * page that includes the given address.
 */
void mn10300_dcache_inv_page(unsigned long start)
{
	unsigned long flags;

	start &= ~(PAGE_SIZE-1);

	flags = smp_lock_cache();
	mn10300_local_dcache_inv_page(start);
	smp_cache_call(SMP_DCACHE_INV_RANGE, start, start + PAGE_SIZE);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_inv_range - Globally invalidate range of data cache
 * @start: The start address of the region to be invalidated.
 * @end: The end address of the region to be invalidated.
 *
 * Invalidate a range of addresses in the data cache on all CPUs, between start
 * and end-1 inclusive.
 */
void mn10300_dcache_inv_range(unsigned long start, unsigned long end)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_inv_range(start, end);
	smp_cache_call(SMP_DCACHE_INV_RANGE, start, end);
	smp_unlock_cache(flags);
}

/**
 * mn10300_dcache_inv_range2 - Globally invalidate range of data cache
 * @start: The start address of the region to be invalidated.
 * @size: The size of the region to be invalidated.
 *
 * Invalidate a range of addresses in the data cache on all CPUs, between start
 * and start+size-1 inclusive.
 */
void mn10300_dcache_inv_range2(unsigned long start, unsigned long size)
{
	unsigned long flags;

	flags = smp_lock_cache();
	mn10300_local_dcache_inv_range2(start, size);
	smp_cache_call(SMP_DCACHE_INV_RANGE, start, start + size);
	smp_unlock_cache(flags);
}
