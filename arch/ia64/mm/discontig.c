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
#include <linux/yesdemask.h>
#include <linux/slab.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/meminit.h>
#include <asm/numa.h>
#include <asm/sections.h>

/*
 * Track per-yesde information needed to setup the boot memory allocator, the
 * per-yesde areas, and the real VM.
 */
struct early_yesde_data {
	struct ia64_yesde_data *yesde_data;
	unsigned long peryesde_addr;
	unsigned long peryesde_size;
	unsigned long min_pfn;
	unsigned long max_pfn;
};

static struct early_yesde_data mem_data[MAX_NUMNODES] __initdata;
static yesdemask_t memory_less_mask __initdata;

pg_data_t *pgdat_list[MAX_NUMNODES];

/*
 * To prevent cache aliasing effects, align per-yesde structures so that they
 * start at addresses that are strided by yesde number.
 */
#define MAX_NODE_ALIGN_OFFSET	(32 * 1024 * 1024)
#define NODEDATA_ALIGN(addr, yesde)						\
	((((addr) + 1024*1024-1) & ~(1024*1024-1)) + 				\
	     (((yesde)*PERCPU_PAGE_SIZE) & (MAX_NODE_ALIGN_OFFSET - 1)))

/**
 * build_yesde_maps - callback to setup mem_data structs for each yesde
 * @start: physical start of range
 * @len: length of range
 * @yesde: yesde where this range resides
 *
 * Detect extents of each piece of memory that we wish to
 * treat as a virtually contiguous block (i.e. each yesde). Each such block
 * must start on an %IA64_GRANULE_SIZE boundary, so we round the address down
 * if necessary.  Any yesn-existent pages will simply be part of the virtual
 * memmap.
 */
static int __init build_yesde_maps(unsigned long start, unsigned long len,
				  int yesde)
{
	unsigned long spfn, epfn, end = start + len;

	epfn = GRANULEROUNDUP(end) >> PAGE_SHIFT;
	spfn = GRANULEROUNDDOWN(start) >> PAGE_SHIFT;

	if (!mem_data[yesde].min_pfn) {
		mem_data[yesde].min_pfn = spfn;
		mem_data[yesde].max_pfn = epfn;
	} else {
		mem_data[yesde].min_pfn = min(spfn, mem_data[yesde].min_pfn);
		mem_data[yesde].max_pfn = max(epfn, mem_data[yesde].max_pfn);
	}

	return 0;
}

/**
 * early_nr_cpus_yesde - return number of cpus on a given yesde
 * @yesde: yesde to check
 *
 * Count the number of cpus on @yesde.  We can't use nr_cpus_yesde() yet because
 * acpi_boot_init() (which builds the yesde_to_cpu_mask array) hasn't been
 * called yet.  Note that yesde 0 will also count all yesn-existent cpus.
 */
static int __meminit early_nr_cpus_yesde(int yesde)
{
	int cpu, n = 0;

	for_each_possible_early_cpu(cpu)
		if (yesde == yesde_cpuid[cpu].nid)
			n++;

	return n;
}

/**
 * compute_peryesdesize - compute size of peryesde data
 * @yesde: the yesde id.
 */
static unsigned long __meminit compute_peryesdesize(int yesde)
{
	unsigned long peryesdesize = 0, cpus;

	cpus = early_nr_cpus_yesde(yesde);
	peryesdesize += PERCPU_PAGE_SIZE * cpus;
	peryesdesize += yesde * L1_CACHE_BYTES;
	peryesdesize += L1_CACHE_ALIGN(sizeof(pg_data_t));
	peryesdesize += L1_CACHE_ALIGN(sizeof(struct ia64_yesde_data));
	peryesdesize += L1_CACHE_ALIGN(sizeof(pg_data_t));
	peryesdesize = PAGE_ALIGN(peryesdesize);
	return peryesdesize;
}

/**
 * per_cpu_yesde_setup - setup per-cpu areas on each yesde
 * @cpu_data: per-cpu area on this yesde
 * @yesde: yesde to setup
 *
 * Copy the static per-cpu data into the region we just set aside and then
 * setup __per_cpu_offset for each CPU on this yesde.  Return a pointer to
 * the end of the area.
 */
