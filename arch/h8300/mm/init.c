// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/arch/h8300/mm/init.c
 *
 *  Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>,
 *                      Kenneth Albanowski <kjahds@kjahds.com>,
 *  Copyright (C) 2000  Lineo, Inc.  (www.lineo.com)
 *
 *  Based on:
 *
 *  linux/arch/m68knommu/mm/init.c
 *  linux/arch/m68k/mm/init.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  JAN/1999 -- hacked to support ColdFire (gerg@snapgear.com)
 *  DEC/2000 -- linux 2.4 support <davidm@snapgear.com>
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/mman.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/highmem.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/gfp.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sections.h>

/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long empty_zero_page;

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses of available kernel virtual memory.
 */
void __init paging_init(void)
{
	/*
	 * Make sure start_mem is page aligned,  otherwise bootmem and
	 * page_alloc get different views og the world.
	 */
	unsigned long start_mem = PAGE_ALIGN(memory_start);
	unsigned long end_mem   = memory_end & PAGE_MASK;

	pr_debug("start_mem is %#lx\nvirtual_end is %#lx\n",
		 start_mem, end_mem);

	/*
	 * Initialize the bad page table and bad page to point
	 * to a couple of allocated pages.
	 */
	empty_zero_page = (unsigned long)alloc_bootmem_pages(PAGE_SIZE);
	memset((void *)empty_zero_page, 0, PAGE_SIZE);

	/*
	 * Set up SFC/DFC registers (user data space).
	 */
	set_fs(USER_DS);

	pr_debug("before free_area_init\n");

	pr_debug("free_area_init -> start_mem is %#lx\nvirtual_end is %#lx\n",
		 start_mem, end_mem);

	{
		unsigned long zones_size[MAX_NR_ZONES] = {0, };

		zones_size[ZONE_NORMAL] = (end_mem - PAGE_OFFSET) >> PAGE_SHIFT;
		free_area_init(zones_size);
	}
}

void __init mem_init(void)
{
	pr_devel("Mem_init: start=%lx, end=%lx\n", memory_start, memory_end);

	high_memory = (void *) (memory_end & PAGE_MASK);
	max_mapnr = MAP_NR(high_memory);

	/* this will put all low memory onto the freelists */
	free_all_bootmem();

	mem_init_print_info(NULL);
}


#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
}
#endif

void
free_initmem(void)
{
	free_initmem_default(-1);
}
