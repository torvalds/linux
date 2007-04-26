/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1998-2003 Hewlett-Packard Co
 *	David Mosberger-Tang <davidm@hpl.hp.com>
 *	Stephane Eranian <eranian@hpl.hp.com>
 * Copyright (C) 2000, Rohit Seth <rohit.seth@intel.com>
 * Copyright (C) 1999 VA Linux Systems
 * Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
 * Copyright (C) 2003 Silicon Graphics, Inc. All rights reserved.
 *
 * Routines used by ia64 machines with contiguous (or virtually contiguous)
 * memory.
 */
#include <linux/bootmem.h>
#include <linux/efi.h>
#include <linux/mm.h>
#include <linux/swap.h>

#include <asm/meminit.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/mca.h>

#ifdef CONFIG_VIRTUAL_MEM_MAP
static unsigned long max_gap;
#endif

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
			if (pfn_valid(pgdat->node_start_pfn + i))
				page = pfn_to_page(pgdat->node_start_pfn + i);
			else {
#ifdef CONFIG_VIRTUAL_MEM_MAP
				if (max_gap < LARGE_GAP)
					continue;
#endif
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
	       pgtable_quicklist_total_size());
	printk(KERN_INFO "%d free buffer pages\n", nr_free_buffer_pages());
}


/* physical address where the bootmem map is located */
unsigned long bootmap_start;

/**
 * find_bootmap_location - callback to find a memory area for the bootmap
 * @start: start of region
 * @end: end of region
 * @arg: unused callback data
 *
 * Find a place to put the bootmap and return its starting address in
 * bootmap_start.  This address must be page-aligned.
 */
static int __init
find_bootmap_location (unsigned long start, unsigned long end, void *arg)
{
	unsigned long needed = *(unsigned long *)arg;
	unsigned long range_start, range_end, free_start;
	int i;

#if IGNORE_PFN0
	if (start == PAGE_OFFSET) {
		start += PAGE_SIZE;
		if (start >= end)
			return 0;
	}
#endif

	free_start = PAGE_OFFSET;

	for (i = 0; i < num_rsvd_regions; i++) {
		range_start = max(start, free_start);
		range_end   = min(end, rsvd_region[i].start & PAGE_MASK);

		free_start = PAGE_ALIGN(rsvd_region[i].end);

		if (range_end <= range_start)
			continue; /* skip over empty range */

		if (range_end - range_start >= needed) {
			bootmap_start = __pa(range_start);
			return -1;	/* done */
		}

		/* nothing more available in this segment */
		if (range_end == end)
			return 0;
	}
	return 0;
}

/**
 * find_memory - setup memory map
 *
 * Walk the EFI memory map and find usable memory for the system, taking
 * into account reserved areas.
 */
void __init
find_memory (void)
{
	unsigned long bootmap_size;

	reserve_memory();

	/* first find highest page frame number */
	min_low_pfn = ~0UL;
	max_low_pfn = 0;
	efi_memmap_walk(find_max_min_low_pfn, NULL);
	max_pfn = max_low_pfn;
	/* how many bytes to cover all the pages */
	bootmap_size = bootmem_bootmap_pages(max_pfn) << PAGE_SHIFT;

	/* look for a location to hold the bootmap */
	bootmap_start = ~0UL;
	efi_memmap_walk(find_bootmap_location, &bootmap_size);
	if (bootmap_start == ~0UL)
		panic("Cannot find %ld bytes for bootmap\n", bootmap_size);

	bootmap_size = init_bootmem_node(NODE_DATA(0),
			(bootmap_start >> PAGE_SHIFT), 0, max_pfn);

	/* Free all available memory, then mark bootmem-map as being in use. */
	efi_memmap_walk(filter_rsvd_memory, free_bootmem);
	reserve_bootmem(bootmap_start, bootmap_size);

	find_initrd();

}

#ifdef CONFIG_SMP
/**
 * per_cpu_init - setup per-cpu variables
 *
 * Allocate and setup per-cpu data areas.
 */
void * __cpuinit
per_cpu_init (void)
{
	void *cpu_data;
	int cpu;
	static int first_time=1;

	/*
	 * get_free_pages() cannot be used before cpu_init() done.  BSP
	 * allocates "NR_CPUS" pages for all CPUs to avoid that AP calls
	 * get_zeroed_page().
	 */
	if (first_time) {
		first_time=0;
		cpu_data = __alloc_bootmem(PERCPU_PAGE_SIZE * NR_CPUS,
					   PERCPU_PAGE_SIZE, __pa(MAX_DMA_ADDRESS));
		for (cpu = 0; cpu < NR_CPUS; cpu++) {
			memcpy(cpu_data, __phys_per_cpu_start, __per_cpu_end - __per_cpu_start);
			__per_cpu_offset[cpu] = (char *) cpu_data - __per_cpu_start;
			cpu_data += PERCPU_PAGE_SIZE;
			per_cpu(local_per_cpu_offset, cpu) = __per_cpu_offset[cpu];
		}
	}
	return __per_cpu_start + __per_cpu_offset[smp_processor_id()];
}
#endif /* CONFIG_SMP */

static int
count_pages (u64 start, u64 end, void *arg)
{
	unsigned long *count = arg;

	*count += (end - start) >> PAGE_SHIFT;
	return 0;
}

/*
 * Set up the page tables.
 */

void __init
paging_init (void)
{
	unsigned long max_dma;
	unsigned long max_zone_pfns[MAX_NR_ZONES];

	num_physpages = 0;
	efi_memmap_walk(count_pages, &num_physpages);

	memset(max_zone_pfns, 0, sizeof(max_zone_pfns));
#ifdef CONFIG_ZONE_DMA
	max_dma = virt_to_phys((void *) MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	max_zone_pfns[ZONE_DMA] = max_dma;
#endif
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;

#ifdef CONFIG_VIRTUAL_MEM_MAP
	efi_memmap_walk(register_active_ranges, NULL);
	efi_memmap_walk(find_largest_hole, (u64 *)&max_gap);
	if (max_gap < LARGE_GAP) {
		vmem_map = (struct page *) 0;
		free_area_init_nodes(max_zone_pfns);
	} else {
		unsigned long map_size;

		/* allocate virtual_mem_map */

		map_size = PAGE_ALIGN(ALIGN(max_low_pfn, MAX_ORDER_NR_PAGES) *
			sizeof(struct page));
		vmalloc_end -= map_size;
		vmem_map = (struct page *) vmalloc_end;
		efi_memmap_walk(create_mem_map_page_table, NULL);

		/*
		 * alloc_node_mem_map makes an adjustment for mem_map
		 * which isn't compatible with vmem_map.
		 */
		NODE_DATA(0)->node_mem_map = vmem_map +
			find_min_pfn_with_active_regions();
		free_area_init_nodes(max_zone_pfns);

		printk("Virtual mem_map starts at 0x%p\n", mem_map);
	}
#else /* !CONFIG_VIRTUAL_MEM_MAP */
	add_active_range(0, 0, max_low_pfn);
	free_area_init_nodes(max_zone_pfns);
#endif /* !CONFIG_VIRTUAL_MEM_MAP */
	zero_page_memmap_ptr = virt_to_page(ia64_imva(empty_zero_page));
}
