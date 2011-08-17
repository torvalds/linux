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

#define LARGE_PAGE_BYTES (PTRS_PER_PTE * PAGE_SIZE)

static void *node_remap_start_vaddr[MAX_NUMNODES];
void set_pmd_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags);

/*
 * Remap memory allocator
 */
static unsigned long node_remap_start_pfn[MAX_NUMNODES];
static void *node_remap_end_vaddr[MAX_NUMNODES];
static void *node_remap_alloc_vaddr[MAX_NUMNODES];

/**
 * alloc_remap - Allocate remapped memory
 * @nid: NUMA node to allocate memory from
 * @size: The size of allocation
 *
 * Allocate @size bytes from the remap area of NUMA node @nid.  The
 * size of the remap area is predetermined by init_alloc_remap() and
 * only the callers considered there should call this function.  For
 * more info, please read the comment on top of init_alloc_remap().
 *
 * The caller must be ready to handle allocation failure from this
 * function and fall back to regular memory allocator in such cases.
 *
 * CONTEXT:
 * Single CPU early boot context.
 *
 * RETURNS:
 * Pointer to the allocated memory on success, %NULL on failure.
 */
void *alloc_remap(int nid, unsigned long size)
{
	void *allocation = node_remap_alloc_vaddr[nid];

	size = ALIGN(size, L1_CACHE_BYTES);

	if (!allocation || (allocation + size) > node_remap_end_vaddr[nid])
		return NULL;

	node_remap_alloc_vaddr[nid] += size;
	memset(allocation, 0, size);

	return allocation;
}

#ifdef CONFIG_HIBERNATION
/**
 * resume_map_numa_kva - add KVA mapping to the temporary page tables created
 *                       during resume from hibernation
 * @pgd_base - temporary resume page directory
 */
void resume_map_numa_kva(pgd_t *pgd_base)
{
	int node;

	for_each_online_node(node) {
		unsigned long start_va, start_pfn, nr_pages, pfn;

		start_va = (unsigned long)node_remap_start_vaddr[node];
		start_pfn = node_remap_start_pfn[node];
		nr_pages = (node_remap_end_vaddr[node] -
			    node_remap_start_vaddr[node]) >> PAGE_SHIFT;

		printk(KERN_DEBUG "%s: node %d\n", __func__, node);

		for (pfn = 0; pfn < nr_pages; pfn += PTRS_PER_PTE) {
			unsigned long vaddr = start_va + (pfn << PAGE_SHIFT);
			pgd_t *pgd = pgd_base + pgd_index(vaddr);
			pud_t *pud = pud_offset(pgd, vaddr);
			pmd_t *pmd = pmd_offset(pud, vaddr);

			set_pmd(pmd, pfn_pmd(start_pfn + pfn,
						PAGE_KERNEL_LARGE_EXEC));

			printk(KERN_DEBUG "%s: %08lx -> pfn %08lx\n",
				__func__, vaddr, start_pfn + pfn);
		}
	}
}
#endif

/**
 * init_alloc_remap - Initialize remap allocator for a NUMA node
 * @nid: NUMA node to initizlie remap allocator for
 *
 * NUMA nodes may end up without any lowmem.  As allocating pgdat and
 * memmap on a different node with lowmem is inefficient, a special
 * remap allocator is implemented which can be used by alloc_remap().
 *
 * For each node, the amount of memory which will be necessary for
 * pgdat and memmap is calculated and two memory areas of the size are
 * allocated - one in the node and the other in lowmem; then, the area
 * in the node is remapped to the lowmem area.
 *
 * As pgdat and memmap must be allocated in lowmem anyway, this
 * doesn't waste lowmem address space; however, the actual lowmem
 * which gets remapped over is wasted.  The amount shouldn't be
 * problematic on machines this feature will be used.
 *
 * Initialization failure isn't fatal.  alloc_remap() is used
 * opportunistically and the callers will fall back to other memory
 * allocation mechanisms on failure.
 */
void __init init_alloc_remap(int nid, u64 start, u64 end)
{
	unsigned long start_pfn = start >> PAGE_SHIFT;
	unsigned long end_pfn = end >> PAGE_SHIFT;
	unsigned long size, pfn;
	u64 node_pa, remap_pa;
	void *remap_va;

	/*
	 * The acpi/srat node info can show hot-add memroy zones where
	 * memory could be added but not currently present.
	 */
	printk(KERN_DEBUG "node %d pfn: [%lx - %lx]\n",
	       nid, start_pfn, end_pfn);

	/* calculate the necessary space aligned to large page size */
	size = node_memmap_size_bytes(nid, start_pfn, end_pfn);
	size += ALIGN(sizeof(pg_data_t), PAGE_SIZE);
	size = ALIGN(size, LARGE_PAGE_BYTES);

	/* allocate node memory and the lowmem remap area */
	node_pa = memblock_find_in_range(start, end, size, LARGE_PAGE_BYTES);
	if (node_pa == MEMBLOCK_ERROR) {
		pr_warning("remap_alloc: failed to allocate %lu bytes for node %d\n",
			   size, nid);
		return;
	}
	memblock_x86_reserve_range(node_pa, node_pa + size, "KVA RAM");

	remap_pa = memblock_find_in_range(min_low_pfn << PAGE_SHIFT,
					  max_low_pfn << PAGE_SHIFT,
					  size, LARGE_PAGE_BYTES);
	if (remap_pa == MEMBLOCK_ERROR) {
		pr_warning("remap_alloc: failed to allocate %lu bytes remap area for node %d\n",
			   size, nid);
		memblock_x86_free_range(node_pa, node_pa + size);
		return;
	}
	memblock_x86_reserve_range(remap_pa, remap_pa + size, "KVA PG");
	remap_va = phys_to_virt(remap_pa);

	/* perform actual remap */
	for (pfn = 0; pfn < size >> PAGE_SHIFT; pfn += PTRS_PER_PTE)
		set_pmd_pfn((unsigned long)remap_va + (pfn << PAGE_SHIFT),
			    (node_pa >> PAGE_SHIFT) + pfn,
			    PAGE_KERNEL_LARGE);

	/* initialize remap allocator parameters */
	node_remap_start_pfn[nid] = node_pa >> PAGE_SHIFT;
	node_remap_start_vaddr[nid] = remap_va;
	node_remap_end_vaddr[nid] = remap_va + size;
	node_remap_alloc_vaddr[nid] = remap_va;

	printk(KERN_DEBUG "remap_alloc: node %d [%08llx-%08llx) -> [%p-%p)\n",
	       nid, node_pa, node_pa + size, remap_va, remap_va + size);
}

void __init initmem_init(void)
{
	x86_numa_init();

#ifdef CONFIG_HIGHMEM
	highstart_pfn = highend_pfn = max_pfn;
	if (max_pfn > max_low_pfn)
		highstart_pfn = max_low_pfn;
	printk(KERN_NOTICE "%ldMB HIGHMEM available.\n",
	       pages_to_mb(highend_pfn - highstart_pfn));
	num_physpages = highend_pfn;
	high_memory = (void *) __va(highstart_pfn * PAGE_SIZE - 1) + 1;
#else
	num_physpages = max_low_pfn;
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
