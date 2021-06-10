// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2000, 2003 Silicon Graphics, Inc.  All rights reserved.
 * Copyright (c) 2001 Intel Corp.
 * Copyright (c) 2001 Tony Luck <tony.luck@intel.com>
 * Copyright (c) 2002 NEC Corp.
 * Copyright (c) 2002 Kimio Suganuma <k-suganuma@da.jp.nec.com>
 * Copyright (c) 2004 Silicon Graphics, Inc
 *	Russ Anderson <rja@sgi.com>
 *	Jesse Barnes <jbarnes@sgi.com>
 *	Jack Steiner <steiner@sgi.com>
 */

/*
 * Platform initialization for Discontig Memory
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/nmi.h>
#include <linux/swap.h>
#include <linux/memblock.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/nodemask.h>
#include <linux/slab.h>
#include <asm/tlb.h>
#include <asm/meminit.h>
#include <asm/numa.h>
#include <asm/sections.h>

/*
 * Track per-node information needed to setup the boot memory allocator, the
 * per-node areas, and the real VM.
 */
struct early_node_data {
	struct ia64_node_data *node_data;
	unsigned long pernode_addr;
	unsigned long pernode_size;
	unsigned long min_pfn;
	unsigned long max_pfn;
};

static struct early_node_data mem_data[MAX_NUMNODES] __initdata;
static nodemask_t memory_less_mask __initdata;

pg_data_t *pgdat_list[MAX_NUMNODES];

/*
 * To prevent cache aliasing effects, align per-node structures so that they
 * start at addresses that are strided by node number.
 */
#define MAX_NODE_ALIGN_OFFSET	(32 * 1024 * 1024)
#define NODEDATA_ALIGN(addr, node)						\
	((((addr) + 1024*1024-1) & ~(1024*1024-1)) + 				\
	     (((node)*PERCPU_PAGE_SIZE) & (MAX_NODE_ALIGN_OFFSET - 1)))

/**
 * build_node_maps - callback to setup mem_data structs for each node
 * @start: physical start of range
 * @len: length of range
 * @node: node where this range resides
 *
 * Detect extents of each piece of memory that we wish to
 * treat as a virtually contiguous block (i.e. each node). Each such block
 * must start on an %IA64_GRANULE_SIZE boundary, so we round the address down
 * if necessary.  Any non-existent pages will simply be part of the virtual
 * memmap.
 */
static int __init build_node_maps(unsigned long start, unsigned long len,
				  int node)
{
	unsigned long spfn, epfn, end = start + len;

	epfn = GRANULEROUNDUP(end) >> PAGE_SHIFT;
	spfn = GRANULEROUNDDOWN(start) >> PAGE_SHIFT;

	if (!mem_data[node].min_pfn) {
		mem_data[node].min_pfn = spfn;
		mem_data[node].max_pfn = epfn;
	} else {
		mem_data[node].min_pfn = min(spfn, mem_data[node].min_pfn);
		mem_data[node].max_pfn = max(epfn, mem_data[node].max_pfn);
	}

	return 0;
}

/**
 * early_nr_cpus_node - return number of cpus on a given node
 * @node: node to check
 *
 * Count the number of cpus on @node.  We can't use nr_cpus_node() yet because
 * acpi_boot_init() (which builds the node_to_cpu_mask array) hasn't been
 * called yet.  Note that node 0 will also count all non-existent cpus.
 */
static int early_nr_cpus_node(int node)
{
	int cpu, n = 0;

	for_each_possible_early_cpu(cpu)
		if (node == node_cpuid[cpu].nid)
			n++;

	return n;
}

/**
 * compute_pernodesize - compute size of pernode data
 * @node: the node id.
 */
static unsigned long compute_pernodesize(int node)
{
	unsigned long pernodesize = 0, cpus;

	cpus = early_nr_cpus_node(node);
	pernodesize += PERCPU_PAGE_SIZE * cpus;
	pernodesize += node * L1_CACHE_BYTES;
	pernodesize += L1_CACHE_ALIGN(sizeof(pg_data_t));
	pernodesize += L1_CACHE_ALIGN(sizeof(struct ia64_node_data));
	pernodesize += L1_CACHE_ALIGN(sizeof(pg_data_t));
	pernodesize = PAGE_ALIGN(pernodesize);
	return pernodesize;
}

