// SPDX-License-Identifier: GPL-2.0
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
#include <linux/proc_fs.h>
#include <linux/kcore.h>
#include <asm/tlb.h>
#include <asm/sections.h>

unsigned long empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

void __init mem_init(void)
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

/* Free a range of init pages. Virtual addresses. */

void free_init_pages(const char *what, unsigned long begin, unsigned long end)
{
	unsigned long addr;

	for (addr = begin; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}

	printk(KERN_INFO "Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

/* Free the pages occupied by initialization code. */

void free_initmem(void)
{
	free_initmem_default(-1);
}

/* Free the pages occupied by initrd code. */

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_init_pages("initrd memory",
	                start,
	                end);
}
#endif
