// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2001-2008 Silicon Graphics, Inc.  All rights reserved.
 *
 * A simple uncached page allocator using the generic allocator. This
 * allocator first utilizes the spare (spill) pages found in the EFI
 * memmap and will then start converting cached pages to uncached ones
 * at a granule at a time. Node awareness is implemented by having a
 * pool of pages per node.
 */

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/efi.h>
#include <linux/nmi.h>
#include <linux/genalloc.h>
#include <linux/gfp.h>
#include <linux/pgtable.h>
#include <asm/efi.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <linux/atomic.h>
#include <asm/tlbflush.h>

struct uncached_pool {
	struct gen_pool *pool;
	struct mutex add_chunk_mutex;	/* serialize adding a converted chunk */
	int nchunks_added;		/* #of converted chunks added to pool */
	atomic_t status;		/* smp called function's return status*/
};

#define MAX_CONVERTED_CHUNKS_PER_NODE	2

struct uncached_pool uncached_pools[MAX_NUMNODES];


static void uncached_ipi_visibility(void *data)
{
	int status;
	struct uncached_pool *uc_pool = (struct uncached_pool *)data;

	status = ia64_pal_prefetch_visibility(PAL_VISIBILITY_PHYSICAL);
	if ((status != PAL_VISIBILITY_OK) &&
	    (status != PAL_VISIBILITY_OK_REMOTE_NEEDED))
		atomic_inc(&uc_pool->status);
}


static void uncached_ipi_mc_drain(void *data)
{
	int status;
	struct uncached_pool *uc_pool = (struct uncached_pool *)data;

	status = ia64_pal_mc_drain();
	if (status != PAL_STATUS_SUCCESS)
		atomic_inc(&uc_pool->status);
}


/*
 * Add a new chunk of uncached memory pages to the specified pool.
 *
 * @pool: pool to add new chunk of uncached memory to
 * @nid: node id of node to allocate memory from, or -1
 *
 * This is accomplished by first allocating a granule of cached memory pages
 * and then converting them to uncached memory pages.
 */
static int uncached_add_chunk(struct uncached_pool *uc_pool, int nid)
{
	struct page *page;
	int status, i, nchunks_added = uc_pool->nchunks_added;
	unsigned long c_addr, uc_addr;

	if (mutex_lock_interruptible(&uc_pool->add_chunk_mutex) != 0)
		return -1;	/* interrupted by a signal */

	if (uc_pool->nchunks_added > nchunks_added) {
		/* someone added a new chunk while we were waiting */
		mutex_unlock(&uc_pool->add_chunk_mutex);
		return 0;
	}

	if (uc_pool->nchunks_added >= MAX_CONVERTED_CHUNKS_PER_NODE) {
		mutex_unlock(&uc_pool->add_chunk_mutex);
		return -1;
	}

	/* attempt to allocate a granule's worth of cached memory pages */

	page = __alloc_pages_node(nid,
				GFP_KERNEL | __GFP_ZERO | __GFP_THISNODE,
				IA64_GRANULE_SHIFT-PAGE_SHIFT);
	if (!page) {
		mutex_unlock(&uc_pool->add_chunk_mutex);
		return -1;
	}

	/* convert the memory pages from cached to uncached */

	c_addr = (unsigned long)page_address(page);
	uc_addr = c_addr - PAGE_OFFSET + __IA64_UNCACHED_OFFSET;

	/*
	 * There's a small race here where it's possible for someone to
	 * access the page through /dev/mem halfway through the conversion
	 * to uncached - not sure it's really worth bothering about
	 */
	for (i = 0; i < (IA64_GRANULE_SIZE / PAGE_SIZE); i++)
		SetPageUncached(&page[i]);

	flush_tlb_kernel_range(uc_addr, uc_addr + IA64_GRANULE_SIZE);

	status = ia64_pal_prefetch_visibility(PAL_VISIBILITY_PHYSICAL);
	if (status == PAL_VISIBILITY_OK_REMOTE_NEEDED) {
		atomic_set(&uc_pool->status, 0);
		smp_call_function(uncached_ipi_visibility, uc_pool, 1);
		if (atomic_read(&uc_pool->status))
			goto failed;
	} else if (status != PAL_VISIBILITY_OK)
		goto failed;

	preempt_disable();

	flush_icache_range(uc_addr, uc_addr + IA64_GRANULE_SIZE);

	/* flush the just introduced uncached translation from the TLB */
	local_flush_tlb_all();

	preempt_enable();

	status = ia64_pal_mc_drain();
	if (status != PAL_STATUS_SUCCESS)
		goto failed;
	atomic_set(&uc_pool->status, 0);
	smp_call_function(uncached_ipi_mc_drain, uc_pool, 1);
	if (atomic_read(&uc_pool->status))
		goto failed;

	/*
	 * The chunk of memory pages has been converted to uncached so now we
	 * can add it to the pool.
	 */
	status = gen_pool_add(uc_pool->pool, uc_addr, IA64_GRANULE_SIZE, nid);
	if (status)
		goto failed;

	uc_pool->nchunks_added++;
	mutex_unlock(&uc_pool->add_chunk_mutex);
	return 0;

	/* failed to convert or add the chunk so give it back to the kernel */
failed:
	for (i = 0; i < (IA64_GRANULE_SIZE / PAGE_SIZE); i++)
		ClearPageUncached(&page[i]);

	free_pages(c_addr, IA64_GRANULE_SHIFT-PAGE_SHIFT);
	mutex_unlock(&uc_pool->add_chunk_mutex);
	return -1;
}


