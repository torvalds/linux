/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>

#include <asm/mach-types.h>
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/tlb.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#define TABLE_SIZE	(2 * PTRS_PER_PTE * sizeof(pte_t))

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern void _stext, _text, _etext, __data_start, _end, __init_begin, __init_end;
extern unsigned long phys_initrd_start;
extern unsigned long phys_initrd_size;

/*
 * The sole use of this is to pass memory configuration
 * data from paging_init to mem_init.
 */
static struct meminfo meminfo __initdata = { 0, };

/*
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;

void show_mem(void)
{
	int free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0, slab = 0, node;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));

	for_each_online_node(node) {
		struct page *page, *end;

		page = NODE_MEM_MAP(node);
		end  = page + NODE_DATA(node)->node_spanned_pages;

		do {
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (PageSlab(page))
				slab++;
			else if (!page_count(page))
				free++;
			else
				shared += page_count(page) - 1;
			page++;
		} while (page < end);
	}

	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d slab pages\n", slab);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
}

struct node_info {
	unsigned int start;
	unsigned int end;
	int bootmap_pages;
};

#define O_PFN_DOWN(x)	((x) >> PAGE_SHIFT)
#define O_PFN_UP(x)	(PAGE_ALIGN(x) >> PAGE_SHIFT)

/*
 * FIXME: We really want to avoid allocating the bootmap bitmap
 * over the top of the initrd.  Hopefully, this is located towards
 * the start of a bank, so if we allocate the bootmap bitmap at
 * the end, we won't clash.
 */
static unsigned int __init
find_bootmap_pfn(int node, struct meminfo *mi, unsigned int bootmap_pages)
{
	unsigned int start_pfn, bank, bootmap_pfn;

	start_pfn   = O_PFN_UP(__pa(&_end));
	bootmap_pfn = 0;

	for (bank = 0; bank < mi->nr_banks; bank ++) {
		unsigned int start, end;

		if (mi->bank[bank].node != node)
			continue;

		start = mi->bank[bank].start >> PAGE_SHIFT;
		end   = (mi->bank[bank].size +
			 mi->bank[bank].start) >> PAGE_SHIFT;

		if (end < start_pfn)
			continue;

		if (start < start_pfn)
			start = start_pfn;

		if (end <= start)
			continue;

		if (end - start >= bootmap_pages) {
			bootmap_pfn = start;
			break;
		}
	}

	if (bootmap_pfn == 0)
		BUG();

	return bootmap_pfn;
}

/*
 * Scan the memory info structure and pull out:
 *  - the end of memory
 *  - the number of nodes
 *  - the pfn range of each node
 *  - the number of bootmem bitmap pages
 */
static unsigned int __init
find_memend_and_nodes(struct meminfo *mi, struct node_info *np)
{
	unsigned int i, bootmem_pages = 0, memend_pfn = 0;

	for (i = 0; i < MAX_NUMNODES; i++) {
		np[i].start = -1U;
		np[i].end = 0;
		np[i].bootmap_pages = 0;
	}

	for (i = 0; i < mi->nr_banks; i++) {
		unsigned long start, end;
		int node;

		if (mi->bank[i].size == 0) {
			/*
			 * Mark this bank with an invalid node number
			 */
			mi->bank[i].node = -1;
			continue;
		}

		node = mi->bank[i].node;

		/*
		 * Make sure we haven't exceeded the maximum number of nodes
		 * that we have in this configuration.  If we have, we're in
		 * trouble.  (maybe we ought to limit, instead of bugging?)
		 */
		if (node >= MAX_NUMNODES)
			BUG();
		node_set_online(node);

		/*
		 * Get the start and end pfns for this bank
		 */
		start = mi->bank[i].start >> PAGE_SHIFT;
		end   = (mi->bank[i].start + mi->bank[i].size) >> PAGE_SHIFT;

		if (np[node].start > start)
			np[node].start = start;

		if (np[node].end < end)
			np[node].end = end;

		if (memend_pfn < end)
			memend_pfn = end;
	}

	/*
	 * Calculate the number of pages we require to
	 * store the bootmem bitmaps.
	 */
	for_each_online_node(i) {
		if (np[i].end == 0)
			continue;

		np[i].bootmap_pages = bootmem_bootmap_pages(np[i].end -
							    np[i].start);
		bootmem_pages += np[i].bootmap_pages;
	}

	high_memory = __va(memend_pfn << PAGE_SHIFT);

	/*
	 * This doesn't seem to be used by the Linux memory
	 * manager any more.  If we can get rid of it, we
	 * also get rid of some of the stuff above as well.
	 *
	 * Note: max_low_pfn and max_pfn reflect the number
	 * of _pages_ in the system, not the maximum PFN.
	 */
	max_low_pfn = memend_pfn - O_PFN_DOWN(PHYS_OFFSET);
	max_pfn = memend_pfn - O_PFN_DOWN(PHYS_OFFSET);

	return bootmem_pages;
}

