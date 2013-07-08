/*
 * arch/score/mm/init.c
 *
 * Score Processor version.
 *
 * Copyright (C) 2009 Sunplus Core Technology Co., Ltd.
 *  Lennox Wu <lennox.wu@sunplusct.com>
 *  Chen Liqin <liqin.chen@sunplusct.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/errno.h>
#include <linux/bootmem.h>
#include <linux/kernel.h>
#include <linux/gfp.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/kcore.h>
#include <linux/sched.h>
#include <linux/initrd.h>

#include <asm/sections.h>
#include <asm/tlb.h>

unsigned long empty_zero_page;
EXPORT_SYMBOL_GPL(empty_zero_page);

static void setup_zero_page(void)
{
	struct page *page;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page((void *) empty_zero_page);
	mark_page_reserved(page);
}

#ifndef CONFIG_NEED_MULTIPLE_NODES
int page_is_ram(unsigned long pagenr)
{
	if (pagenr >= min_low_pfn && pagenr < max_low_pfn)
		return 1;
	else
		return 0;
}

void __init paging_init(void)
{
	unsigned long max_zone_pfns[MAX_NR_ZONES];
	unsigned long lastpfn;

	pagetable_init();
	max_zone_pfns[ZONE_NORMAL] = max_low_pfn;
	lastpfn = max_low_pfn;
	free_area_init_nodes(max_zone_pfns);
}

void __init mem_init(void)
{
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);
	free_all_bootmem();
	setup_zero_page();	/* Setup zeroed pages. */

	mem_init_print_info(NULL);
}
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, POISON_FREE_INITMEM,
			   "initrd");
}
#endif

void __init_refok free_initmem(void)
{
	free_initmem_default(POISON_FREE_INITMEM);
}

unsigned long pgd_current;

#define __page_aligned(order) __attribute__((__aligned__(PAGE_SIZE<<order)))

/*
 * gcc 3.3 and older have trouble determining that PTRS_PER_PGD and PGD_ORDER
 * are constants.  So we use the variants from asm-offset.h until that gcc
 * will officially be retired.
 */
pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned(PTE_ORDER);
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned(PTE_ORDER);