/**
 * per_cpu_node_setup - setup per-cpu areas on each node
 * @cpu_data: per-cpu area on this node
 * @node: node to setup
 *
 * Copy the static per-cpu data into the region we just set aside and then
 * setup __per_cpu_offset for each CPU on this node.  Return a pointer to
 * the end of the area.
 */
static void *per_cpu_node_setup(void *cpu_data, int node)
{
#ifdef CONFIG_SMP
	int cpu;

	for_each_possible_early_cpu(cpu) {
		void *src = cpu == 0 ? __cpu0_per_cpu : __phys_per_cpu_start;

		if (node != node_cpuid[cpu].nid)
			continue;

		memcpy(__va(cpu_data), src, __per_cpu_end - __per_cpu_start);
		__per_cpu_offset[cpu] = (char *)__va(cpu_data) -
			__per_cpu_start;

		/*
		 * percpu area for cpu0 is moved from the __init area
		 * which is setup by head.S and used till this point.
		 * Update ar.k3.  This move is ensures that percpu
		 * area for cpu0 is on the correct node and its
		 * virtual address isn't insanely far from other
		 * percpu areas which is important for congruent
		 * percpu allocator.
		 */
		if (cpu == 0)
			ia64_set_kr(IA64_KR_PER_CPU_DATA,
				    (unsigned long)cpu_data -
				    (unsigned long)__per_cpu_start);

		cpu_data += PERCPU_PAGE_SIZE;
	}
#endif
	return cpu_data;
}

#ifdef CONFIG_SMP
/**
 * setup_per_cpu_areas - setup percpu areas
 *
 * Arch code has already allocated and initialized percpu areas.  All
 * this function has to do is to teach the determined layout to the
 * dynamic percpu allocator, which happens to be more complex than
 * creating whole new ones using helpers.
 */
void __init setup_per_cpu_areas(void)
{
	struct pcpu_alloc_info *ai;
	struct pcpu_group_info *gi;
	unsigned int *cpu_map;
	void *base;
	unsigned long base_offset;
	unsigned int cpu;
	ssize_t static_size, reserved_size, dyn_size;
	int node, prev_node, unit, nr_units;

	ai = pcpu_alloc_alloc_info(MAX_NUMNODES, nr_cpu_ids);
	if (!ai)
		panic("failed to allocate pcpu_alloc_info");
	cpu_map = ai->groups[0].cpu_map;

	/* determine base */
	base = (void *)ULONG_MAX;
	for_each_possible_cpu(cpu)
		base = min(base,
			   (void *)(__per_cpu_offset[cpu] + __per_cpu_start));
	base_offset = (void *)__per_cpu_start - base;

	/* build cpu_map, units are grouped by node */
	unit = 0;
	for_each_node(node)
		for_each_possible_cpu(cpu)
			if (node == node_cpuid[cpu].nid)
				cpu_map[unit++] = cpu;
	nr_units = unit;

	/* set basic parameters */
	static_size = __per_cpu_end - __per_cpu_start;
	reserved_size = PERCPU_MODULE_RESERVE;
	dyn_size = PERCPU_PAGE_SIZE - static_size - reserved_size;
	if (dyn_size < 0)
		panic("percpu area overflow static=%zd reserved=%zd\n",
		      static_size, reserved_size);

	ai->static_size		= static_size;
	ai->reserved_size	= reserved_size;
	ai->dyn_size		= dyn_size;
	ai->unit_size		= PERCPU_PAGE_SIZE;
	ai->atom_size		= PAGE_SIZE;
	ai->alloc_size		= PERCPU_PAGE_SIZE;

	/*
	 * CPUs are put into groups according to node.  Walk cpu_map
	 * and create new groups at node boundaries.
	 */
	prev_node = NUMA_NO_NODE;
	ai->nr_groups = 0;
	for (unit = 0; unit < nr_units; unit++) {
		cpu = cpu_map[unit];
		node = node_cpuid[cpu].nid;

		if (node == prev_node) {
			gi->nr_units++;
			continue;
		}
		prev_node = node;

		gi = &ai->groups[ai->nr_groups++];
		gi->nr_units		= 1;
		gi->base_offset		= __per_cpu_offset[cpu] + base_offset;
		gi->cpu_map		= &cpu_map[unit];
	}

	pcpu_setup_first_chunk(ai, base);
	pcpu_free_alloc_info(ai);
}
#endif

