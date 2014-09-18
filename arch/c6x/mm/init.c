/*
 *  Port on Texas Instruments TMS320C6x architecture
 *
 *  Copyright (C) 2004, 2009, 2010, 2011 Texas Instruments Incorporated
 *  Author: Aurelien Jacquiot (aurelien.jacquiot@jaluna.com)
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 */
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/module.h>
#include <linux/bootmem.h>
#ifdef CONFIG_BLK_DEV_RAM
#include <linux/blkdev.h>
#endif
#include <linux/initrd.h>

#include <asm/sections.h>
#include <asm/uaccess.h>

/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses  of available kernel virtual memory.
 */
void __init paging_init(void)
{
	struct pglist_data *pgdat = NODE_DATA(0);
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	empty_zero_page      = (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	/*
	 * Set up user data space
	 */
	set_fs(KERNEL_DS);

	/*
	 * Define zones
	 */
	zones_size[ZONE_NORMAL] = (memory_end - PAGE_OFFSET) >> PAGE_SHIFT;
	pgdat->node_zones[ZONE_NORMAL].zone_start_pfn =
		__pa(PAGE_OFFSET) >> PAGE_SHIFT;

	free_area_init(zones_size);
}

void __init mem_init(void)
{
	high_memory = (void *)(memory_end & PAGE_MASK);

	/* this will put all memory onto the freelists */
	free_all_bootmem();

	mem_init_print_info(NULL);
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

void __init free_initmem(void)
{
	free_initmem_default(-1);
}