static void *per_cpu_yesde_setup(void *cpu_data, int yesde)
{
#ifdef CONFIG_SMP
	int cpu;

	for_each_possible_early_cpu(cpu) {
		void *src = cpu == 0 ? __cpu0_per_cpu : __phys_per_cpu_start;

		if (yesde != yesde_cpuid[cpu].nid)
			continue;

		memcpy(__va(cpu_data), src, __per_cpu_end - __per_cpu_start);
		__per_cpu_offset[cpu] = (char *)__va(cpu_data) -
			__per_cpu_start;

		/*
		 * percpu area for cpu0 is moved from the __init area
		 * which is setup by head.S and used till this point.
		 * Update ar.k3.  This move is ensures that percpu
		 * area for cpu0 is on the correct yesde and its
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
	struct pcpu_group_info *uninitialized_var(gi);
	unsigned int *cpu_map;
	void *base;
	unsigned long base_offset;
	unsigned int cpu;
	ssize_t static_size, reserved_size, dyn_size;
	int yesde, prev_yesde, unit, nr_units;

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

	/* build cpu_map, units are grouped by yesde */
	unit = 0;
	for_each_yesde(yesde)
		for_each_possible_cpu(cpu)
			if (yesde == yesde_cpuid[cpu].nid)
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
	 * CPUs are put into groups according to yesde.  Walk cpu_map
	 * and create new groups at yesde boundaries.
	 */
	prev_yesde = NUMA_NO_NODE;
	ai->nr_groups = 0;
	for (unit = 0; unit < nr_units; unit++) {
		cpu = cpu_map[unit];
		yesde = yesde_cpuid[cpu].nid;

		if (yesde == prev_yesde) {
			gi->nr_units++;
			continue;
		}
		prev_yesde = yesde;

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
 * fill_peryesde - initialize peryesde data.
 * @yesde: the yesde id.
 * @peryesde: physical address of peryesde data
 * @peryesdesize: size of the peryesde data
 */
static void __init fill_peryesde(int yesde, unsigned long peryesde,
	unsigned long peryesdesize)
{
	void *cpu_data;
	int cpus = early_nr_cpus_yesde(yesde);

	mem_data[yesde].peryesde_addr = peryesde;
	mem_data[yesde].peryesde_size = peryesdesize;
	memset(__va(peryesde), 0, peryesdesize);

	cpu_data = (void *)peryesde;
	peryesde += PERCPU_PAGE_SIZE * cpus;
	peryesde += yesde * L1_CACHE_BYTES;

	pgdat_list[yesde] = __va(peryesde);
	peryesde += L1_CACHE_ALIGN(sizeof(pg_data_t));

	mem_data[yesde].yesde_data = __va(peryesde);
	peryesde += L1_CACHE_ALIGN(sizeof(struct ia64_yesde_data));
	peryesde += L1_CACHE_ALIGN(sizeof(pg_data_t));

	cpu_data = per_cpu_yesde_setup(cpu_data, yesde);

	return;
}

/**
 * find_peryesde_space - allocate memory for memory map and per-yesde structures
 * @start: physical start of range
 * @len: length of range
 * @yesde: yesde where this range resides
 *
 * This routine reserves space for the per-cpu data struct, the list of
 * pg_data_ts and the per-yesde data struct.  Each yesde will have something like
 * the following in the first chunk of addr. space large eyesugh to hold it.
 *
 *    ________________________
 *   |                        |
 *   |~~~~~~~~~~~~~~~~~~~~~~~~| <-- NODEDATA_ALIGN(start, yesde) for the first
 *   |    PERCPU_PAGE_SIZE *  |     start and length big eyesugh
 *   |    cpus_on_this_yesde   | Node 0 will also have entries for all yesn-existent cpus.
 *   |------------------------|
 *   |   local pg_data_t *    |
 *   |------------------------|
 *   |  local ia64_yesde_data  |
 *   |------------------------|
 *   |          ???           |
 *   |________________________|
 *
 * Once this space has been set aside, the bootmem maps are initialized.  We
 * could probably move the allocation of the per-cpu and ia64_yesde_data space
 * outside of this function and use alloc_bootmem_yesde(), but doing it here
 * is straightforward and we get the alignments we want so...
 */
static int __init find_peryesde_space(unsigned long start, unsigned long len,
				     int yesde)
{
	unsigned long spfn, epfn;
	unsigned long peryesdesize = 0, peryesde;

	spfn = start >> PAGE_SHIFT;
	epfn = (start + len) >> PAGE_SHIFT;

	/*
	 * Make sure this memory falls within this yesde's usable memory
	 * since we may have thrown some away in build_maps().
	 */
	if (spfn < mem_data[yesde].min_pfn || epfn > mem_data[yesde].max_pfn)
		return 0;

	/* Don't setup this yesde's local space twice... */
	if (mem_data[yesde].peryesde_addr)
		return 0;

	/*
	 * Calculate total size needed, incl. what's necessary
	 * for good alignment and alias prevention.
	 */
	peryesdesize = compute_peryesdesize(yesde);
	peryesde = NODEDATA_ALIGN(start, yesde);

	/* Is this range big eyesugh for what we want to store here? */
	if (start + len > (peryesde + peryesdesize))
		fill_peryesde(yesde, peryesde, peryesdesize);

	return 0;
}

/**
 * reserve_peryesde_space - reserve memory for per-yesde space
 *
 * Reserve the space used by the bootmem maps & per-yesde space in the boot
 * allocator so that when we actually create the real mem maps we don't
 * use their memory.
 */
static void __init reserve_peryesde_space(void)
{
	unsigned long base, size;
	int yesde;

	for_each_online_yesde(yesde) {
		if (yesde_isset(yesde, memory_less_mask))
			continue;

		/* Now the per-yesde space */
		size = mem_data[yesde].peryesde_size;
		base = __pa(mem_data[yesde].peryesde_addr);
		memblock_reserve(base, size);
	}
}

static void __meminit scatter_yesde_data(void)
{
	pg_data_t **dst;
	int yesde;

	/*
	 * for_each_online_yesde() can't be used at here.
	 * yesde_online_map is yest set for hot-added yesdes at this time,
	 * because we are halfway through initialization of the new yesde's
	 * structures.  If for_each_online_yesde() is used, a new yesde's
	 * pg_data_ptrs will be yest initialized. Instead of using it,
	 * pgdat_list[] is checked.
	 */
	for_each_yesde(yesde) {
		if (pgdat_list[yesde]) {
			dst = LOCAL_DATA_ADDR(pgdat_list[yesde])->pg_data_ptrs;
			memcpy(dst, pgdat_list, sizeof(pgdat_list));
		}
	}
}

/**
 * initialize_peryesde_data - fixup per-cpu & per-yesde pointers
 *
 * Each yesde's per-yesde area has a copy of the global pg_data_t list, so
 * we copy that to each yesde here, as well as setting the per-cpu pointer
 * to the local yesde data structure.
 */
static void __init initialize_peryesde_data(void)
{
	int cpu, yesde;

	scatter_yesde_data();

#ifdef CONFIG_SMP
	/* Set the yesde_data pointer for each per-cpu struct */
	for_each_possible_early_cpu(cpu) {
		yesde = yesde_cpuid[cpu].nid;
		per_cpu(ia64_cpu_info, cpu).yesde_data =
			mem_data[yesde].yesde_data;
	}
#else
	{
		struct cpuinfo_ia64 *cpu0_cpu_info;
		cpu = 0;
		yesde = yesde_cpuid[cpu].nid;
		cpu0_cpu_info = (struct cpuinfo_ia64 *)(__phys_per_cpu_start +
			((char *)&ia64_cpu_info - __per_cpu_start));
		cpu0_cpu_info->yesde_data = mem_data[yesde].yesde_data;
	}
#endif /* CONFIG_SMP */
}

/**
 * memory_less_yesde_alloc - * attempt to allocate memory on the best NUMA slit
 * 	yesde but fall back to any other yesde when __alloc_bootmem_yesde fails
 *	for best.
 * @nid: yesde id
 * @peryesdesize: size of this yesde's peryesde data
 */
static void __init *memory_less_yesde_alloc(int nid, unsigned long peryesdesize)
{
	void *ptr = NULL;
	u8 best = 0xff;
	int bestyesde = NUMA_NO_NODE, yesde, anyyesde = 0;

	for_each_online_yesde(yesde) {
		if (yesde_isset(yesde, memory_less_mask))
			continue;
		else if (yesde_distance(nid, yesde) < best) {
			best = yesde_distance(nid, yesde);
			bestyesde = yesde;
		}
		anyyesde = yesde;
	}

	if (bestyesde == NUMA_NO_NODE)
		bestyesde = anyyesde;

	ptr = memblock_alloc_try_nid(peryesdesize, PERCPU_PAGE_SIZE,
				     __pa(MAX_DMA_ADDRESS),
				     MEMBLOCK_ALLOC_ACCESSIBLE,
				     bestyesde);
	if (!ptr)
		panic("%s: Failed to allocate %lu bytes align=0x%lx nid=%d from=%lx\n",
		      __func__, peryesdesize, PERCPU_PAGE_SIZE, bestyesde,
		      __pa(MAX_DMA_ADDRESS));

	return ptr;
}

/**
 * memory_less_yesdes - allocate and initialize CPU only yesdes peryesde
 *	information.
 */
static void __init memory_less_yesdes(void)
{
	unsigned long peryesdesize;
	void *peryesde;
	int yesde;

	for_each_yesde_mask(yesde, memory_less_mask) {
		peryesdesize = compute_peryesdesize(yesde);
		peryesde = memory_less_yesde_alloc(yesde, peryesdesize);
		fill_peryesde(yesde, __pa(peryesde), peryesdesize);
	}

	return;
}

/**
 * find_memory - walk the EFI memory map and setup the bootmem allocator
 *
 * Called early in boot to setup the bootmem allocator, and to
 * allocate the per-cpu and per-yesde structures.
 */
void __init find_memory(void)
{
	int yesde;

	reserve_memory();
	efi_memmap_walk(filter_memory, register_active_ranges);

	if (num_online_yesdes() == 0) {
		printk(KERN_ERR "yesde info missing!\n");
		yesde_set_online(0);
	}

	yesdes_or(memory_less_mask, memory_less_mask, yesde_online_map);
	min_low_pfn = -1;
	max_low_pfn = 0;

	/* These actually end up getting called by call_peryesde_memory() */
	efi_memmap_walk(filter_rsvd_memory, build_yesde_maps);
	efi_memmap_walk(filter_rsvd_memory, find_peryesde_space);
	efi_memmap_walk(find_max_min_low_pfn, NULL);

	for_each_online_yesde(yesde)
		if (mem_data[yesde].min_pfn)
			yesde_clear(yesde, memory_less_mask);

	reserve_peryesde_space();
	memory_less_yesdes();
	initialize_peryesde_data();

	max_pfn = max_low_pfn;

	find_initrd();
}

#ifdef CONFIG_SMP
/**
 * per_cpu_init - setup per-cpu variables
 *
 * find_peryesde_space() does most of this already, we just need to set
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
 * call_peryesde_memory - use SRAT to call callback functions with yesde info
 * @start: physical start of range
 * @len: length of range
 * @arg: function to call for each range
 *
 * efi_memmap_walk() kyesws yesthing about layout of memory across yesdes. Find
 * out to which yesde a block of memory belongs.  Igyesre memory that we canyest
 * identify, and split blocks that run across multiple yesdes.
 *
 * Take this opportunity to round the start address up and the end address
 * down to page boundaries.
 */
void call_peryesde_memory(unsigned long start, unsigned long len, void *arg)
{
	unsigned long rs, re, end = start + len;
	void (*func)(unsigned long, unsigned long, int);
	int i;

	start = PAGE_ALIGN(start);
	end &= PAGE_MASK;
	if (start >= end)
		return;

	func = arg;

	if (!num_yesde_memblks) {
		/* No SRAT table, so assume one yesde (yesde 0) */
		if (start < end)
			(*func)(start, end - start, 0);
		return;
	}

	for (i = 0; i < num_yesde_memblks; i++) {
		rs = max(start, yesde_memblk[i].start_paddr);
		re = min(end, yesde_memblk[i].start_paddr +
			 yesde_memblk[i].size);

		if (rs < re)
			(*func)(rs, re - rs, yesde_memblk[i].nid);

		if (re == end)
			break;
	}
}

/**
 * paging_init - setup page tables
 *
 * paging_init() sets up the page tables for each yesde of the system and frees
 * the bootmem allocator memory for general use.
 */
void __init paging_init(void)
{
	unsigned long max_dma;
	unsigned long pfn_offset = 0;
	unsigned long max_pfn = 0;
	int yesde;
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;

	sparse_memory_present_with_active_regions(MAX_NUMNODES);
	sparse_init();

#ifdef CONFIG_VIRTUAL_MEM_MAP
	VMALLOC_END -= PAGE_ALIGN(ALIGN(max_low_pfn, MAX_ORDER_NR_PAGES) *
		sizeof(struct page));
	vmem_map = (struct page *) VMALLOC_END;
	efi_memmap_walk(create_mem_map_page_table, NULL);
	printk("Virtual mem_map starts at 0x%p\n", vmem_map);
#endif

	for_each_online_yesde(yesde) {
		pfn_offset = mem_data[yesde].min_pfn;

#ifdef CONFIG_VIRTUAL_MEM_MAP
		NODE_DATA(yesde)->yesde_mem_map = vmem_map + pfn_offset;
#endif
		if (mem_data[yesde].max_pfn > max_pfn)
			max_pfn = mem_data[yesde].max_pfn;
	}

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_ZONE_DMA32
	max_zone_pfns[ZONE_DMA32] = max_dma;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_pfn;
	free_area_init_yesdes(max_zone_pfns);

	zero_page_memmap_ptr = virt_to_page(ia64_imva(empty_zero_page));
}

#ifdef CONFIG_MEMORY_HOTPLUG
pg_data_t *arch_alloc_yesdedata(int nid)
{
	unsigned long size = compute_peryesdesize(nid);

	return kzalloc(size, GFP_KERNEL);
}

void arch_free_yesdedata(pg_data_t *pgdat)
{
	kfree(pgdat);
}

void arch_refresh_yesdedata(int update_yesde, pg_data_t *update_pgdat)
{
	pgdat_list[update_yesde] = update_pgdat;
	scatter_yesde_data();
}
#endif

#ifdef CONFIG_SPARSEMEM_VMEMMAP
int __meminit vmemmap_populate(unsigned long start, unsigned long end, int yesde,
		struct vmem_altmap *altmap)
{
	return vmemmap_populate_basepages(start, end, yesde);
}

void vmemmap_free(unsigned long start, unsigned long end,
		struct vmem_altmap *altmap)
{
}
#endif
