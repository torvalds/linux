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
	int codesize, reservedpages, datasize, initsize;
	unsigned long tmp;

	BUG_ON(!mem_map);

	/* max/min_low_pfn was set by setup.c
	 * now we just copy it to some other necessary places...
	 *
	 * high_memory was also set in setup.c
	 */

	max_mapnr = num_physpages = max_low_pfn - min_low_pfn;
 
	/* this will put all memory onto the freelists */
        totalram_pages = free_all_bootmem();

	reservedpages = 0;
	for (tmp = 0; tmp < max_mapnr; tmp++) {
		/*
                 * Only count reserved RAM pages
                 */
		if (PageReserved(mem_map + tmp))
			reservedpages++;
	}

	codesize =  (unsigned long) &_etext - (unsigned long) &_stext;
        datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
        initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;
	
        printk(KERN_INFO
               "Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, "
	       "%dk init)\n" ,
	       nr_free_pages() << (PAGE_SHIFT-10),
	       max_mapnr << (PAGE_SHIFT-10),
	       codesize >> 10,
	       reservedpages << (PAGE_SHIFT-10),
	       datasize >> 10,
	       initsize >> 10
               );
}

/* free the pages occupied by initialization code */

void 
free_initmem(void)
{
	free_initmem_default(-1);
}
