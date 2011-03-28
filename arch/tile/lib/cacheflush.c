/*
 * Copyright 2010 Tilera Corporation. All Rights Reserved.
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation, version 2.
 *
 *   This program is distributed in the hope that it will be useful, but
 *   WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 *   NON INFRINGEMENT.  See the GNU General Public License for
 *   more details.
 */

#include <asm/page.h>
#include <asm/cacheflush.h>
#include <arch/icache.h>


void __flush_icache_range(unsigned long start, unsigned long end)
{
	invalidate_icache((const void *)start, end - start, PAGE_SIZE);
}


/* Force a load instruction to issue. */
static inline void force_load(char *p)
{
	*(volatile char *)p;
}

/*
 * Flush and invalidate a VA range that is homed remotely on a single
 * core (if "!hfh") or homed via hash-for-home (if "hfh"), waiting
 * until the memory controller holds the flushed values.
 */
void finv_buffer_remote(void *buffer, size_t size, int hfh)
{
	char *p, *base;
	size_t step_size, load_count;
	const unsigned long STRIPE_WIDTH = 8192;

	/*
	 * Flush and invalidate the buffer out of the local L1/L2
	 * and request the home cache to flush and invalidate as well.
	 */
	__finv_buffer(buffer, size);

	/*
	 * Wait for the home cache to acknowledge that it has processed
	 * all the flush-and-invalidate requests.  This does not mean
	 * that the flushed data has reached the memory controller yet,
	 * but it does mean the home cache is processing the flushes.
	 */
	__insn_mf();

	/*
	 * Issue a load to the last cache line, which can't complete
	 * until all the previously-issued flushes to the same memory
	 * controller have also completed.  If we weren't striping
	 * memory, that one load would be sufficient, but since we may
	 * be, we also need to back up to the last load issued to
	 * another memory controller, which would be the point where
	 * we crossed an 8KB boundary (the granularity of striping
	 * across memory controllers).  Keep backing up and doing this
	 * until we are before the beginning of the buffer, or have
	 * hit all the controllers.
	 *
	 * If we are flushing a hash-for-home buffer, it's even worse.
	 * Each line may be homed on a different tile, and each tile
	 * may have up to four lines that are on different
	 * controllers.  So as we walk backwards, we have to touch
	 * enough cache lines to satisfy these constraints.  In
	 * practice this ends up being close enough to "load from
	 * every cache line on a full memory stripe on each
	 * controller" that we simply do that, to simplify the logic.
	 *
	 * FIXME: See bug 9535 for some issues with this code.
	 */
	if (hfh) {
		step_size = L2_CACHE_BYTES;
		load_count = (STRIPE_WIDTH / L2_CACHE_BYTES) *
			      (1 << CHIP_LOG_NUM_MSHIMS());
	} else {
		step_size = STRIPE_WIDTH;
		load_count = (1 << CHIP_LOG_NUM_MSHIMS());
	}

	/* Load the last byte of the buffer. */
	p = (char *)buffer + size - 1;
	force_load(p);

	/* Bump down to the end of the previous stripe or cache line. */
	p -= step_size;
	p = (char *)((unsigned long)p | (step_size - 1));

	/* Figure out how far back we need to go. */
	base = p - (step_size * (load_count - 2));
	if ((long)base < (long)buffer)
		base = buffer;

	/*
	 * Fire all the loads we need.  The MAF only has eight entries
	 * so we can have at most eight outstanding loads, so we
	 * unroll by that amount.
	 */
#pragma unroll 8
	for (; p >= base; p -= step_size)
		force_load(p);

	/*
	 * Repeat, but with inv's instead of loads, to get rid of the
	 * data we just loaded into our own cache and the old home L3.
	 * No need to unroll since inv's don't target a register.
	 */
	p = (char *)buffer + size - 1;
	__insn_inv(p);
	p -= step_size;
	p = (char *)((unsigned long)p | (step_size - 1));
	for (; p >= base; p -= step_size)
		__insn_inv(p);

	/* Wait for the load+inv's (and thus finvs) to have completed. */
	__insn_mf();
}
