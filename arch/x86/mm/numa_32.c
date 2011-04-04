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

#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#include <linux/mmzone.h>
#include <linux/highmem.h>
#include <linux/initrd.h>
#include <linux/nodemask.h>
#include <linux/module.h>
#include <linux/kexec.h>
#include <linux/pfn.h>
#include <linux/swap.h>
#include <linux/acpi.h>

#include <asm/e820.h>
#include <asm/setup.h>
#include <asm/mmzone.h>
#include <asm/bios_ebda.h>
#include <asm/proto.h>

struct pglist_data *node_data[MAX_NUMNODES] __read_mostly;
EXPORT_SYMBOL(node_data);

/*
 * numa interface - we expect the numa architecture specific code to have
 *                  populated the following initialisation.
 *
 * 1) node_online_map  - the map of all nodes configured (online) in the system
 * 2) node_start_pfn   - the starting page frame number for a node
 * 3) node_end_pfn     - the ending page fram number for a node
 */
unsigned long node_start_pfn[MAX_NUMNODES] __read_mostly;
unsigned long node_end_pfn[MAX_NUMNODES] __read_mostly;


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
s8 physnode_map[MAX_ELEMENTS] __read_mostly = { [0 ... (MAX_ELEMENTS - 1)] = -1};
EXPORT_SYMBOL(physnode_map);