static int __init check_initrd(struct meminfo *mi)
{
	int initrd_node = -2;
#ifdef CONFIG_BLK_DEV_INITRD
	unsigned long end = phys_initrd_start + phys_initrd_size;

	/*
	 * Make sure that the initrd is within a valid area of
	 * memory.
	 */
	if (phys_initrd_size) {
		unsigned int i;

		initrd_node = -1;

		for (i = 0; i < mi->nr_banks; i++) {
			unsigned long bank_end;

			bank_end = mi->bank[i].start + mi->bank[i].size;

			if (mi->bank[i].start <= phys_initrd_start &&
			    end <= bank_end)
				initrd_node = mi->bank[i].node;
		}
	}

	if (initrd_node == -1) {
		printk(KERN_ERR "initrd (0x%08lx - 0x%08lx) extends beyond "
		       "physical memory - disabling initrd\n",
		       phys_initrd_start, end);
		phys_initrd_start = phys_initrd_size = 0;
	}
#endif

	return initrd_node;
}

/*
 * Reserve the various regions of node 0
 */
static __init void reserve_node_zero(unsigned int bootmap_pfn, unsigned int bootmap_pages)
{
	pg_data_t *pgdat = NODE_DATA(0);
	unsigned long res_size = 0;

	/*
	 * Register the kernel text and data with bootmem.
	 * Note that this can only be in node 0.
	 */
#ifdef CONFIG_XIP_KERNEL
	reserve_bootmem_node(pgdat, __pa(&__data_start), &_end - &__data_start);
#else
	reserve_bootmem_node(pgdat, __pa(&_stext), &_end - &_stext);
#endif

	/*
	 * Reserve the page tables.  These are already in use,
	 * and can only be in node 0.
	 */
	reserve_bootmem_node(pgdat, __pa(swapper_pg_dir),
			     PTRS_PER_PGD * sizeof(pgd_t));

	/*
	 * And don't forget to reserve the allocator bitmap,
	 * which will be freed later.
	 */
	reserve_bootmem_node(pgdat, bootmap_pfn << PAGE_SHIFT,
			     bootmap_pages << PAGE_SHIFT);

	/*
	 * Hmm... This should go elsewhere, but we really really need to
	 * stop things allocating the low memory; ideally we need a better
	 * implementation of GFP_DMA which does not assume that DMA-able
	 * memory starts at zero.
	 */
	if (machine_is_integrator() || machine_is_cintegrator())
		res_size = __pa(swapper_pg_dir) - PHYS_OFFSET;

	/*
	 * These should likewise go elsewhere.  They pre-reserve the
	 * screen memory region at the start of main system memory.
	 */
	if (machine_is_edb7211())
		res_size = 0x00020000;
	if (machine_is_p720t())
		res_size = 0x00014000;

#ifdef CONFIG_SA1111
	/*
	 * Because of the SA1111 DMA bug, we want to preserve our
	 * precious DMA-able memory...
	 */
	res_size = __pa(swapper_pg_dir) - PHYS_OFFSET;
#endif
	if (res_size)
		reserve_bootmem_node(pgdat, PHYS_OFFSET, res_size);
}

/*
 * Register all available RAM in this node with the bootmem allocator.
 */
static inline void free_bootmem_node_bank(int node, struct meminfo *mi)
{
	pg_data_t *pgdat = NODE_DATA(node);
	int bank;

	for (bank = 0; bank < mi->nr_banks; bank++)
		if (mi->bank[bank].node == node)
			free_bootmem_node(pgdat, mi->bank[bank].start,
					  mi->bank[bank].size);
}

/*
 * Initialise the bootmem allocator for all nodes.  This is called
 * early during the architecture specific initialisation.
 */
