/*
 * arch/sh/mm/numa.c - Multiple node support for SH machines
 *
 *  Copyright (C) 2007  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/module.h>
#include <linux/bootmem.h>
#include <linux/lmb.h>
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/pfn.h>
#include <asm/sections.h>

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL_GPL(node_data);

/*
 * On SH machines the conventional approach is to stash system RAM
 * in node 0, and other memory blocks in to node 1 and up, ordered by
 * latency. Each node's pgdat is node-local at the beginning of the node,
 * immediately followed by the node mem map.
 */
void __init setup_memory(void)
{
	unsigned long free_pfn = PFN_UP(__pa(_end));
	u64 base = min_low_pfn << PAGE_SHIFT;
	u64 size = (max_low_pfn << PAGE_SHIFT) - base;

	lmb_add(base, size);

	/* Reserve the LMB regions used by the kernel, initrd, etc.. */
	lmb_reserve(__MEMORY_START + CONFIG_ZERO_PAGE_OFFSET,
		    (PFN_PHYS(free_pfn) + PAGE_SIZE - 1) -
		    (__MEMORY_START + CONFIG_ZERO_PAGE_OFFSET));

	/*
	 * Reserve physical pages below CONFIG_ZERO_PAGE_OFFSET.
	 */
	if (CONFIG_ZERO_PAGE_OFFSET != 0)
		lmb_reserve(__MEMORY_START, CONFIG_ZERO_PAGE_OFFSET);

	lmb_analyze();
	lmb_dump_all();

	/*
	 * Node 0 sets up its pgdat at the first available pfn,
	 * and bumps it up before setting up the bootmem allocator.
	 */
	NODE_DATA(0) = pfn_to_kaddr(free_pfn);
	memset(NODE_DATA(0), 0, sizeof(struct pglist_data));
	free_pfn += PFN_UP(sizeof(struct pglist_data));
	NODE_DATA(0)->bdata = &bootmem_node_data[0];

	/* Set up node 0 */
	setup_bootmem_allocator(free_pfn);

	/* Give the platforms a chance to hook up their nodes */
	plat_mem_setup();
}

void __init setup_bootmem_node(int nid, unsigned long start, unsigned long end)
{
	unsigned long bootmap_pages;
	unsigned long start_pfn, end_pfn;
	unsigned long bootmem_paddr;

	/* Don't allow bogus node assignment */
	BUG_ON(nid > MAX_NUMNODES || nid <= 0);

	start_pfn = start >> PAGE_SHIFT;
	end_pfn = end >> PAGE_SHIFT;

	lmb_add(start, end - start);

	__add_active_range(nid, start_pfn, end_pfn);

	/* Node-local pgdat */
	NODE_DATA(nid) = __va(lmb_alloc_base(sizeof(struct pglist_data),
					     SMP_CACHE_BYTES, end));
	memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

	NODE_DATA(nid)->bdata = &bootmem_node_data[nid];
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;

	/* Node-local bootmap */
	bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	bootmem_paddr = lmb_alloc_base(bootmap_pages << PAGE_SHIFT,
				       PAGE_SIZE, end);
	init_bootmem_node(NODE_DATA(nid), bootmem_paddr >> PAGE_SHIFT,
			  start_pfn, end_pfn);

	free_bootmem_with_active_regions(nid, end_pfn);

	/* Reserve the pgdat and bootmap space with the bootmem allocator */
	reserve_bootmem_node(NODE_DATA(nid), start_pfn << PAGE_SHIFT,
			     sizeof(struct pglist_data), BOOTMEM_DEFAULT);
	reserve_bootmem_node(NODE_DATA(nid), bootmem_paddr,
			     bootmap_pages << PAGE_SHIFT, BOOTMEM_DEFAULT);

	/* It's up */
	node_set_online(nid);

	/* Kick sparsemem */
	sparse_memory_present_with_active_regions(nid);
}
