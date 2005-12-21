/*
 * Copyright (C) 2001-2005 Silicon Graphics, Inc.  All rights reserved.
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

#define DEBUG	0

#if DEBUG
#define dprintk			printk
#else
#define dprintk(x...)		do { } while (0)
#endif

void __init efi_memmap_walk_uc (efi_freemem_callback_t callback);

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


static unsigned long
uncached_get_new_chunk(struct gen_pool *poolp)
{
	struct page *page;
	void *tmp;
	int status, i;
	unsigned long addr, node;

	if (allocated_granules >= MAX_UNCACHED_GRANULES)
		return 0;

	node = poolp->private;
	page = alloc_pages_node(node, GFP_KERNEL | __GFP_ZERO,
				IA64_GRANULE_SHIFT-PAGE_SHIFT);

	dprintk(KERN_INFO "get_new_chunk page %p, addr %lx\n",
		page, (unsigned long)(page-vmem_map) << PAGE_SHIFT);

	/*
	 * Do magic if no mem on local node! XXX
	 */
	if (!page)
		return 0;
	tmp = page_address(page);

	/*
	 * There's a small race here where it's possible for someone to
	 * access the page through /dev/mem halfway through the conversion
	 * to uncached - not sure it's really worth bothering about
	 */
	for (i = 0; i < (IA64_GRANULE_SIZE / PAGE_SIZE); i++)
		SetPageUncached(&page[i]);

	flush_tlb_kernel_range(tmp, tmp + IA64_GRANULE_SIZE);

	status = ia64_pal_prefetch_visibility(PAL_VISIBILITY_PHYSICAL);

	dprintk(KERN_INFO "pal_prefetch_visibility() returns %i on cpu %i\n",
		status, raw_smp_processor_id());

	if (!status) {
		status = smp_call_function(uncached_ipi_visibility, NULL, 0, 1);
		if (status)
			printk(KERN_WARNING "smp_call_function failed for "
			       "uncached_ipi_visibility! (%i)\n", status);
	}

	if (ia64_platform_is("sn2"))
		sn_flush_all_caches((unsigned long)tmp, IA64_GRANULE_SIZE);
	else
		flush_icache_range((unsigned long)tmp,
				   (unsigned long)tmp+IA64_GRANULE_SIZE);

	ia64_pal_mc_drain();
	status = smp_call_function(uncached_ipi_mc_drain, NULL, 0, 1);
	if (status)
		printk(KERN_WARNING "smp_call_function failed for "
		       "uncached_ipi_mc_drain! (%i)\n", status);

	addr = (unsigned long)tmp - PAGE_OFFSET + __IA64_UNCACHED_OFFSET;

	allocated_granules++;
	return addr;
}


/*
 * uncached_alloc_page
 *
 * Allocate 1 uncached page. Allocates on the requested node. If no
 * uncached pages are available on the requested node, roundrobin starting
 * with higher nodes.
 */
unsigned long
uncached_alloc_page(int nid)
{
	unsigned long maddr;

	maddr = gen_pool_alloc(uncached_pool[nid], PAGE_SIZE);

	dprintk(KERN_DEBUG "uncached_alloc_page returns %lx on node %i\n",
		maddr, nid);

	/*
	 * If no memory is availble on our local node, try the
	 * remaining nodes in the system.
	 */
	if (!maddr) {
		int i;

		for (i = MAX_NUMNODES - 1; i >= 0; i--) {
			if (i == nid || !node_online(i))
				continue;
			maddr = gen_pool_alloc(uncached_pool[i], PAGE_SIZE);
			dprintk(KERN_DEBUG "uncached_alloc_page alternate search "
				"returns %lx on node %i\n", maddr, i);
			if (maddr) {
				break;
			}
		}
	}

	return maddr;
}
EXPORT_SYMBOL(uncached_alloc_page);


/*
 * uncached_free_page
 *
 * Free a single uncached page.
 */
void
uncached_free_page(unsigned long maddr)
{
	int node;

	node = paddr_to_nid(maddr - __IA64_UNCACHED_OFFSET);

	dprintk(KERN_DEBUG "uncached_free_page(%lx) on node %i\n", maddr, node);

	if ((maddr & (0XFUL << 60)) != __IA64_UNCACHED_OFFSET)
		panic("uncached_free_page invalid address %lx\n", maddr);

	gen_pool_free(uncached_pool[node], maddr, PAGE_SIZE);
}
EXPORT_SYMBOL(uncached_free_page);


/*
 * uncached_build_memmap,
 *
 * Called at boot time to build a map of pages that can be used for
 * memory special operations.
 */
static int __init
uncached_build_memmap(unsigned long start, unsigned long end, void *arg)
{
	long length = end - start;
	int node;

	dprintk(KERN_ERR "uncached_build_memmap(%lx %lx)\n", start, end);

	memset((char *)start, 0, length);

	node = paddr_to_nid(start - __IA64_UNCACHED_OFFSET);

	for (; start < end ; start += PAGE_SIZE) {
		dprintk(KERN_INFO "sticking %lx into the pool!\n", start);
		gen_pool_free(uncached_pool[node], start, PAGE_SIZE);
	}

	return 0;
}


static int __init uncached_init(void) {
	int i;

	for (i = 0; i < MAX_NUMNODES; i++) {
		if (!node_online(i))
			continue;
		uncached_pool[i] = gen_pool_create(0, IA64_GRANULE_SHIFT,
						   &uncached_get_new_chunk, i);
	}

	efi_memmap_walk_uc(uncached_build_memmap);

	return 0;
}

__initcall(uncached_init);
