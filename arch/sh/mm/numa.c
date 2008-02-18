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
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/pfn.h>
#include <asm/sections.h>

static bootmem_data_t plat_node_bdata[MAX_NUMNODES];
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

	/*
	 * Node 0 sets up its pgdat at the first available pfn,
	 * and bumps it up before setting up the bootmem allocator.
	 */
	NODE_DATA(0) = pfn_to_kaddr(free_pfn);
	memset(NODE_DATA(0), 0, sizeof(struct pglist_data));
	free_pfn += PFN_UP(sizeof(struct pglist_data));
	NODE_DATA(0)->bdata = &plat_node_bdata[0];

	/* Set up node 0 */
	setup_bootmem_allocator(free_pfn);

	/* Give the platforms a chance to hook up their nodes */
	plat_mem_setup();
}

void __init setup_bootmem_node(int nid, unsigned long start, unsigned long end)
{
	unsigned long bootmap_pages, bootmap_start, bootmap_size;
	unsigned long start_pfn, free_pfn, end_pfn;

	/* Don't allow bogus node assignment */
	BUG_ON(nid > MAX_NUMNODES || nid == 0);

	/*
	 * The free pfn starts at the beginning of the range, and is
	 * advanced as necessary for pgdat and node map allocations.
	 */
	free_pfn = start_pfn = start >> PAGE_SHIFT;
	end_pfn = end >> PAGE_SHIFT;

	add_active_range(nid, start_pfn, end_pfn);

	/* Node-local pgdat */
	NODE_DATA(nid) = pfn_to_kaddr(free_pfn);
	free_pfn += PFN_UP(sizeof(struct pglist_data));
	memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

	NODE_DATA(nid)->bdata = &plat_node_bdata[nid];
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;

	/* Node-local bootmap */
	bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	bootmap_start = (unsigned long)pfn_to_kaddr(free_pfn);
	bootmap_size = init_bootmem_node(NODE_DATA(nid), free_pfn, start_pfn,
				    end_pfn);

	free_bootmem_with_active_regions(nid, end_pfn);

	/* Reserve the pgdat and bootmap space with the bootmem allocator */
	reserve_bootmem_node(NODE_DATA(nid), start_pfn << PAGE_SHIFT,
			     sizeof(struct pglist_data), BOOTMEM_DEFAULT);
	reserve_bootmem_node(NODE_DATA(nid), free_pfn << PAGE_SHIFT,
			     bootmap_pages << PAGE_SHIFT, BOOTMEM_DEFAULT);

	/* It's up */
	node_set_online(nid);

	/* Kick sparsemem */
	sparse_memory_present_with_active_regions(nid);
}
