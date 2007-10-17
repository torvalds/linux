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
#include <linux/bootmem.h>
#include <linux/acpi.h>
#include <linux/efi.h>
#include <linux/nodemask.h>
#include <asm/pgalloc.h>
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
	struct bootmem_data bootmem_data;
	unsigned long num_physpages;
#ifdef CONFIG_ZONE_DMA
	unsigned long num_dma_physpages;
#endif
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
 * build_node_maps - callback to setup bootmem structs for each node
 * @start: physical start of range
 * @len: length of range
 * @node: node where this range resides
 *
 * We allocate a struct bootmem_data for each piece of memory that we wish to
 * treat as a virtually contiguous block (i.e. each node). Each such block
 * must start on an %IA64_GRANULE_SIZE boundary, so we round the address down
 * if necessary.  Any non-existent pages will simply be part of the virtual
 * memmap.  We also update min_low_pfn and max_low_pfn here as we receive
 * memory ranges from the caller.
 */
static int __init build_node_maps(unsigned long start, unsigned long len,
				  int node)
{
	unsigned long cstart, epfn, end = start + len;
	struct bootmem_data *bdp = &mem_data[node].bootmem_data;

	epfn = GRANULEROUNDUP(end) >> PAGE_SHIFT;
	cstart = GRANULEROUNDDOWN(start);

	if (!bdp->node_low_pfn) {
		bdp->node_boot_start = cstart;
		bdp->node_low_pfn = epfn;
	} else {
		bdp->node_boot_start = min(cstart, bdp->node_boot_start);
		bdp->node_low_pfn = max(epfn, bdp->node_low_pfn);
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
static int __meminit early_nr_cpus_node(int node)
{
	int cpu, n = 0;

	for (cpu = 0; cpu < NR_CPUS; cpu++)
		if (node == node_cpuid[cpu].nid)
			n++;

	return n;
}

/**
 * compute_pernodesize - compute size of pernode data
 * @node: the node id.
 */
static unsigned long __meminit compute_pernodesize(int node)
{
	unsigned long pernodesize = 0, cpus;

	cpus = early_nr_cpus_node(node);
	pernodesize += PERCPU_PAGE_SIZE * cpus;
	pernodesize += node * L1_CACHE_BYTES;
	pernodesize += L1_CACHE_ALIGN(sizeof(pg_data_t));
	pernodesize += L1_CACHE_ALIGN(sizeof(struct ia64_node_data));
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

	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		if (node == node_cpuid[cpu].nid) {
			memcpy(__va(cpu_data), __phys_per_cpu_start,
			       __per_cpu_end - __per_cpu_start);
			__per_cpu_offset[cpu] = (char*)__va(cpu_data) -
				__per_cpu_start;
			cpu_data += PERCPU_PAGE_SIZE;
		}
	}
#endif
	return cpu_data;
}

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
	struct bootmem_data *bdp = &mem_data[node].bootmem_data;

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

	pgdat_list[node]->bdata = bdp;
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
	unsigned long epfn;
	unsigned long pernodesize = 0, pernode, pages, mapsize;
	struct bootmem_data *bdp = &mem_data[node].bootmem_data;

	epfn = (start + len) >> PAGE_SHIFT;

	pages = bdp->node_low_pfn - (bdp->node_boot_start >> PAGE_SHIFT);
	mapsize = bootmem_bootmap_pages(pages) << PAGE_SHIFT;

	/*
	 * Make sure this memory falls within this node's usable memory
	 * since we may have thrown some away in build_maps().
	 */
	if (start < bdp->node_boot_start || epfn > bdp->node_low_pfn)
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
	if (start + len > (pernode + pernodesize + mapsize))
		fill_pernode(node, pernode, pernodesize);

	return 0;
}

/**
 * free_node_bootmem - free bootmem allocator memory for use
 * @start: physical start of range
 * @len: length of range
 * @node: node where this range resides
 *
 * Simply calls the bootmem allocator to free the specified ranged from
 * the given pg_data_t's bdata struct.  After this function has been called
 * for all the entries in the EFI memory map, the bootmem allocator will
 * be ready to service allocation requests.
 */