/*
 * uncached_alloc_page
 *
 * @starting_nid: node id of node to start with, or -1
 * @n_pages: number of contiguous pages to allocate
 *
 * Allocate the specified number of contiguous uncached pages on the
 * requested node. If not enough contiguous uncached pages are available
 * on the requested node, roundrobin starting with the next higher node.
 */
unsigned long uncached_alloc_page(int starting_nid, int n_pages)
{
	unsigned long uc_addr;
	struct uncached_pool *uc_pool;
	int nid;

	if (unlikely(starting_nid >= MAX_NUMNODES))
		return 0;

	if (starting_nid < 0)
		starting_nid = numa_node_id();
	nid = starting_nid;

	do {
		if (!node_state(nid, N_HIGH_MEMORY))
			continue;
		uc_pool = &uncached_pools[nid];
		if (uc_pool->pool == NULL)
			continue;
		do {
			uc_addr = gen_pool_alloc(uc_pool->pool,
						 n_pages * PAGE_SIZE);
			if (uc_addr != 0)
				return uc_addr;
		} while (uncached_add_chunk(uc_pool, nid) == 0);

	} while ((nid = (nid + 1) % MAX_NUMNODES) != starting_nid);

	return 0;
}
EXPORT_SYMBOL(uncached_alloc_page);


/*
 * uncached_free_page
 *
 * @uc_addr: uncached address of first page to free
 * @n_pages: number of contiguous pages to free
 *
 * Free the specified number of uncached pages.
 */
void uncached_free_page(unsigned long uc_addr, int n_pages)
{
	int nid = paddr_to_nid(uc_addr - __IA64_UNCACHED_OFFSET);
	struct gen_pool *pool = uncached_pools[nid].pool;

	if (unlikely(pool == NULL))
		return;

	if ((uc_addr & (0XFUL << 60)) != __IA64_UNCACHED_OFFSET)
		panic("uncached_free_page invalid address %lx\n", uc_addr);

	gen_pool_free(pool, uc_addr, n_pages * PAGE_SIZE);
}
EXPORT_SYMBOL(uncached_free_page);


/*
 * uncached_build_memmap,
 *
 * @uc_start: uncached starting address of a chunk of uncached memory
 * @uc_end: uncached ending address of a chunk of uncached memory
 * @arg: ignored, (NULL argument passed in on call to efi_memmap_walk_uc())
 *
 * Called at boot time to build a map of pages that can be used for
 * memory special operations.
 */
static int __init uncached_build_memmap(u64 uc_start, u64 uc_end, void *arg)
{
	int nid = paddr_to_nid(uc_start - __IA64_UNCACHED_OFFSET);
	struct gen_pool *pool = uncached_pools[nid].pool;
	size_t size = uc_end - uc_start;

	touch_softlockup_watchdog();

	if (pool != NULL) {
		memset((char *)uc_start, 0, size);
		(void) gen_pool_add(pool, uc_start, size, nid);
	}
	return 0;
}


static int __init uncached_init(void)
{
	int nid;

	for_each_online_node(nid) {
		uncached_pools[nid].pool = gen_pool_create(PAGE_SHIFT, nid);
		mutex_init(&uncached_pools[nid].add_chunk_mutex);
	}

	efi_memmap_walk_uc(uncached_build_memmap, NULL);
	return 0;
}

__initcall(uncached_init);
