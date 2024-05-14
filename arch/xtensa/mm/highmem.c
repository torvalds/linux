/*
 * High memory support for Xtensa architecture
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file "COPYING" in the main directory of
 * this archive for more details.
 *
 * Copyright (C) 2014 Cadence Design Systems Inc.
 */

#include <linux/export.h>
#include <linux/highmem.h>
#include <asm/tlbflush.h>

#if DCACHE_WAY_SIZE > PAGE_SIZE
unsigned int last_pkmap_nr_arr[DCACHE_N_COLORS];
wait_queue_head_t pkmap_map_wait_arr[DCACHE_N_COLORS];

static void __init kmap_waitqueues_init(void)
{
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(pkmap_map_wait_arr); ++i)
		init_waitqueue_head(pkmap_map_wait_arr + i);
}

static inline enum fixed_addresses kmap_idx(int type, unsigned long color)
{
	int idx = (type + KM_MAX_IDX * smp_processor_id()) * DCACHE_N_COLORS;

	/*
	 * The fixmap operates top down, so the color offset needs to be
	 * reverse as well.
	 */
	return idx + DCACHE_N_COLORS - 1 - color;
}

enum fixed_addresses kmap_local_map_idx(int type, unsigned long pfn)
{
	return kmap_idx(type, DCACHE_ALIAS(pfn << PAGE_SHIFT));
}

enum fixed_addresses kmap_local_unmap_idx(int type, unsigned long addr)
{
	return kmap_idx(type, DCACHE_ALIAS(addr));
}

#else
static inline void kmap_waitqueues_init(void) { }
#endif

void __init kmap_init(void)
{
	/* Check if this memory layout is broken because PKMAP overlaps
	 * page table.
	 */
	BUILD_BUG_ON(PKMAP_BASE < TLBTEMP_BASE_1 + TLBTEMP_SIZE);
	kmap_waitqueues_init();
}