void memory_present(int nid, unsigned long start, unsigned long end)
{
	unsigned long pfn;

	printk(KERN_INFO "Node: %d, start_pfn: %lx, end_pfn: %lx\n",
			nid, start, end);
	printk(KERN_DEBUG "  Setting physnode_map array to node %d for pfns:\n", nid);
	printk(KERN_DEBUG "  ");
	for (pfn = start; pfn < end; pfn += PAGES_PER_ELEMENT) {
		physnode_map[pfn / PAGES_PER_ELEMENT] = nid;
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

extern unsigned long find_max_low_pfn(void);
extern unsigned long highend_pfn, highstart_pfn;

#define LARGE_PAGE_BYTES (PTRS_PER_PTE * PAGE_SIZE)

static unsigned long node_remap_size[MAX_NUMNODES];
static void *node_remap_start_vaddr[MAX_NUMNODES];
void set_pmd_pfn(unsigned long vaddr, unsigned long pfn, pgprot_t flags);

int __cpuinit numa_cpu_node(int cpu)
{
	return apic->x86_32_numa_cpu_node(cpu);
}

/*
 * FLAT - support for basic PC memory model with discontig enabled, essentially
 *        a single node with all available processors in it with a flat
 *        memory map.
 */
int __init get_memcfg_numa_flat(void)
{
	printk(KERN_DEBUG "NUMA - single node, flat memory mode\n");

	node_start_pfn[0] = 0;
	node_end_pfn[0] = max_pfn;
	memblock_x86_register_active_regions(0, 0, max_pfn);
	memory_present(0, 0, max_pfn);

        /* Indicate there is one node available. */
	nodes_clear(node_online_map);
	node_set_online(0);
	return 1;
}

/*
 * Find the highest page frame number we have available for the node
 */
static void __init propagate_e820_map_node(int nid)
{
	if (node_end_pfn[nid] > max_pfn)
		node_end_pfn[nid] = max_pfn;
	/*
	 * if a user has given mem=XXXX, then we need to make sure 
	 * that the node _starts_ before that, too, not just ends
	 */
	if (node_start_pfn[nid] > max_pfn)
		node_start_pfn[nid] = max_pfn;
	BUG_ON(node_start_pfn[nid] > node_end_pfn[nid]);
}

/* 
 * Allocate memory for the pg_data_t for this node via a crude pre-bootmem
 * method.  For node zero take this from the bottom of memory, for
 * subsequent nodes place them at node_remap_start_vaddr which contains
 * node local data in physically node local memory.  See setup_memory()
 * for details.
 */
static void __init allocate_pgdat(int nid)
{
	char buf[16];

	if (node_has_online_mem(nid) && node_remap_start_vaddr[nid])
		NODE_DATA(nid) = (pg_data_t *)node_remap_start_vaddr[nid];
	else {
		unsigned long pgdat_phys;
		pgdat_phys = memblock_find_in_range(min_low_pfn<<PAGE_SHIFT,
				 max_pfn_mapped<<PAGE_SHIFT,
				 sizeof(pg_data_t),
				 PAGE_SIZE);
		NODE_DATA(nid) = (pg_data_t *)(pfn_to_kaddr(pgdat_phys>>PAGE_SHIFT));
		memset(buf, 0, sizeof(buf));
		sprintf(buf, "NODE_DATA %d",  nid);
		memblock_x86_reserve_range(pgdat_phys, pgdat_phys + sizeof(pg_data_t), buf);
	}
	printk(KERN_DEBUG "allocate_pgdat: node %d NODE_DATA %08lx\n",
		nid, (unsigned long)NODE_DATA(nid));
}

/*
 * In the DISCONTIGMEM and SPARSEMEM memory model, a portion of the kernel
 * virtual address space (KVA) is reserved and portions of nodes are mapped
 * using it. This is to allow node-local memory to be allocated for
 * structures that would normally require ZONE_NORMAL. The memory is
 * allocated with alloc_remap() and callers should be prepared to allocate
 * from the bootmem allocator instead.
 */
static unsigned long node_remap_start_pfn[MAX_NUMNODES];
static void *node_remap_end_vaddr[MAX_NUMNODES];
static void *node_remap_alloc_vaddr[MAX_NUMNODES];
static unsigned long node_remap_offset[MAX_NUMNODES];

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
		unsigned long start_va, start_pfn, size, pfn;

		start_va = (unsigned long)node_remap_start_vaddr[node];
		start_pfn = node_remap_start_pfn[node];
		size = node_remap_size[node];

		printk(KERN_DEBUG "%s: node %d\n", __func__, node);

		for (pfn = 0; pfn < size; pfn += PTRS_PER_PTE) {
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

static __init unsigned long init_alloc_remap(int nid, unsigned long offset)
{
	unsigned long size, pfn;
	u64 node_pa, remap_pa;
	void *remap_va;

	/*
	 * The acpi/srat node info can show hot-add memroy zones where
	 * memory could be added but not currently present.
	 */
	printk(KERN_DEBUG "node %d pfn: [%lx - %lx]\n",
	       nid, node_start_pfn[nid], node_end_pfn[nid]);
	if (node_start_pfn[nid] > max_pfn)
		return 0;
	if (!node_end_pfn[nid])
		return 0;
	if (node_end_pfn[nid] > max_pfn)
		node_end_pfn[nid] = max_pfn;

	/* calculate the necessary space aligned to large page size */
	size = node_memmap_size_bytes(nid, node_start_pfn[nid],
				      min(node_end_pfn[nid], max_pfn));
	size += ALIGN(sizeof(pg_data_t), PAGE_SIZE);
	size = ALIGN(size, LARGE_PAGE_BYTES);

	/* allocate node memory and the lowmem remap area */
	node_pa = memblock_find_in_range(node_start_pfn[nid] << PAGE_SHIFT,
					 (u64)node_end_pfn[nid] << PAGE_SHIFT,
					 size, LARGE_PAGE_BYTES);
	if (node_pa == MEMBLOCK_ERROR) {
		pr_warning("remap_alloc: failed to allocate %lu bytes for node %d\n",
			   size, nid);
		return 0;
	}
	memblock_x86_reserve_range(node_pa, node_pa + size, "KVA RAM");

	remap_pa = memblock_find_in_range(min_low_pfn << PAGE_SHIFT,
					  max_low_pfn << PAGE_SHIFT,
					  size, LARGE_PAGE_BYTES);
	if (remap_pa == MEMBLOCK_ERROR) {
		pr_warning("remap_alloc: failed to allocate %lu bytes remap area for node %d\n",
			   size, nid);
		memblock_x86_free_range(node_pa, node_pa + size);
		return 0;
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
	node_remap_size[nid] = size >> PAGE_SHIFT;
	node_remap_offset[nid] = offset;

	node_remap_start_vaddr[nid] = remap_va;
	node_remap_end_vaddr[nid] = remap_va + size;
	node_remap_alloc_vaddr[nid] = remap_va + ALIGN(sizeof(pg_data_t), PAGE_SIZE);

	printk(KERN_DEBUG "remap_alloc: node %d [%08llx-%08llx) -> [%p-%p)\n",
	       nid, node_pa, node_pa + size, remap_va, remap_va + size);

	return size >> PAGE_SHIFT;
}

void __init initmem_init(void)
{
	unsigned long reserve_pages = 0;
	int nid;

	/*
	 * When mapping a NUMA machine we allocate the node_mem_map arrays
	 * from node local memory.  They are then mapped directly into KVA
	 * between zone normal and vmalloc space.  Calculate the size of
	 * this space and use it to adjust the boundary between ZONE_NORMAL
	 * and ZONE_HIGHMEM.
	 */

	get_memcfg_numa();
	numa_init_array();

	for_each_online_node(nid)
		reserve_pages += init_alloc_remap(nid, reserve_pages);

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
	for_each_online_node(nid)
		allocate_pgdat(nid);

	printk(KERN_DEBUG "High memory starts at vaddr %08lx\n",
			(ulong) pfn_to_kaddr(highstart_pfn));
	for_each_online_node(nid)
		propagate_e820_map_node(nid);

	for_each_online_node(nid) {
		memset(NODE_DATA(nid), 0, sizeof(struct pglist_data));
		NODE_DATA(nid)->node_id = nid;
	}

	setup_bootmem_allocator();
}

#ifdef CONFIG_MEMORY_HOTPLUG
static int paddr_to_nid(u64 addr)
{
	int nid;
	unsigned long pfn = PFN_DOWN(addr);

	for_each_node(nid)
		if (node_start_pfn[nid] <= pfn &&
		    pfn < node_end_pfn[nid])
			return nid;

	return -1;
}

/*
 * This function is used to ask node id BEFORE memmap and mem_section's
 * initialization (pfn_to_nid() can't be used yet).
 * If _PXM is not defined on ACPI's DSDT, node id must be found by this.
 */
int memory_add_physaddr_to_nid(u64 addr)
{
	int nid = paddr_to_nid(addr);
	return (nid >= 0) ? nid : 0;
}

EXPORT_SYMBOL_GPL(memory_add_physaddr_to_nid);
#endif

