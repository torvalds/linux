/*
 *  linux/arch/arm/mm/init.c
 *
 *  Copyright (C) 1995-2005 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/mman.h>
#include <linux/nodemask.h>
#include <linux/initrd.h>

#include <asm/mach-types.h>
#include <asm/setup.h>
#include <asm/sizes.h>
#include <asm/tlb.h>

#include <asm/mach/arch.h>
#include <asm/mach/map.h>

#include "mm.h"

extern void _text, _etext, __data_start, _end, __init_begin, __init_end;
extern unsigned long phys_initrd_start;
extern unsigned long phys_initrd_size;

/*
 * This is used to pass memory configuration data from paging_init
 * to mem_init, and by show_mem() to skip holes in the memory map.
 */
static struct meminfo meminfo = { 0, };

#define for_each_nodebank(iter,mi,no)			\
	for (iter = 0; iter < mi->nr_banks; iter++)	\
		if (mi->bank[iter].node == no)

void show_mem(void)
{
	int free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0, slab = 0, node, i;
	struct meminfo * mi = &meminfo;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));

	for_each_online_node(node) {
		pg_data_t *n = NODE_DATA(node);
		struct page *map = n->node_mem_map - n->node_start_pfn;

		for_each_nodebank (i,mi,node) {
			unsigned int pfn1, pfn2;
			struct page *page, *end;

			pfn1 = __phys_to_pfn(mi->bank[i].start);
			pfn2 = __phys_to_pfn(mi->bank[i].size + mi->bank[i].start);

			page = map + pfn1;
			end  = map + pfn2;

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
	}

	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d slab pages\n", slab);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
}

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

	start_pfn   = PAGE_ALIGN(__pa(&_end)) >> PAGE_SHIFT;
	bootmap_pfn = 0;

	for_each_nodebank(bank, mi, node) {
		unsigned int start, end;

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

static inline void map_memory_bank(struct membank *bank)
{
#ifdef CONFIG_MMU
	struct map_desc map;

	map.pfn = __phys_to_pfn(bank->start);
	map.virtual = __phys_to_virt(bank->start);
	map.length = bank->size;
	map.type = MT_MEMORY;

	create_mapping(&map);
#endif
}

static unsigned long __init
bootmem_init_node(int node, int initrd_node, struct meminfo *mi)
{
	unsigned long zone_size[MAX_NR_ZONES], zhole_size[MAX_NR_ZONES];
	unsigned long start_pfn, end_pfn, boot_pfn;
	unsigned int boot_pages;
	pg_data_t *pgdat;
	int i;

	start_pfn = -1UL;
	end_pfn = 0;

	/*
	 * Calculate the pfn range, and map the memory banks for this node.
	 */
	for_each_nodebank(i, mi, node) {
		struct membank *bank = &mi->bank[i];
		unsigned long start, end;

		start = bank->start >> PAGE_SHIFT;
		end = (bank->start + bank->size) >> PAGE_SHIFT;

		if (start_pfn > start)
			start_pfn = start;
		if (end_pfn < end)
			end_pfn = end;

		map_memory_bank(bank);
	}

	/*
	 * If there is no memory in this node, ignore it.
	 */
	if (end_pfn == 0)
		return end_pfn;

	/*
	 * Allocate the bootmem bitmap page.
	 */
	boot_pages = bootmem_bootmap_pages(end_pfn - start_pfn);
	boot_pfn = find_bootmap_pfn(node, mi, boot_pages);

	/*
	 * Initialise the bootmem allocator for this node, handing the
	 * memory banks over to bootmem.
	 */
	node_set_online(node);
	pgdat = NODE_DATA(node);
	init_bootmem_node(pgdat, boot_pfn, start_pfn, end_pfn);

	for_each_nodebank(i, mi, node)
		free_bootmem_node(pgdat, mi->bank[i].start, mi->bank[i].size);

	/*
	 * Reserve the bootmem bitmap for this node.
	 */
	reserve_bootmem_node(pgdat, boot_pfn << PAGE_SHIFT,
			     boot_pages << PAGE_SHIFT);

#ifdef CONFIG_BLK_DEV_INITRD
	/*
	 * If the initrd is in this node, reserve its memory.
	 */
	if (node == initrd_node) {
		reserve_bootmem_node(pgdat, phys_initrd_start,
				     phys_initrd_size);
		initrd_start = __phys_to_virt(phys_initrd_start);
		initrd_end = initrd_start + phys_initrd_size;
	}
#endif

	/*
	 * Finally, reserve any node zero regions.
	 */
	if (node == 0)
		reserve_node_zero(pgdat);

	/*
	 * initialise the zones within this node.
	 */
	memset(zone_size, 0, sizeof(zone_size));
	memset(zhole_size, 0, sizeof(zhole_size));

	/*
	 * The size of this node has already been determined.  If we need
	 * to do anything fancy with the allocation of this memory to the
	 * zones, now is the time to do it.
	 */
	zone_size[0] = end_pfn - start_pfn;

	/*
	 * For each bank in this node, calculate the size of the holes.
	 *  holes = node_size - sum(bank_sizes_in_node)
	 */
	zhole_size[0] = zone_size[0];
	for_each_nodebank(i, mi, node)
		zhole_size[0] -= mi->bank[i].size >> PAGE_SHIFT;

	/*
	 * Adjust the sizes according to any special requirements for
	 * this machine type.
	 */
	arch_adjust_zones(node, zone_size, zhole_size);

	free_area_init_node(node, pgdat, zone_size, start_pfn, zhole_size);

	return end_pfn;
}

void __init bootmem_init(struct meminfo *mi)
{
	unsigned long memend_pfn = 0;
	int node, initrd_node, i;

	/*
	 * Invalidate the node number for empty or invalid memory banks
	 */
	for (i = 0; i < mi->nr_banks; i++)
		if (mi->bank[i].size == 0 || mi->bank[i].node >= MAX_NUMNODES)
			mi->bank[i].node = -1;

	memcpy(&meminfo, mi, sizeof(meminfo));

	/*
	 * Locate which node contains the ramdisk image, if any.
	 */
	initrd_node = check_initrd(mi);

	/*
	 * Run through each node initialising the bootmem allocator.
	 */
	for_each_node(node) {
		unsigned long end_pfn;

		end_pfn = bootmem_init_node(node, initrd_node, mi);

		/*
		 * Remember the highest memory PFN.
		 */
		if (end_pfn > memend_pfn)
			memend_pfn = end_pfn;
	}

	high_memory = __va(memend_pfn << PAGE_SHIFT);

	/*
	 * This doesn't seem to be used by the Linux memory manager any
	 * more, but is used by ll_rw_block.  If we can get rid of it, we
	 * also get rid of some of the stuff above as well.
	 *
	 * Note: max_low_pfn and max_pfn reflect the number of _pages_ in
	 * the system, not the maximum PFN.
	 */
	max_pfn = max_low_pfn = memend_pfn - PHYS_PFN_OFFSET;
}

static inline void free_area(unsigned long addr, unsigned long end, char *s)
{
	unsigned int size = (end - addr) >> 10;

	for (; addr < end; addr += PAGE_SIZE) {
		struct page *page = virt_to_page(addr);
		ClearPageReserved(page);
		init_page_count(page);
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
	for_each_nodebank(i, mi, node) {
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
