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
#include <linux/memblock.h>
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
void __init setup_bootmem_node(int nid, unsigned long start, unsigned long end)
{
	unsigned long start_pfn, end_pfn;

	/* Don't allow bogus node assignment */
	BUG_ON(nid >= MAX_NUMNODES || nid <= 0);

	start_pfn = PFN_DOWN(start);
	end_pfn = PFN_DOWN(end);

	pmb_bolt_mapping((unsigned long)__va(start), start, end - start,
			 PAGE_KERNEL);

	memblock_add(start, end - start);

	__add_active_range(nid, start_pfn, end_pfn);

	/* Node-local pgdat */
	NODE_DATA(nid) = memblock_alloc_node(sizeof(struct pglist_data),
					     SMP_CACHE_BYTES, nid);
	if (!NODE_DATA(nid))
		panic("%s: Failed to allocate %zu bytes align=0x%x nid=%d\n",
		      __func__, sizeof(struct pglist_data), SMP_CACHE_BYTES,
		      nid);

	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;

	/* It's up */
	node_set_online(nid);

	/* Kick sparsemem */
	sparse_memory_present_with_active_regions(nid);
}