/**
 * fill_pernode - initialize pernode data.
 * @node: the node id.
 * @pernode: physical address of pernode data
 * @pernodesize: size of the pernode data
 */
static void __init fill_pernode(int node, unsigned long pernode,
	unsigned long pernodesize)
{
	void *cpu_data;
	int cpus = early_nr_cpus_node(node);

	mem_data[node].pernode_addr = pernode;
	mem_data[node].pernode_size = pernodesize;
	memset(__va(pernode), 0, pernodesize);

	cpu_data = (void *)pernode;
	pernode += PERCPU_PAGE_SIZE * cpus;
	pernode += node * L1_CACHE_BYTES;

	pgdat_list[node] = __va(pernode);
	pernode += L1_CACHE_ALIGN(sizeof(pg_data_t));

	mem_data[node].node_data = __va(pernode);
	pernode += L1_CACHE_ALIGN(sizeof(struct ia64_node_data));
	pernode += L1_CACHE_ALIGN(sizeof(pg_data_t));

	cpu_data = per_cpu_node_setup(cpu_data, node);

	return;
}

/**
 * find_pernode_space - allocate memory for memory map and per-node structures
 * @start: physical start of range
 * @len: length of range
 * @node: node where this range resides
 *
 * This routine reserves space for the per-cpu data struct, the list of
 * pg_data_ts and the per-node data struct.  Each node will have something like
 * the following in the first chunk of addr. space large enough to hold it.
 *
 *    ________________________
 *   |                        |
 *   |~~~~~~~~~~~~~~~~~~~~~~~~| <-- NODEDATA_ALIGN(start, node) for the first
 *   |    PERCPU_PAGE_SIZE *  |     start and length big enough
 *   |    cpus_on_this_node   | Node 0 will also have entries for all non-existent cpus.
 *   |------------------------|
 *   |   local pg_data_t *    |
 *   |------------------------|
 *   |  local ia64_node_data  |
 *   |------------------------|
 *   |          ???           |
 *   |________________________|
 *
 * Once this space has been set aside, the bootmem maps are initialized.  We
 * could probably move the allocation of the per-cpu and ia64_node_data space
 * outside of this function and use alloc_bootmem_node(), but doing it here
 * is straightforward and we get the alignments we want so...
 */
static int __init find_pernode_space(unsigned long start, unsigned long len,
				     int node)
{
	unsigned long spfn, epfn;
	unsigned long pernodesize = 0, pernode;

	spfn = start >> PAGE_SHIFT;
	epfn = (start + len) >> PAGE_SHIFT;

	/*
	 * Make sure this memory falls within this node's usable memory
	 * since we may have thrown some away in build_maps().
	 */
	if (spfn < mem_data[node].min_pfn || epfn > mem_data[node].max_pfn)
		return 0;

	/* Don't setup this node's local space twice... */
	if (mem_data[node].pernode_addr)
		return 0;

	/*
	 * Calculate total size needed, incl. what's necessary
	 * for good alignment and alias prevention.
	 */
	pernodesize = compute_pernodesize(node);
	pernode = NODEDATA_ALIGN(start, node);

	/* Is this range big enough for what we want to store here? */
	if (start + len > (pernode + pernodesize))
		fill_pernode(node, pernode, pernodesize);

	return 0;
}

/**
 * reserve_pernode_space - reserve memory for per-node space
 *
 * Reserve the space used by the bootmem maps & per-node space in the boot
 * allocator so that when we actually create the real mem maps we don't
 * use their memory.
 */
static void __init reserve_pernode_space(void)
{
	unsigned long base, size;
	int node;

	for_each_online_node(node) {
		if (node_isset(node, memory_less_mask))
			continue;

		/* Now the per-node space */
		size = mem_data[node].pernode_size;
		base = __pa(mem_data[node].pernode_addr);
		memblock_reserve(base, size);
	}
}