static void __init bootmem_init(struct meminfo *mi)
{
	struct node_info node_info[MAX_NUMNODES], *np = node_info;
	unsigned int bootmap_pages, bootmap_pfn, map_pg;
	int node, initrd_node;

	bootmap_pages = find_memend_and_nodes(mi, np);
	bootmap_pfn   = find_bootmap_pfn(0, mi, bootmap_pages);
	initrd_node   = check_initrd(mi);

	map_pg = bootmap_pfn;

	/*
	 * Initialise the bootmem nodes.
	 *
	 * What we really want to do is:
	 *
	 *   unmap_all_regions_except_kernel();
	 *   for_each_node_in_reverse_order(node) {
	 *     map_node(node);
	 *     allocate_bootmem_map(node);
	 *     init_bootmem_node(node);
	 *     free_bootmem_node(node);
	 *   }
	 *
	 * but this is a 2.5-type change.  For now, we just set
	 * the nodes up in reverse order.
	 *
	 * (we could also do with rolling bootmem_init and paging_init
	 * into one generic "memory_init" type function).
	 */
	np += num_online_nodes() - 1;
	for (node = num_online_nodes() - 1; node >= 0; node--, np--) {
		/*
		 * If there are no pages in this node, ignore it.
		 * Note that node 0 must always have some pages.
		 */
		if (np->end == 0 || !node_online(node)) {
			if (node == 0)
				BUG();
			continue;
		}

		/*
		 * Initialise the bootmem allocator.
		 */
		init_bootmem_node(NODE_DATA(node), map_pg, np->start, np->end);
		free_bootmem_node_bank(node, mi);
		map_pg += np->bootmap_pages;

		/*
		 * If this is node 0, we need to reserve some areas ASAP -
		 * we may use bootmem on node 0 to setup the other nodes.
		 */
		if (node == 0)
			reserve_node_zero(bootmap_pfn, bootmap_pages);
	}


#ifdef CONFIG_BLK_DEV_INITRD
	if (phys_initrd_size && initrd_node >= 0) {
		reserve_bootmem_node(NODE_DATA(initrd_node), phys_initrd_start,
				     phys_initrd_size);
		initrd_start = __phys_to_virt(phys_initrd_start);
		initrd_end = initrd_start + phys_initrd_size;
	}
#endif

	BUG_ON(map_pg != bootmap_pfn + bootmap_pages);
}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(struct meminfo *mi, struct machine_desc *mdesc)
{
	void *zero_page;
	int node;

	bootmem_init(mi);

	memcpy(&meminfo, mi, sizeof(meminfo));

	/*
	 * allocate the zero page.  Note that we count on this going ok.
	 */
	zero_page = alloc_bootmem_low_pages(PAGE_SIZE);

	/*
	 * initialise the page tables.
	 */
	memtable_init(mi);
	if (mdesc->map_io)
		mdesc->map_io();
	local_flush_tlb_all();

	/*
	 * initialise the zones within each node
	 */
	for_each_online_node(node) {
		unsigned long zone_size[MAX_NR_ZONES];
		unsigned long zhole_size[MAX_NR_ZONES];
		struct bootmem_data *bdata;
		pg_data_t *pgdat;
		int i;

		/*
		 * Initialise the zone size information.
		 */
		for (i = 0; i < MAX_NR_ZONES; i++) {
			zone_size[i]  = 0;
			zhole_size[i] = 0;
		}

		pgdat = NODE_DATA(node);
		bdata = pgdat->bdata;

		/*
		 * The size of this node has already been determined.
		 * If we need to do anything fancy with the allocation
		 * of this memory to the zones, now is the time to do
		 * it.
		 */
		zone_size[0] = bdata->node_low_pfn -
				(bdata->node_boot_start >> PAGE_SHIFT);

		/*
		 * If this zone has zero size, skip it.
		 */
		if (!zone_size[0])
			continue;

		/*
		 * For each bank in this node, calculate the size of the
		 * holes.  holes = node_size - sum(bank_sizes_in_node)
		 */
		zhole_size[0] = zone_size[0];
		for (i = 0; i < mi->nr_banks; i++) {
			if (mi->bank[i].node != node)
				continue;

			zhole_size[0] -= mi->bank[i].size >> PAGE_SHIFT;
		}

		/*
		 * Adjust the sizes according to any special
		 * requirements for this machine type.
		 */
		arch_adjust_zones(node, zone_size, zhole_size);

		free_area_init_node(node, pgdat, zone_size,
				bdata->node_boot_start >> PAGE_SHIFT, zhole_size);
	}

	/*
	 * finish off the bad pages once
	 * the mem_map is initialised
	 */
	memzero(zero_page, PAGE_SIZE);
	empty_zero_page = virt_to_page(zero_page);
	flush_dcache_page(empty_zero_page);
}

static inline void free_area(unsigned long addr, unsigned long end, char *s)
{
	unsigned int size = (end - addr) >> 10;

	for (; addr < end; addr += PAGE_SIZE) {
		struct page *page = virt_to_page(addr);
		ClearPageReserved(page);
		set_page_count(page, 1);
		free_page(addr);
		totalram_pages++;
	}

	if (size && s)
		printk(KERN_INFO "Freeing %s memory: %dK\n", s, size);
}

