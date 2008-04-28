/*
 *  linux/arch/m68k/mm/init.c
 *
 *  Copyright (C) 1995  Hamish Macdonald
 *
 *  Contains common initialization routines, specific init code moved
 *  to motorola.c and sun3mmu.c
 */

#include <linux/module.h>
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

static bootmem_data_t __initdata bootmem_data[MAX_NUMNODES];

pg_data_t pg_data_map[MAX_NUMNODES];
EXPORT_SYMBOL(pg_data_map);

int m68k_virt_to_node_shift;

#ifndef CONFIG_SINGLE_MEMORY_CHUNK
pg_data_t *pg_data_table[65];
EXPORT_SYMBOL(pg_data_table);
#endif

void __init m68k_setup_node(int node)
{
#ifndef CONFIG_SINGLE_MEMORY_CHUNK
	struct mem_info *info = m68k_memory + node;
	int i, end;

	i = (unsigned long)phys_to_virt(info->addr) >> __virt_to_node_shift();
	end = (unsigned long)phys_to_virt(info->addr + info->size - 1) >> __virt_to_node_shift();
	for (; i <= end; i++) {
		if (pg_data_table[i])
			printk("overlap at %u for chunk %u\n", i, node);
		pg_data_table[i] = pg_data_map + node;
	}
#endif
	pg_data_map[node].bdata = bootmem_data + node;
	node_set_online(node);
}


/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */

void *empty_zero_page;

void show_mem(void)
{
	pg_data_t *pgdat;
	int free = 0, total = 0, reserved = 0, shared = 0;
	int cached = 0;
	int i;

	printk("\nMem-info:\n");
	show_free_areas();
	for_each_online_pgdat(pgdat) {
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			struct page *page = pgdat->node_mem_map + i;
			total++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (!page_count(page))
				free++;
			else
				shared += page_count(page) - 1;
		}
	}
	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

extern void init_pointer_table(unsigned long ptable);

/* References to section boundaries */

extern char _text[], _etext[];
extern char __init_begin[], __init_end[];

extern pmd_t *zero_pgtable;

void __init mem_init(void)
{
	pg_data_t *pgdat;
	int codepages = 0;
	int datapages = 0;
	int initpages = 0;
	int i;

#ifdef CONFIG_ATARI
	if (MACH_IS_ATARI)
		atari_stram_mem_init_hook();
#endif

	/* this will put all memory onto the freelists */
	totalram_pages = num_physpages = 0;
	for_each_online_pgdat(pgdat) {
		num_physpages += pgdat->node_present_pages;

		totalram_pages += free_all_bootmem_node(pgdat);
		for (i = 0; i < pgdat->node_spanned_pages; i++) {
			struct page *page = pgdat->node_mem_map + i;
			char *addr = page_to_virt(page);

			if (!PageReserved(page))
				continue;
			if (addr >= _text &&
			    addr < _etext)
				codepages++;
			else if (addr >= __init_begin &&
				 addr < __init_end)
				initpages++;
			else
				datapages++;
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
	       totalram_pages << (PAGE_SHIFT-10),
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
