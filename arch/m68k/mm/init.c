/*
 *  linux/arch/m68k/mm/init.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  Contains common initialization routines, specific init code moved
 *  to motorola.c and sun3mmu.c
 */

#include <linux/config.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/setup.h>
#include <asm/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/system.h>
#include <asm/machdep.h>
#include <asm/io.h>
#ifdef CONFIG_ATARI
#include <asm/atari_stram.h>
#endif
#include <asm/tlb.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */

void *empty_zero_page;

void show_mem(void)
{
    unsigned long i;
    int free = 0, total = 0, reserved = 0, shared = 0;
    int cached = 0;

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
	else
	    shared += page_count(mem_map+i) - 1;
    }
    printk("%d pages of RAM\n",total);
    printk("%d free pages\n",free);
    printk("%d reserved pages\n",reserved);
    printk("%d pages shared\n",shared);
    printk("%d pages swap cached\n",cached);
}

extern void init_pointer_table(unsigned long ptable);

/* References to section boundaries */

extern char _text, _etext, _edata, __bss_start, _end;
extern char __init_begin, __init_end;

extern pmd_t *zero_pgtable;

void __init mem_init(void)
{
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	unsigned long tmp;
#ifndef CONFIG_SUN3
	int i;
#endif

	max_mapnr = num_physpages = (((unsigned long)high_memory - PAGE_OFFSET) >> PAGE_SHIFT);

#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI)
		atari_stram_mem_init_hook();
#endif

	/* this will put all memory onto the freelists */
	totalram_pages = free_all_bootmem();

	for (tmp = PAGE_OFFSET ; tmp < (unsigned long)high_memory; tmp += PAGE_SIZE) {
		if (PageReserved(virt_to_page(tmp))) {
			if (tmp >= (unsigned long)&_text
			    && tmp < (unsigned long)&_etext)
				codepages++;
			else if (tmp >= (unsigned long) &__init_begin
				 && tmp < (unsigned long) &__init_end)
				initpages++;
			else
				datapages++;
			continue;
		}
	}

#ifndef CONFIG_SUN3
	/* insert pointer tables allocated so far into the tablelist */
	init_pointer_table((unsigned long)kernel_pg_dir);
	for (i = 0; i < PTRS_PER_PGD; i++) {
		if (pgd_present(kernel_pg_dir[i]))
			init_pointer_table(__pgd_page(kernel_pg_dir[i]));
	}

	/* insert also pointer table that we used to unmap the zero page */
	if (zero_pgtable)
		init_pointer_table((unsigned long)zero_pgtable);
#endif

	printk("Memory: %luk/%luk available (%dk kernel code, %dk data, %dk init)\n",
	       (unsigned long)nr_free_pages() << (PAGE_SHIFT-10),
	       max_mapnr << (PAGE_SHIFT-10),
	       codepages << (PAGE_SHIFT-10),
	       datapages << (PAGE_SHIFT-10),
	       initpages << (PAGE_SHIFT-10));
}

#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long start, unsigned long end)
{
	int pages = 0;
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
		pages++;
	}
	printk ("Freeing initrd memory: %dk freed\n", pages);
}
#endif
