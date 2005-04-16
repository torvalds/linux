/*
 *  linux/arch/cris/mm/init.c
 *
 *  Copyright (C) 1995  Linus Torvalds
 *  Copyright (C) 2000,2001  Axis Communications AB
 *
 *  Authors:  Bjorn Wesen (bjornw@axis.com)
 *
 *  $Log: init.c,v $
 *  Revision 1.11  2004/05/28 09:28:56  starvik
 *  Calculation of loops_per_usec moved because initalization order has changed
 *  in Linux 2.6.
 *
 *  Revision 1.10  2004/05/14 07:58:05  starvik
 *  Merge of changes from 2.4
 *
 *  Revision 1.9  2003/07/04 08:27:54  starvik
 *  Merge of Linux 2.5.74
 *
 *  Revision 1.8  2003/04/09 05:20:48  starvik
 *  Merge of Linux 2.5.67
 *
 *  Revision 1.7  2003/01/22 06:48:38  starvik
 *  Fixed warnings issued by GCC 3.2.1
 *
 *  Revision 1.6  2002/12/11 14:44:48  starvik
 *  Extracted v10 (ETRAX 100LX) specific stuff to arch/cris/arch-v10/mm
 *
 *  Revision 1.5  2002/11/18 07:37:37  starvik
 *  Added cache bug workaround (from Linux 2.4)
 *
 *  Revision 1.4  2002/11/13 15:40:24  starvik
 *  Removed the page table cache stuff (as done in other archs)
 *
 *  Revision 1.3  2002/11/05 06:45:13  starvik
 *  Merge of Linux 2.5.45
 *
 *  Revision 1.2  2001/12/18 13:35:22  bjornw
 *  Applied the 2.4.13->2.4.16 CRIS patch to 2.5.1 (is a copy of 2.4.15).
 *
 *  Revision 1.31  2001/11/13 16:22:00  bjornw
 *  Skip calculating totalram and sharedram in si_meminfo
 *
 *  Revision 1.30  2001/11/12 19:02:10  pkj
 *  Fixed compiler warnings.
 *
 *  Revision 1.29  2001/07/25 16:09:50  bjornw
 *  val->sharedram will stay 0
 *
 *  Revision 1.28  2001/06/28 16:30:17  bjornw
 *  Oops. This needs to wait until 2.4.6 is merged
 *
 *  Revision 1.27  2001/06/28 14:04:07  bjornw
 *  Fill in sharedram
 *
 *  Revision 1.26  2001/06/18 06:36:02  hp
 *  Enable free_initmem of __init-type pages
 *
 *  Revision 1.25  2001/06/13 00:02:23  bjornw
 *  Use a separate variable to store the current pgd to avoid races in schedule
 *
 *  Revision 1.24  2001/05/15 00:52:20  hp
 *  Only map segment 0xa as seg if CONFIG_JULIETTE
 *
 *  Revision 1.23  2001/04/04 14:35:40  bjornw
 *  * Removed get_pte_slow and friends (2.4.3 change)
 *  * Removed bad_pmd handling (2.4.3 change)
 *
 *  Revision 1.22  2001/04/04 13:38:04  matsfg
 *  Moved ioremap to a separate function instead
 *
 *  Revision 1.21  2001/03/27 09:28:33  bjornw
 *  ioremap used too early - lets try it in mem_init instead
 *
 *  Revision 1.20  2001/03/23 07:39:21  starvik
 *  Corrected according to review remarks
 *
 *  Revision 1.19  2001/03/15 14:25:17  bjornw
 *  More general shadow registers and ioremaped addresses for external I/O
 *
 *  Revision 1.18  2001/02/23 12:46:44  bjornw
 *  * 0xc was not CSE1; 0x8 is, same as uncached flash, so we move the uncached
 *    flash during CRIS_LOW_MAP from 0xe to 0x8 so both the flash and the I/O
 *    is mapped straight over (for !CRIS_LOW_MAP the uncached flash is still 0xe)
 *
 *  Revision 1.17  2001/02/22 15:05:21  bjornw
 *  Map 0x9 straight over during LOW_MAP to allow for memory mapped LEDs
 *
 *  Revision 1.16  2001/02/22 15:02:35  bjornw
 *  Map 0xc straight over during LOW_MAP to allow for memory mapped I/O
 *
 *  Revision 1.15  2001/01/10 21:12:10  bjornw
 *  loops_per_sec -> loops_per_jiffy
 *
 *  Revision 1.14  2000/11/22 16:23:20  bjornw
 *  Initialize totalhigh counters to 0 to make /proc/meminfo look nice.
 *
 *  Revision 1.13  2000/11/21 16:37:51  bjornw
 *  Temporarily disable initmem freeing
 *
 *  Revision 1.12  2000/11/21 13:55:07  bjornw
 *  Use CONFIG_CRIS_LOW_MAP for the low VM map instead of explicit CPU type
 *
 *  Revision 1.11  2000/10/06 12:38:22  bjornw
 *  Cast empty_bad_page correctly (should really be of * type from the start..
 *
 *  Revision 1.10  2000/10/04 16:53:57  bjornw
 *  Fix memory-map due to LX features
 *
 *  Revision 1.9  2000/09/13 15:47:49  bjornw
 *  Wrong count in reserved-pages loop
 *
 *  Revision 1.8  2000/09/13 14:35:10  bjornw
 *  2.4.0-test8 added a new arg to free_area_init_node
 *
 *  Revision 1.7  2000/08/17 15:35:55  bjornw
 *  2.4.0-test6 removed MAP_NR and inserted virt_to_page
 *
 *
 */

#include <linux/init.h>
#include <linux/bootmem.h>
#include <asm/tlb.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

unsigned long empty_zero_page;

extern char _stext, _edata, _etext; /* From linkerscript */
extern char __init_begin, __init_end;

void 
show_mem(void)
{
	int i,free = 0,total = 0,cached = 0, reserved = 0, nonshared = 0;
	int shared = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (!page_count(mem_map+i))
			free++;
		else if (page_count(mem_map+i) == 1)
			nonshared++;
		else
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages nonshared\n",nonshared);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

void __init
mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	unsigned long tmp;

	if(!mem_map)
		BUG();

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
	       (unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
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
        unsigned long addr;

        addr = (unsigned long)(&__init_begin);
        for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
                ClearPageReserved(virt_to_page(addr));
                set_page_count(virt_to_page(addr), 1);
                free_page(addr);
                totalram_pages++;
        }
        printk (KERN_INFO "Freeing unused kernel memory: %luk freed\n",
		(unsigned long)((&__init_end - &__init_begin) >> 10));
}
