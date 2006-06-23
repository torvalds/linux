/*
 * Copyright (C) 2001-2006 Silicon Graphics, Inc.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License
 * as published by the Free Software Foundation.
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
#include <linux/slab.h>
#include <linux/efi.h>
#include <linux/genalloc.h>
#include <asm/page.h>
#include <asm/pal.h>
#include <asm/system.h>
#include <asm/pgtable.h>
#include <asm/atomic.h>
#include <asm/tlbflush.h>
#include <asm/sn/arch.h>


extern void __init efi_memmap_walk_uc(efi_freemem_callback_t, void *);

#define MAX_UNCACHED_GRANULES	5
static int allocated_granules;

struct gen_pool *uncached_pool[MAX_NUMNODES];


static void uncached_ipi_visibility(void *data)
{
	int status;

	status = ia64_pal_prefetch_visibility(PAL_VISIBILITY_PHYSICAL);
	if ((status != PAL_VISIBILITY_OK) &&
	    (status != PAL_VISIBILITY_OK_REMOTE_NEEDED))
		printk(KERN_DEBUG "pal_prefetch_visibility() returns %i on "
		       "CPU %i\n", status, raw_smp_processor_id());
}


static void uncached_ipi_mc_drain(void *data)
{
	int status;

	status = ia64_pal_mc_drain();
	if (status)
		printk(KERN_WARNING "ia64_pal_mc_drain() failed with %i on "
		       "CPU %i\n", status, raw_smp_processor_id());
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
static int uncached_add_chunk(struct gen_pool *pool, int nid)
{
	struct page *page;
	int status, i;
	unsigned long c_addr, uc_addr;

	if (allocated_granules >= MAX_UNCACHED_GRANULES)
		return -1;

	/* attempt to allocate a granule's worth of cached memory pages */

	page = alloc_pages_node(nid, GFP_KERNEL | __GFP_ZERO,
				IA64_GRANULE_SHIFT-PAGE_SHIFT);
	if (!page)
		return -1;

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

	flush_tlb_kernel_range(uc_addr, uc_adddr + IA64_GRANULE_SIZE);

	status = ia64_pal_prefetch_visibility(PAL_VISIBILITY_PHYSICAL);
	if (!status) {
		status = smp_call_function(uncached_ipi_visibility, NULL, 0, 1);
		if (status)
			goto failed;
	}

	preempt_disable();

	if (ia64_platform_is("sn2"))
		sn_flush_all_caches(uc_addr, IA64_GRANULE_SIZE);
	else
		flush_icache_range(uc_addr, uc_addr + IA64_GRANULE_SIZE);

	/* flush the just introduced uncached translation from the TLB */
	local_flush_tlb_all();

	preempt_enable();

	ia64_pal_mc_drain();
	status = smp_call_function(uncached_ipi_mc_drain, NULL, 0, 1);
	if (status)
		goto failed;

	/*
	 * The chunk of memory pages has been converted to uncached so now we
	 * can add it to the pool.
	 */
	status = gen_pool_add(pool, uc_addr, IA64_GRANULE_SIZE, nid);
	if (status)
		goto failed;

	allocated_granules++;
	return 0;

	/* failed to convert or add the chunk so give it back to the kernel */
failed:
	for (i = 0; i < (IA64_GRANULE_SIZE / PAGE_SIZE); i++)
		ClearPageUncached(&page[i]);

	free_pages(c_addr, IA64_GRANULE_SHIFT-PAGE_SHIFT);
	return -1;
}


/*
 * uncached_alloc_page
 *
 * @starting_nid: node id of node to start with, or -1
 *
 * Allocate 1 uncached page. Allocates on the requested node. If no
 * uncached pages are available on the requested node, roundrobin starting
 * with the next higher node.
 */
unsigned long uncached_alloc_page(int starting_nid)
{
	unsigned long uc_addr;
	struct gen_pool *pool;
	int nid;

	if (unlikely(starting_nid >= MAX_NUMNODES))
		return 0;

	if (starting_nid < 0)
		starting_nid = numa_node_id();
	nid = starting_nid;

	do {
		if (!node_online(nid))
			continue;
		pool = uncached_pool[nid];
		if (pool == NULL)
			continue;
		do {
			uc_addr = gen_pool_alloc(pool, PAGE_SIZE);
			if (uc_addr != 0)
				return uc_addr;
		} while (uncached_add_chunk(pool, nid) == 0);

	} while ((nid = (nid + 1) % MAX_NUMNODES) != starting_nid);

	return 0;
}
EXPORT_SYMBOL(uncached_alloc_page);


/*
 * uncached_free_page
 *
 * @uc_addr: uncached address of page to free
 *
 * Free a single uncached page.
 */
void uncached_free_page(unsigned long uc_addr)
{
	int nid = paddr_to_nid(uc_addr - __IA64_UNCACHED_OFFSET);
	struct gen_pool *pool = uncached_pool[nid];

	if (unlikely(pool == NULL))
		return;

	if ((uc_addr & (0XFUL << 60)) != __IA64_UNCACHED_OFFSET)
		panic("uncached_free_page invalid address %lx\n", uc_addr);

	gen_pool_free(pool, uc_addr, PAGE_SIZE);
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
static int __init uncached_build_memmap(unsigned long uc_start,
					unsigned long uc_end, void *arg)
{
	int nid = paddr_to_nid(uc_start - __IA64_UNCACHED_OFFSET);
	struct gen_pool *pool = uncached_pool[nid];
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
		uncached_pool[nid] = gen_pool_create(PAGE_SHIFT, nid);
	}

	efi_memmap_walk_uc(uncached_build_memmap, NULL);
	return 0;
}

__initcall(uncached_init);
