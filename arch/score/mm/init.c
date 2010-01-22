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
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/mman.h>
#include <linux/pagemap.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/initrd.h>

#include <asm/sections.h>
#include <asm/tlb.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

unsigned long empty_zero_page;
EXPORT_SYMBOL_GPL(empty_zero_page);

static struct kcore_list kcore_mem, kcore_vmalloc;

static unsigned long setup_zero_page(void)
{
	struct page *page;

	empty_zero_page = __get_free_pages(GFP_KERNEL | __GFP_ZERO, 0);
	if (!empty_zero_page)
		panic("Oh boy, that early out of memory?");

	page = virt_to_page((void *) empty_zero_page);
	SetPageReserved(page);

	return 1UL;
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
	unsigned long codesize, reservedpages, datasize, initsize;
	unsigned long tmp, ram = 0;

	max_mapnr = max_low_pfn;
	high_memory = (void *) __va(max_low_pfn << PAGE_SHIFT);
	totalram_pages += free_all_bootmem();
	totalram_pages -= setup_zero_page();	/* Setup zeroed pages. */
	reservedpages = 0;

	for (tmp = 0; tmp < max_low_pfn; tmp++)
		if (page_is_ram(tmp)) {
			ram++;
			if (PageReserved(pfn_to_page(tmp)))
				reservedpages++;
		}

	num_physpages = ram;
	codesize = (unsigned long) &_etext - (unsigned long) &_text;
	datasize = (unsigned long) &_edata - (unsigned long) &_etext;
	initsize = (unsigned long) &__init_end - (unsigned long) &__init_begin;

	kclist_add(&kcore_mem, __va(0), max_low_pfn << PAGE_SHIFT);
	kclist_add(&kcore_vmalloc, (void *) VMALLOC_START,
			VMALLOC_END - VMALLOC_START);

	printk(KERN_INFO "Memory: %luk/%luk available (%ldk kernel code, "
			"%ldk reserved, %ldk data, %ldk init, %ldk highmem)\n",
			(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
			ram << (PAGE_SHIFT-10), codesize >> 10,
			reservedpages << (PAGE_SHIFT-10), datasize >> 10,
			initsize >> 10,
			(unsigned long) (totalhigh_pages << (PAGE_SHIFT-10)));
}
#endif /* !CONFIG_NEED_MULTIPLE_NODES */

static void free_init_pages(const char *what, unsigned long begin, unsigned long end)
{
	unsigned long pfn;

	for (pfn = PFN_UP(begin); pfn < PFN_DOWN(end); pfn++) {
		struct page *page = pfn_to_page(pfn);
		void *addr = phys_to_virt(PFN_PHYS(pfn));

		ClearPageReserved(page);
		init_page_count(page);
		memset(addr, POISON_FREE_INITMEM, PAGE_SIZE);
		__free_page(page);
		totalram_pages++;
	}
	printk(KERN_INFO "Freeing %s: %ldk freed\n", what, (end - begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	free_init_pages("initrd memory",
		virt_to_phys((void *) start),
		virt_to_phys((void *) end));
}
#endif

void __init_refok free_initmem(void)
{
	free_init_pages("unused kernel memory",
	__pa(&__init_begin),
	__pa(&__init_end));
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
