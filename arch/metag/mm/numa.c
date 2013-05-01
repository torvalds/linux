/*
 *  Multiple memory node support for Meta machines
 *
 *  Copyright (C) 2007  Paul Mundt
 *  Copyright (C) 2010  Imagination Technologies Ltd.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/export.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/mm.h>
#include <linux/numa.h>
#include <linux/pfn.h>
#include <asm/sections.h>

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL_GPL(node_data);

extern char _heap_start[];

/*
 * On Meta machines the conventional approach is to stash system RAM
 * in node 0, and other memory blocks in to node 1 and up, ordered by
 * latency. Each node's pgdat is node-local at the beginning of the node,
 * immediately followed by the node mem map.
 */
void __init setup_bootmem_node(int nid, unsigned long start, unsigned long end)
{
	unsigned long bootmap_pages, bootmem_paddr;
	unsigned long start_pfn, end_pfn;
	unsigned long pgdat_paddr;

	/* Don't allow bogus node assignment */
	BUG_ON(nid > MAX_NUMNODES || nid <= 0);

	start_pfn = start >> PAGE_SHIFT;
	end_pfn = end >> PAGE_SHIFT;

	memblock_add(start, end - start);

	memblock_set_node(PFN_PHYS(start_pfn),
			  PFN_PHYS(end_pfn - start_pfn), nid);

	/* Node-local pgdat */
	pgdat_paddr = memblock_alloc_base(sizeof(struct pglist_data),
					  SMP_CACHE_BYTES, end);
	NODE_DATA(nid) = __va(pgdat_paddr);
	memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));

	NODE_DATA(nid)->bdata = &bootmem_node_data[nid];
	NODE_DATA(nid)->node_start_pfn = start_pfn;
	NODE_DATA(nid)->node_spanned_pages = end_pfn - start_pfn;

	/* Node-local bootmap */
	bootmap_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	bootmem_paddr = memblock_alloc_base(bootmap_pages << PAGE_SHIFT,
					    PAGE_SIZE, end);
	init_bootmem_node(NODE_DATA(nid), bootmem_paddr >> PAGE_SHIFT,
			  start_pfn, end_pfn);

	free_bootmem_with_active_regions(nid, end_pfn);

	/* Reserve the pgdat and bootmap space with the bootmem allocator */
	reserve_bootmem_node(NODE_DATA(nid), pgdat_paddr & PAGE_MASK,
			     sizeof(struct pglist_data), BOOTMEM_DEFAULT);
	reserve_bootmem_node(NODE_DATA(nid), bootmem_paddr,
			     bootmap_pages << PAGE_SHIFT, BOOTMEM_DEFAULT);

	/* It's up */
	node_set_online(nid);

	/* Kick sparsemem */
	sparse_memory_present_with_active_regions(nid);
}

void __init __weak soc_mem_setup(void)
{
}
