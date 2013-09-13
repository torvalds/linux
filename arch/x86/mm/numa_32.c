/*
 * Written by: Patricia Gaughen <gone@us.ibm.com>, IBM Corporation
 * August 2002: added remote node KVA remap - Martin J. Bligh 
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/module.h>

#include "numa_internal.h"

#ifdef CONFIG_DISCONTIGMEM
/*
 * 4) physnode_map     - the mapping between a pfn and owning node
 * physnode_map keeps track of the physical memory layout of a generic
 * numa node on a 64Mb break (each element of the array will
 * represent 64Mb of memory and will be marked by the node id.  so,
 * if the first gig is on node 0, and the second gig is on node 1
 * physnode_map will contain:
 *
 *     physnode_map[0-15] = 0;
 *     physnode_map[16-31] = 1;
 *     physnode_map[32- ] = -1;
 */
s8 physnode_map[MAX_SECTIONS] __read_mostly = { [0 ... (MAX_SECTIONS - 1)] = -1};
EXPORT_SYMBOL(physnode_map);

void memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	printk(KERN_INFO "Node: %d, start_pfn: %lx, end_pfn: %lx\n",
			nid, start, end);
	printk(KERN_DEBUG "  Setting physnode_map array to node %d for pfns:\n", nid);
	printk(KERN_DEBUG "  ");
	for (pfn = start; pfn < end; pfn += PAGES_PER_SECTION) {
		physnode_map[pfn / PAGES_PER_SECTION] = nid;
		printk(KERN_CONT "%lx ", pfn);
	}
	printk(KERN_CONT "\n");
}

unsigned long node_memmap_size_bytes(int nid, unsigned long start_pfn,
					      unsigned long end_pfn)
{
	unsigned long nr_pages = end_pfn - start_pfn;

	if (!nr_pages)
		return 0;

	return (nr_pages + 1) * sizeof(struct page);
}
#endif

extern unsigned long highend_pfn, highstart_pfn;

void __init initmem_init(void)
{
	x86_numa_init();

#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > max_low_pfn)
		highstart_pfn = max_low_pfn;
	printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
	       pages_to_mb(highend_pfn - highstart_pfn));
	high_memory = (void *) __va(highstart_pfn * PAGE_SIZE - 1) + 1;
#else
	high_memory = (void *) __va(max_low_pfn * PAGE_SIZE - 1) + 1;
#endif
	printk(KERN_NOTICE "%ldMB LOWMEM available.\n",
			pages_to_mb(max_low_pfn));
	printk(KERN_DEBUG "max_low_pfn = %lx, highstart_pfn = %lx\n",
			max_low_pfn, highstart_pfn);

	printk(KERN_DEBUG "Low memory ends at vaddr %08lx\n",
			(ulong) pfn_to_kaddr(max_low_pfn));

	printk(KERN_DEBUG "High memory starts at vaddr %08lx\n",
			(ulong) pfn_to_kaddr(highstart_pfn));

	setup_bootmem_allocator();
}