static inline void
free_memmap(int node, unsigned long start_pfn, unsigned long end_pfn)
{
	struct page *start_pg, *end_pg;
	unsigned long pg, pgend;

	/*
	 * Convert start_pfn/end_pfn to a struct page pointer.
	 */
	start_pg = pfn_to_page(start_pfn);
	end_pg = pfn_to_page(end_pfn);

	/*
	 * Convert to physical addresses, and
	 * round start upwards and end downwards.
	 */
	pg = PAGE_ALIGN(__pa(start_pg));
	pgend = __pa(end_pg) & PAGE_MASK;

	/*
	 * If there are free pages between these,
	 * free the section of the memmap array.
	 */
	if (pg < pgend)
		free_bootmem_node(NODE_DATA(node), pg, pgend - pg);
}

/*
 * The mem_map array can get very big.  Free the unused area of the memory map.
 */
static void __init free_unused_memmap_node(int node, struct meminfo *mi)
{
	unsigned long bank_start, prev_bank_end = 0;
	unsigned int i;

	/*
	 * [FIXME] This relies on each bank being in address order.  This
	 * may not be the case, especially if the user has provided the
	 * information on the command line.
	 */
	for (i = 0; i < mi->nr_banks; i++) {
		if (mi->bank[i].size == 0 || mi->bank[i].node != node)
			continue;

		bank_start = mi->bank[i].start >> PAGE_SHIFT;
		if (bank_start < prev_bank_end) {
			printk(KERN_ERR "MEM: unordered memory banks.  "
				"Not freeing memmap.\n");
			break;
		}

		/*
		 * If we had a previous bank, and there is a space
		 * between the current bank and the previous, free it.
		 */
		if (prev_bank_end && prev_bank_end != bank_start)
			free_memmap(node, prev_bank_end, bank_start);

		prev_bank_end = (mi->bank[i].start +
				 mi->bank[i].size) >> PAGE_SHIFT;
	}
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
	unsigned int codepages, datapages, initpages;
	int i, node;

	codepages = &_etext - &_text;
	datapages = &_end - &__data_start;
	initpages = &__init_end - &__init_begin;

#ifndef CONFIG_DISCONTIGMEM
	max_mapnr   = virt_to_page(high_memory) - mem_map;
#endif

	/* this will put all unused low memory onto the freelists */
	for_each_online_node(node) {
		pg_data_t *pgdat = NODE_DATA(node);

		free_unused_memmap_node(node, &meminfo);

		if (pgdat->node_spanned_pages != 0)
			totalram_pages += free_all_bootmem_node(pgdat);
	}

#ifdef CONFIG_SA1111
	/* now that our DMA memory is actually so designated, we can free it */
	free_area(PAGE_OFFSET, (unsigned long)swapper_pg_dir, NULL);
#endif

	/*
	 * Since our memory may not be contiguous, calculate the
	 * real number of pages we have in this system
	 */
	printk(KERN_INFO "Memory:");

	num_physpages = 0;
	for (i = 0; i < meminfo.nr_banks; i++) {
		num_physpages += meminfo.bank[i].size >> PAGE_SHIFT;
		printk(" %ldMB", meminfo.bank[i].size >> 20);
	}

	printk(" = %luMB total\n", num_physpages >> (20 - PAGE_SHIFT));
	printk(KERN_NOTICE "Memory: %luKB available (%dK code, "
		"%dK data, %dK init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		codepages >> 10, datapages >> 10, initpages >> 10);

	if (PAGE_SIZE >= 16384 && num_physpages <= 128) {
		extern int sysctl_overcommit_memory;
		/*
		 * On a machine this small we won't get
		 * anywhere without overcommit, so turn
		 * it on by default.
		 */
		sysctl_overcommit_memory = OVERCOMMIT_ALWAYS;
	}
}

void free_initmem(void)
{
	if (!machine_is_integrator() && !machine_is_cintegrator()) {
		free_area((unsigned long)(&__init_begin),
			  (unsigned long)(&__init_end),
			  "init");
	}
}

#ifdef CONFIG_BLK_DEV_INITRD

static int keep_initrd;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (!keep_initrd)
		free_area(start, end, "initrd");
}

static int __init keepinitrd_setup(char *__unused)
{
	keep_initrd = 1;
	return 1;
}

__setup("keepinitrd", keepinitrd_setup);
#endif