static void scatter_node_data(void)
{
	pg_data_t **dst;
	int node;

	/*
	 * for_each_online_node() can't be used at here.
	 * node_online_map is not set for hot-added nodes at this time,
	 * because we are halfway through initialization of the new node's
	 * structures.  If for_each_online_node() is used, a new node's
	 * pg_data_ptrs will be not initialized. Instead of using it,
	 * pgdat_list[] is checked.
	 */
	for_each_node(node) {
		if (pgdat_list[node]) {
			dst = LOCAL_DATA_ADDR(pgdat_list[node])->pg_data_ptrs;
			memcpy(dst, pgdat_list, sizeof(pgdat_list));
		}
	}
}

/**
 * initialize_pernode_data - fixup per-cpu & per-node pointers
 *
 * Each node's per-node area has a copy of the global pg_data_t list, so
 * we copy that to each node here, as well as setting the per-cpu pointer
 * to the local node data structure.
 */
static void __init initialize_pernode_data(void)
{
	int cpu, node;

	scatter_node_data();

#ifdef CONFIG_SMP
	/* Set the node_data pointer for each per-cpu struct */
	for_each_possible_early_cpu(cpu) {
		node = node_cpuid[cpu].nid;
		per_cpu(ia64_cpu_info, cpu).node_data =
			mem_data[node].node_data;
	}
#else
	{
		struct cpuinfo_ia64 *cpu0_cpu_info;
		cpu = 0;
		node = node_cpuid[cpu].nid;
		cpu0_cpu_info = (struct cpuinfo_ia64 *)(__phys_per_cpu_start +
			((char *)&ia64_cpu_info - __per_cpu_start));
		cpu0_cpu_info->node_data = mem_data[node].node_data;
	}
#endif /* CONFIG_SMP */
}

/**
 * memory_less_node_alloc - * attempt to allocate memory on the best NUMA slit
 * 	node but fall back to any other node when __alloc_bootmem_node fails
 *	for best.
 * @nid: node id
 * @pernodesize: size of this node's pernode data
 */
static void __init *memory_less_node_alloc(int nid, unsigned long pernodesize)
{
	void *ptr = NULL;
	u8 best = 0xff;
	int bestnode = NUMA_NO_NODE, node, anynode = 0;

	for_each_online_node(node) {
		if (node_isset(node, memory_less_mask))
			continue;
		else if (node_distance(nid, node) < best) {
			best = node_distance(nid, node);
			bestnode = node;
		}
		anynode = node;
	}

	if (bestnode == NUMA_NO_NODE)
		bestnode = anynode;

	ptr = memblock_alloc_try_nid(pernodesize, PERCPU_PAGE_SIZE,
				     __pa(MAX_DMA_ADDRESS),
				     MEMBLOCK_ALLOC_ACCESSIBLE,
				     bestnode);
	if (!ptr)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%lx\n",
		      __func__, pernodesize, PERCPU_PAGE_SIZE, bestnode,
		      __pa(MAX_DMA_ADDRESS));

	return ptr;
}

/**
 * memory_less_nodes - allocate and initialize CPU only nodes pernode
 *	information.
 */
static void __init memory_less_nodes(void)
{
	unsigned long pernodesize;
	void *pernode;
	int node;

	for_each_node_mask(node, memory_less_mask) {
		pernodesize = compute_pernodesize(node);
		pernode = memory_less_node_alloc(node, pernodesize);
		fill_pernode(node, __pa(pernode), pernodesize);
	}

	return;
}

/**
 * find_memory - walk the EFI memory map and setup the bootmem allocator
 *
 * Called early in boot to setup the bootmem allocator, and to
 * allocate the per-cpu and per-node structures.
 */
void __init find_memory(void)
{
	int node;

	reserve_memory();
	efi_memmap_walk(filter_memory, register_active_ranges);

	if (num_online_nodes() == 0) {
		printk(KERN_ERR "node info missing!\n");
		node_set_online(0);
	}

	nodes_or(memory_less_mask, memory_less_mask, node_online_map);
	min_low_pfn = -1;
	max_low_pfn = 0;

	/* These actually end up getting called by call_pernode_memory() */
	efi_memmap_walk(filter_rsvd_memory, build_node_maps);
	efi_memmap_walk(filter_rsvd_memory, find_pernode_space);
	efi_memmap_walk(find_max_min_low_pfn, NULL);

	for_each_online_node(node)
		if (mem_data[node].min_pfn)
			node_clear(node, memory_less_mask);

	reserve_pernode_space();
	memory_less_nodes();
	initialize_pernode_data();

	max_pfn = max_low_pfn;

	find_initrd();
}

