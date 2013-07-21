/*
 *  linux/arch/cris/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000,2001  Axis Communications AB
 *
 *  Authors:  Bjorn Wesen (bjornw@axis.com)
 *
 */

#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <asm/tlb.h>
#include <asm/sections.h>

unsigned long empty_zero_page;

void __init
mem_init(void)
{
	BUG_ON(!mem_map);

	/* max/min_low_pfn was set by setup.c
	 * now we just copy it to some other necessary places...
	 *
	 * high_memory was also set in setup.c
	 */
	max_mapnr = max_low_pfn - min_low_pfn;
        free_all_bootmem();
	mem_init_print_info(NULL);
}

/* free the pages occupied by initialization code */

void 
free_initmem(void)
{
	free_initmem_default(-1);
}
