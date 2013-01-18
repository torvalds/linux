/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/memblock.h>
#ifdef CONFIG_BLOCK_DEV_RAM
#include <linux/blk.h>
#endif
#include <linux/swap.h>
#include <linux/module.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/sections.h>
#include <asm/arcregs.h>

pgd_t swapper_pg_dir[PTRS_PER_PGD] __aligned(PAGE_SIZE);
char empty_zero_page[PAGE_SIZE] __aligned(PAGE_SIZE);
EXPORT_SYMBOL(empty_zero_page);

/* Default tot mem from .config */
static unsigned long arc_mem_sz = CONFIG_ARC_PLAT_SDRAM_SIZE;

/* User can over-ride above with "mem=nnn[KkMm]" in cmdline */
static int __init setup_mem_sz(char *str)
{
	arc_mem_sz = memparse(str, NULL) & PAGE_MASK;

	/* early console might not be setup yet - it will show up later */
	pr_info("\"mem=%s\": mem sz set to %ldM\n", str, TO_MB(arc_mem_sz));

	return 0;
}
early_param("mem", setup_mem_sz);

/*
 * First memory setup routine called from setup_arch()
 * 1. setup swapper's mm @init_mm
 * 2. Count the pages we have and setup bootmem allocator
 * 3. zone setup
 */
void __init setup_arch_memory(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = { 0, 0 };
	unsigned long end_mem = CONFIG_LINUX_LINK_BASE + arc_mem_sz;

	init_mm.start_code = (unsigned long)_text;
	init_mm.end_code = (unsigned long)_etext;
	init_mm.end_data = (unsigned long)_edata;
	init_mm.brk = (unsigned long)_end;

	/*
	 * We do it here, so that memory is correctly instantiated
	 * even if "mem=xxx" cmline over-ride is not given
	 */
	memblock_add(CONFIG_LINUX_LINK_BASE, arc_mem_sz);

	/*------------- externs in mm need setting up ---------------*/

	/* first page of system - kernel .vector starts here */
	min_low_pfn = PFN_DOWN(CONFIG_LINUX_LINK_BASE);

	/* Last usable page of low mem (no HIGHMEM yet for ARC port) */
	max_low_pfn = max_pfn = PFN_DOWN(end_mem);

	max_mapnr = num_physpages = max_low_pfn - min_low_pfn;

	/*------------- reserve kernel image -----------------------*/
	memblock_reserve(CONFIG_LINUX_LINK_BASE,
			 __pa(_end) - CONFIG_LINUX_LINK_BASE);

	memblock_dump_all();

	/*-------------- node setup --------------------------------*/
	memset(zones_size, 0, sizeof(zones_size));
	zones_size[ZONE_NORMAL] = num_physpages;

	/*
	 * We can't use the helper free_area_init(zones[]) because it uses
	 * PAGE_OFFSET to compute the @min_low_pfn which would be wrong
	 * when our kernel doesn't start at PAGE_OFFSET, i.e.
	 * PAGE_OFFSET != CONFIG_LINUX_LINK_BASE
	 */
	free_area_init_node(0,			/* node-id */
			    zones_size,		/* num pages per zone */
			    min_low_pfn,	/* first pfn of node */
			    NULL);		/* NO holes */
}

/*
 * mem_init - initializes memory
 *
 * Frees up bootmem
 * Calculates and displays memory available/used
 */
void __init mem_init(void)
{
	int codesize, datasize, initsize, reserved_pages, free_pages;
	int tmp;

	high_memory = (void *)(CONFIG_LINUX_LINK_BASE + arc_mem_sz);

	totalram_pages = free_all_bootmem();

	/* count all reserved pages [kernel code/data/mem_map..] */
	reserved_pages = 0;
	for (tmp = 0; tmp < max_mapnr; tmp++)
		if (PageReserved(mem_map + tmp))
			reserved_pages++;

	/* XXX: nr_free_pages() is equivalent */
	free_pages = max_mapnr - reserved_pages;

	/*
	 * For the purpose of display below, split the "reserve mem"
	 * kernel code/data is already shown explicitly,
	 * Show any other reservations (mem_map[ ] et al)
	 */
	reserved_pages -= (((unsigned int)_end - CONFIG_LINUX_LINK_BASE) >>
								PAGE_SHIFT);

	codesize = _etext - _text;
	datasize = _end - _etext;
	initsize = __init_end - __init_begin;

	pr_info("Memory Available: %dM / %ldM (%dK code, %dK data, %dK init, %dK reserv)\n",
		PAGES_TO_MB(free_pages),
		TO_MB(arc_mem_sz),
		TO_KB(codesize), TO_KB(datasize), TO_KB(initsize),
		PAGES_TO_KB(reserved_pages));
}

static void __init free_init_pages(const char *what, unsigned long begin,
				   unsigned long end)
{
	unsigned long addr;

	pr_info("Freeing %s: %ldk [%lx] to [%lx]\n",
		what, TO_KB(end - begin), begin, end);

	/* need to check that the page we free is not a partial page */
	for (addr = begin; addr + PAGE_SIZE <= end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}
}

/*
 * free_initmem: Free all the __init memory.
 */
void __init_refok free_initmem(void)
{
	free_init_pages("unused kernel memory",
			(unsigned long)__init_begin,
			(unsigned long)__init_end);
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_init_pages("initrd memory", start, end);
}
#endif