#ifdef CONFIG_SMP
/**
 * per_cpu_init - setup per-cpu variables
 *
 * find_pernode_space() does most of this already, we just need to set
 * local_per_cpu_offset
 */
void *per_cpu_init(void)
{
	int cpu;
	static int first_time = 1;

	if (first_time) {
		first_time = 0;
		for_each_possible_early_cpu(cpu)
			per_cpu(local_per_cpu_offset, cpu) = __per_cpu_offset[cpu];
	}

	return __per_cpu_start + __per_cpu_offset[smp_processor_id()];
}
#endif /* CONFIG_SMP */

/**
 * call_pernode_memory - use SRAT to call callback functions with node info
 * @start: physical start of range
 * @len: length of range
 * @arg: function to call for each range
 *
 * efi_memmap_walk() knows nothing about layout of memory across nodes. Find
 * out to which node a block of memory belongs.  Ignore memory that we cannot
 * identify, and split blocks that run across multiple nodes.
 *
 * Take this opportunity to round the start address up and the end address
 * down to page boundaries.
 */
void call_pernode_memory(unsigned long start, unsigned long len, void *arg)
{
	unsigned long rs, re, end = start + len;
	void (*func)(unsigned long, unsigned long, int);
	int i;

	start = PAGE_ALIGN(start);
	end &= PAGE_MASK;
	if (start >= end)
		return;

	func = arg;

	if (!num_node_memblks) {
		/* No SRAT table, so assume one node (node 0) */
		if (start < end)
			(*func)(start, end - start, 0);
		return;
	}

	for (i = 0; i < num_node_memblks; i++) {
		rs = max(start, node_memblk[i].start_paddr);
		re = min(end, node_memblk[i].start_paddr +
			 node_memblk[i].size);

		if (rs < re)
			(*func)(rs, re - rs, node_memblk[i].nid);

		if (re == end)
			break;
	}
}

/**
 * paging_init - setup page tables
 *
 * paging_init() sets up the page tables for each node of the system and frees
 * the bootmem allocator memory for general use.
 */
void __init paging_init(void)
{
	unsigned long max_dma;
	unsigned long pfn_offset = 0;
	unsigned long max_pfn = 0;
	int node;
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;

	sparse_init();

#ifdef CONFIG_VIRTUAL_MEM_MAP
	VMALLOC_END -= PAGE_ALIGN(ALIGN(max_low_pfn, MAX_ORDER_NR_PAGES) *
		sizeof(struct page));
	vmem_map = (struct page *) VMALLOC_END;
	efi_memmap_walk(create_mem_map_page_table, NULL);
	printk("Virtual mem_map starts at 0x%p\n", vmem_map);
#endif

	for_each_online_node(node) {
		pfn_offset = mem_data[node].min_pfn;

#ifdef CONFIG_VIRTUAL_MEM_MAP
		NODE_DATA(node)->node_mem_map = vmem_map + pfn_offset;
#endif
		if (mem_data[node].max_pfn > max_pfn)
			max_pfn = mem_data[node].max_pfn;
	}

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = max_dma;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_pfn;
	free_area_init(max_zone_pfns);

	zero_page_memmap_ptr = virt_to_page(ia64_imva(empty_zero_page));
}

#ifdef CONFIG_MEMORY_HOTPLUG
pg_data_t *arch_alloc_nodedata(int nid)
{
	unsigned long size = compute_pernodesize(nid);

	return kzalloc(size, GFP_KERNEL);
}

void arch_free_nodedata(pg_data_t *pgdat)
{
	kfree(pgdat);
}

void arch_refresh_nodedata(int update_node, pg_data_t *update_pgdat)
{
	pgdat_list[update_node] = update_pgdat;
	scatter_node_data();
}
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
int __meminit vmemmap_populate(unsigned long start, unsigned long end, int node,
		struct vmem_altmap *altmap)
{
	return vmemmap_populate_basepages(start, end, node, NULL);
}

void vmemmap_free(unsigned long start, unsigned long end,
		struct vmem_altmap *altmap)
{
}
#endif
