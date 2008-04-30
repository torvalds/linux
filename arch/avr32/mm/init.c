/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/nodemask.h>

#include <asm/page.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/setup.h>
#include <asm/sections.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

pgd_t swapper_pg_dir[PTRS_PER_PGD];

struct page *empty_zero_page;

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache = NO_CONTEXT;

void show_mem(void)
{
	int total = 0, reserved = 0, cached = 0;
	int slab = 0, free = 0, shared = 0;
	pg_data_t *pgdat;

	printk("Mem-info:\n");
	show_free_areas();

	for_each_online_pgdat(pgdat) {
		struct page *page, *end;

		page = pgdat->node_mem_map;
		end = page + pgdat->node_spanned_pages;

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

	printk ("%d pages of RAM\n", total);
	printk ("%d free pages\n", free);
	printk ("%d reserved pages\n", reserved);
	printk ("%d slab pages\n", slab);
	printk ("%d pages shared\n", shared);
	printk ("%d pages swap cached\n", cached);
}

/*
 * paging_init() sets up the page tables
 *
 * This routine also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	extern unsigned long _evba;
	void *zero_page;
	int nid;

	/*
	 * Make sure we can handle exceptions before enabling
	 * paging. Not that we should ever _get_ any exceptions this
	 * early, but you never know...
	 */
	printk("Exception vectors start at %p\n", &_evba);
	sysreg_write(EVBA, (unsigned long)&_evba);

	/*
	 * Since we are ready to handle exceptions now, we should let
	 * the CPU generate them...
	 */
	__asm__ __volatile__ ("csrf %0" : : "i"(SR_EM_BIT));

	/*
	 * Allocate the zero page. The allocator will panic if it
	 * can't satisfy the request, so no need to check.
	 */
	zero_page = alloc_bootmem_low_pages_node(NODE_DATA(0),
						 PAGE_SIZE);

	{
		pgd_t *pg_dir;
		int i;

		pg_dir = swapper_pg_dir;
		sysreg_write(PTBR, (unsigned long)pg_dir);

		for (i = 0; i < PTRS_PER_PGD; i++)
			pgd_val(pg_dir[i]) = 0;

		enable_mmu();
		printk ("CPU: Paging enabled\n");
	}

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long zones_size[MAX_NR_ZONES];
		unsigned long low, start_pfn;

		start_pfn = pgdat->bdata->node_boot_start;
		start_pfn >>= PAGE_SHIFT;
		low = pgdat->bdata->node_low_pfn;

		memset(zones_size, 0, sizeof(zones_size));
		zones_size[ZONE_NORMAL] = low - start_pfn;

		printk("Node %u: start_pfn = 0x%lx, low = 0x%lx\n",
		       nid, start_pfn, low);

		free_area_init_node(nid, pgdat, zones_size, start_pfn, NULL);

		printk("Node %u: mem_map starts at %p\n",
		       pgdat->node_id, pgdat->node_mem_map);
	}

	mem_map = NODE_DATA(0)->node_mem_map;

	memset(zero_page, 0, PAGE_SIZE);
	empty_zero_page = virt_to_page(zero_page);
	flush_dcache_page(empty_zero_page);
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	int nid, i;

	reservedpages = 0;
	high_memory = NULL;

	/* this will put all low memory onto the freelists */
	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long node_pages = 0;
		void *node_high_memory;

		num_physpages += pgdat->node_present_pages;

		if (pgdat->node_spanned_pages != 0)
			node_pages = free_all_bootmem_node(pgdat);

		totalram_pages += node_pages;

		for (i = 0; i < node_pages; i++)
			if (PageReserved(pgdat->node_mem_map + i))
				reservedpages++;

		node_high_memory = (void *)((pgdat->node_start_pfn
					     + pgdat->node_spanned_pages)
					    << PAGE_SHIFT);
		if (node_high_memory > high_memory)
			high_memory = node_high_memory;
	}

	max_mapnr = MAP_NR(high_memory);

	codesize = (unsigned long)_etext - (unsigned long)_text;
	datasize = (unsigned long)_edata - (unsigned long)_data;
	initsize = (unsigned long)__init_end - (unsigned long)__init_begin;

	printk ("Memory: %luk/%luk available (%dk kernel code, "
		"%dk reserved, %dk data, %dk init)\n",
		(unsigned long)nr_free_pages() << (PAGE_SHIFT - 10),
		totalram_pages << (PAGE_SHIFT - 10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT - 10),
		datasize >> 10,
		initsize >> 10);
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
		printk(KERN_INFO "Freeing %s memory: %dK (%lx - %lx)\n",
		       s, size, end - (size << 10), end);
}

void free_initmem(void)
{
	free_area((unsigned long)__init_begin, (unsigned long)__init_end,
		  "init");
}

#ifdef CONFIG_BLK_DEV_INITRD

void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_area(start, end, "initrd");
}

#endif
