/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * arch/sh64/mm/init.c
 *
 * Copyright (C) 2000, 2001  Paolo Alberelli
 * Copyright (C) 2003, 2004  Paul Mundt
 *
 */

#include <linux/init.h>
#include <linux/rwsem.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/bootmem.h>

#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/tlb.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache;
pgd_t * mmu_pdtp_cache;
int after_bootmem = 0;

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving an inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */

extern unsigned char empty_zero_page[PAGE_SIZE];
extern unsigned char empty_bad_page[PAGE_SIZE];
extern pte_t empty_bad_pte_table[PTRS_PER_PTE];
extern pgd_t swapper_pg_dir[PTRS_PER_PGD];

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

/* It'd be good if these lines were in the standard header file. */
#define START_PFN	(NODE_DATA(0)->bdata->node_boot_start >> PAGE_SHIFT)
#define MAX_LOW_PFN	(NODE_DATA(0)->bdata->node_low_pfn)


void show_mem(void)
{
	int i, total = 0, reserved = 0;
	int shared = 0, cached = 0;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	i = max_mapnr;
	while (i-- > 0) {
		total++;
		if (PageReserved(mem_map+i))
			reserved++;
		else if (PageSwapCache(mem_map+i))
			cached++;
		else if (page_count(mem_map+i))
			shared += page_count(mem_map+i) - 1;
	}
	printk("%d pages of RAM\n",total);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
	printk("%ld pages in page table cache\n", quicklist_total_size());
}

/*
 * paging_init() sets up the page tables.
 *
 * head.S already did a lot to set up address translation for the kernel.
 * Here we comes with:
 * . MMU enabled
 * . ASID set (SR)
 * .  some 512MB regions being mapped of which the most relevant here is:
 *   . CACHED segment (ASID 0 [irrelevant], shared AND NOT user)
 * . possible variable length regions being mapped as:
 *   . UNCACHED segment (ASID 0 [irrelevant], shared AND NOT user)
 * . All of the memory regions are placed, independently from the platform
 *   on high addresses, above 0x80000000.
 * . swapper_pg_dir is already cleared out by the .space directive
 *   in any case swapper does not require a real page directory since
 *   it's all kernel contained.
 *
 * Those pesky NULL-reference errors in the kernel are then
 * dealt with by not mapping address 0x00000000 at all.
 *
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	pgd_init((unsigned long)swapper_pg_dir);
	pgd_init((unsigned long)swapper_pg_dir +
		 sizeof(pgd_t) * USER_PTRS_PER_PGD);

	mmu_context_cache = MMU_CONTEXT_FIRST_VERSION;

	zones_size[ZONE_NORMAL] = MAX_LOW_PFN - START_PFN;
	NODE_DATA(0)->node_mem_map = NULL;
	free_area_init_node(0, NODE_DATA(0), zones_size, __MEMORY_START >> PAGE_SHIFT, 0);
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	int tmp;

	max_mapnr = num_physpages = MAX_LOW_PFN - START_PFN;
	high_memory = (void *)__va(MAX_LOW_PFN * PAGE_SIZE);

	/*
         * Clear the zero-page.
         * This is not required but we might want to re-use
         * this very page to pass boot parameters, one day.
         */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	totalram_pages += free_all_bootmem_node(NODE_DATA(0));
	reservedpages = 0;
	for (tmp = 0; tmp < num_physpages; tmp++)
		/*
		 * Only count reserved RAM pages
		 */
		if (PageReserved(mem_map+tmp))
			reservedpages++;

	after_bootmem = 1;

	codesize =  (unsigned long) &_etext - (unsigned long) &_text;
	datasize =  (unsigned long) &_edata - (unsigned long) &_etext;
	initsize =  (unsigned long) &__init_end - (unsigned long) &__init_begin;

	printk("Memory: %luk/%luk available (%dk kernel code, %dk reserved, %dk data, %dk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		max_mapnr << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);
}

void free_initmem(void)
{
	unsigned long addr;

	addr = (unsigned long)(&__init_begin);
	for (; addr < (unsigned long)(&__init_end); addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}
	printk ("Freeing unused kernel memory: %ldk freed\n", (&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	unsigned long p;
	for (p = start; p < end; p += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(p));
		init_page_count(virt_to_page(p));
		free_page(p);
		totalram_pages++;
	}
	printk ("Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