static int __init free_node_bootmem(unsigned long start, unsigned long len,
				    int node)
{
	free_bootmem_node(pgdat_list[node], start, len);

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
	unsigned long base, size, pages;
	struct bootmem_data *bdp;
	int node;

	for_each_online_node(node) {
		pg_data_t *pdp = pgdat_list[node];

		if (node_isset(node, memory_less_mask))
			continue;

		bdp = pdp->bdata;

		/* First the bootmem_map itself */
		pages = bdp->node_low_pfn - (bdp->node_boot_start>>PAGE_SHIFT);
		size = bootmem_bootmap_pages(pages) << PAGE_SHIFT;
		base = __pa(bdp->node_bootmem_map);
		reserve_bootmem_node(pdp, base, size);

		/* Now the per-node space */
		size = mem_data[node].pernode_size;
		base = __pa(mem_data[node].pernode_addr);
		reserve_bootmem_node(pdp, base, size);
	}
}

static void __meminit scatter_node_data(void)
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
 * to the local node data structure.  The active_cpus field of the per-node
 * structure gets setup by the platform_cpu_init() function later.
 */
static void __init initialize_pernode_data(void)
{
	int cpu, node;

	scatter_node_data();

#ifdef CONFIG_SMP
	/* Set the node_data pointer for each per-cpu struct */
	for (cpu = 0; cpu < NR_CPUS; cpu++) {
		node = node_cpuid[cpu].nid;
		per_cpu(cpu_info, cpu).node_data = mem_data[node].node_data;
	}
#else
	{
		struct cpuinfo_ia64 *cpu0_cpu_info;
		cpu = 0;
		node = node_cpuid[cpu].nid;
		cpu0_cpu_info = (struct cpuinfo_ia64 *)(__phys_per_cpu_start +
			((char *)&per_cpu__cpu_info - __per_cpu_start));
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
	int bestnode = -1, node, anynode = 0;

	for_each_online_node(node) {
		if (node_isset(node, memory_less_mask))
			continue;
		else if (node_distance(nid, node) < best) {
			best = node_distance(nid, node);
			bestnode = node;
		}
		anynode = node;
	}

	if (bestnode == -1)
		bestnode = anynode;

	ptr = __alloc_bootmem_node(pgdat_list[bestnode], pernodesize,
		PERCPU_PAGE_SIZE, __pa(MAX_DMA_ADDRESS));

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
		if (mem_data[node].bootmem_data.node_low_pfn) {
			node_clear(node, memory_less_mask);
			mem_data[node].min_pfn = ~0UL;
		}

	efi_memmap_walk(register_active_ranges, NULL);

	/*
	 * Initialize the boot memory maps in reverse order since that's
	 * what the bootmem allocator expects
	 */
	for (node = MAX_NUMNODES - 1; node >= 0; node--) {
		unsigned long pernode, pernodesize, map;
		struct bootmem_data *bdp;

		if (!node_online(node))
			continue;
		else if (node_isset(node, memory_less_mask))
			continue;

		bdp = &mem_data[node].bootmem_data;
		pernode = mem_data[node].pernode_addr;
		pernodesize = mem_data[node].pernode_size;
		map = pernode + pernodesize;

		init_bootmem_node(pgdat_list[node],
				  map>>PAGE_SHIFT,
				  bdp->node_boot_start>>PAGE_SHIFT,
				  bdp->node_low_pfn);
	}

	efi_memmap_walk(filter_rsvd_memory, free_node_bootmem);

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
void __cpuinit *per_cpu_init(void)
{
	int cpu;
	static int first_time = 1;


	if (smp_processor_id() != 0)
		return __per_cpu_start + __per_cpu_offset[smp_processor_id()];

	if (first_time) {
		first_time = 0;
		for (cpu = 0; cpu < NR_CPUS; cpu++)
			per_cpu(local_per_cpu_offset, cpu) = __per_cpu_offset[cpu];
	}

	return __per_cpu_start + __per_cpu_offset[smp_processor_id()];
}
#endif /* CONFIG_SMP */

/**
 * show_mem - give short summary of memory stats
 *
 * Shows a simple page count of reserved and used pages in the system.
 * For discontig machines, it does this on a per-pgdat basis.
 */
void show_mem(void)
{
	int i, total_reserved = 0;
	int total_shared = 0, total_cached = 0;
	unsigned long total_present = 0;
	pg_data_t *pgdat;

	printk(KERN_INFO "Mem-info:\n");
	show_free_areas();
	printk(KERN_INFO "Free swap:       %6ldkB\n",
	       nr_swap_pages<<(PAGE_SHIFT-10));
	printk(KERN_INFO "Node memory in pages:\n");
	for_each_online_pgdat(pgdat) {
		unsigned long present;
		unsigned long flags;
		int shared = 0, cached = 0, reserved = 0;

		pgdat_resize_lock(pgdat, &flags);
		present = pgdat->node_present_pages;
		for(i = 0; i < pgdat->node_spanned_pages; i++) {
			struct page *page;
			if (unlikely(i % MAX_ORDER_NR_PAGES == 0))
				touch_nmi_watchdog();
			if (pfn_valid(pgdat->node_start_pfn + i))
				page = pfn_to_page(pgdat->node_start_pfn + i);
			else {
				i = vmemmap_find_next_valid_pfn(pgdat->node_id,
					 i) - 1;
				continue;
			}
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page)-1;
		}
		pgdat_resize_unlock(pgdat, &flags);
		total_present += present;
		total_reserved += reserved;
		total_cached += cached;
		total_shared += shared;
		printk(KERN_INFO "Node %4d:  RAM: %11ld, rsvd: %8d, "
		       "shrd: %10d, swpd: %10d\n", pgdat->node_id,
		       present, reserved, shared, cached);
	}
	printk(KERN_INFO "%ld pages of RAM\n", total_present);
	printk(KERN_INFO "%d reserved pages\n", total_reserved);
	printk(KERN_INFO "%d pages shared\n", total_shared);
	printk(KERN_INFO "%d pages swap cached\n", total_cached);
	printk(KERN_INFO "Total of %ld pages in page table cache\n",
	       quicklist_total_size());
	printk(KERN_INFO "%d free buffer pages\n", nr_free_buffer_pages());
}

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
 * count_node_pages - callback to build per-node memory info structures
 * @start: physical start of range
 * @len: length of range
 * @node: node where this range resides
 *
 * Each node has it's own number of physical pages, DMAable pages, start, and
 * end page frame number.  This routine will be called by call_pernode_memory()
 * for each piece of usable memory and will setup these values for each node.
 * Very similar to build_maps().
 */
static __init int count_node_pages(unsigned long start, unsigned long len, int node)
{
	unsigned long end = start + len;

	mem_data[node].num_physpages += len >> PAGE_SHIFT;
#ifdef CONFIG_ZONE_DMA
	if (start <= __pa(MAX_DMA_ADDRESS))
		mem_data[node].num_dma_physpages +=
			(min(end, __pa(MAX_DMA_ADDRESS)) - start) >>PAGE_SHIFT;
#endif
	start = GRANULEROUNDDOWN(start);
	start = ORDERROUNDDOWN(start);
	end = GRANULEROUNDUP(end);
	mem_data[node].max_pfn = max(mem_data[node].max_pfn,
				     end >> PAGE_SHIFT);
	mem_data[node].min_pfn = min(mem_data[node].min_pfn,
				     start >> PAGE_SHIFT);

	return 0;
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

	efi_memmap_walk(filter_rsvd_memory, count_node_pages);

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();

#ifdef CONFIG_VIRTUAL_MEM_MAP
	vmalloc_end -= PAGE_ALIGN(ALIGN(max_low_pfn, MAX_ORDER_NR_PAGES) *
		sizeof(struct page));
	vmem_map = (struct page *) vmalloc_end;
	efi_memmap_walk(create_mem_map_page_table, NULL);
	printk("Virtual mem_map starts at 0x%p\n", vmem_map);
#endif

	for_each_online_node(node) {
		num_physpages += mem_data[node].num_physpages;
		pfn_offset = mem_data[node].min_pfn;

#ifdef CONFIG_VIRTUAL_MEM_MAP
		NODE_DATA(node)->node_mem_map = vmem_map + pfn_offset;
#endif
		if (mem_data[node].max_pfn > max_pfn)
			max_pfn = mem_data[node].max_pfn;
	}

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_ZONE_DMA
	max_zone_pfns[ZONE_DMA] = max_dma;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_pfn;
	free_area_init_nodes(max_zone_pfns);

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
int __meminit vmemmap_populate(struct page *start_page,
						unsigned long size, int node)
{
	return vmemmap_populate_basepages(start_page, size, node);
}
#endif
