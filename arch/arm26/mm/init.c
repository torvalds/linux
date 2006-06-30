/*
 *  linux/arch/arm26/mm/init.c
 *
 *  Copyright (C) 1995-2002 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
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
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/bootmem.h>
#include <linux/blkdev.h>
#include <linux/pfn.h>

#include <asm/segment.h>
#include <asm/mach-types.h>
#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/setup.h>
#include <asm/tlb.h>

#include <asm/map.h>


#define TABLE_SIZE	PTRS_PER_PTE * sizeof(pte_t))

struct mmu_gather mmu_gathers[NR_CPUS];

extern pgd_t swapper_pg_dir[PTRS_PER_PGD];
extern char _stext, _text, _etext, _end, __init_begin, __init_end;
#ifdef CONFIG_XIP_KERNEL
extern char _endtext, _sdata;
#endif
extern unsigned long phys_initrd_start;
extern unsigned long phys_initrd_size;

/*
 * The sole use of this is to pass memory configuration
 * data from paging_init to mem_init.
 */
static struct meminfo meminfo __initdata = { 0, };

/*
 * empty_zero_page is a special page that is used for
 * zero-initialized data and COW.
 */
struct page *empty_zero_page;

void show_mem(void)
{
	int free = 0, total = 0, reserved = 0;
	int shared = 0, cached = 0, slab = 0;
	struct page *page, *end;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n", nr_swap_pages<<(PAGE_SHIFT-10));


	page = NODE_MEM_MAP(0);
	end  = page + NODE_DATA(0)->node_spanned_pages;

	do {
		total++;
		if (PageReserved(page))
			reserved++;
		else if (PageSwapCache(page))
			cached++;
		else if (PageSlab(page))
			slab++;
		else if (!page_count(page))
			free++;
		else
			shared += page_count(page) - 1;
		page++;
	} while (page < end);

	printk("%d pages of RAM\n", total);
	printk("%d free pages\n", free);
	printk("%d reserved pages\n", reserved);
	printk("%d slab pages\n", slab);
	printk("%d pages shared\n", shared);
	printk("%d pages swap cached\n", cached);
}

struct node_info {
	unsigned int start;
	unsigned int end;
	int bootmap_pages;
};

/*
 * FIXME: We really want to avoid allocating the bootmap bitmap
 * over the top of the initrd.  Hopefully, this is located towards
 * the start of a bank, so if we allocate the bootmap bitmap at
 * the end, we won't clash.
 */
static unsigned int __init
find_bootmap_pfn(struct meminfo *mi, unsigned int bootmap_pages)
{
	unsigned int start_pfn, bootmap_pfn;
	unsigned int start, end;

	start_pfn   = PFN_UP((unsigned long)&_end);
	bootmap_pfn = 0;

	/* ARM26 machines only have one node */
	if (mi->bank->node != 0)
		BUG();

	start = PFN_UP(mi->bank->start);
	end   = PFN_DOWN(mi->bank->size + mi->bank->start);

	if (start < start_pfn)
		start = start_pfn;

	if (end <= start)
		BUG();

	if (end - start >= bootmap_pages) 
		bootmap_pfn = start;
	else
		BUG();

	return bootmap_pfn;
}

/*
 * Scan the memory info structure and pull out:
 *  - the end of memory
 *  - the number of nodes
 *  - the pfn range of each node
 *  - the number of bootmem bitmap pages
 */
static void __init
find_memend_and_nodes(struct meminfo *mi, struct node_info *np)
{
	unsigned int memend_pfn = 0;

	nodes_clear(node_online_map);
	node_set_online(0);

	np->bootmap_pages = 0;

	if (mi->bank->size == 0) {
		BUG();
	}

	/*
	 * Get the start and end pfns for this bank
	 */
	np->start = PFN_UP(mi->bank->start);
	np->end   = PFN_DOWN(mi->bank->start + mi->bank->size);

	if (memend_pfn < np->end)
		memend_pfn = np->end;

	/*
	 * Calculate the number of pages we require to
	 * store the bootmem bitmaps.
	 */
	np->bootmap_pages = bootmem_bootmap_pages(np->end - np->start);

	/*
	 * This doesn't seem to be used by the Linux memory
	 * manager any more.  If we can get rid of it, we
	 * also get rid of some of the stuff above as well.
	 */
	max_low_pfn = memend_pfn - PFN_DOWN(PHYS_OFFSET);
	max_pfn = memend_pfn - PFN_DOWN(PHYS_OFFSET);
	mi->end = memend_pfn << PAGE_SHIFT;

}

/*
 * Initialise the bootmem allocator for all nodes.  This is called
 * early during the architecture specific initialisation.
 */
void __init bootmem_init(struct meminfo *mi)
{
	struct node_info node_info;
	unsigned int bootmap_pfn;
	pg_data_t *pgdat = NODE_DATA(0);

	find_memend_and_nodes(mi, &node_info);

	bootmap_pfn   = find_bootmap_pfn(mi, node_info.bootmap_pages);

	/*
	 * Note that node 0 must always have some pages.
	 */
	if (node_info.end == 0)
		BUG();

	/*
	 * Initialise the bootmem allocator.
	 */
	init_bootmem_node(pgdat, bootmap_pfn, node_info.start, node_info.end);

 	/*
	 * Register all available RAM in this node with the bootmem allocator. 
	 */
	free_bootmem_node(pgdat, mi->bank->start, mi->bank->size);

        /*
         * Register the kernel text and data with bootmem.
         * Note: with XIP we dont register .text since
         * its in ROM.
         */
#ifdef CONFIG_XIP_KERNEL
        reserve_bootmem_node(pgdat, __pa(&_sdata), &_end - &_sdata);
#else
        reserve_bootmem_node(pgdat, __pa(&_stext), &_end - &_stext);
#endif

        /*
         * And don't forget to reserve the allocator bitmap,
         * which will be freed later.
         */
        reserve_bootmem_node(pgdat, bootmap_pfn << PAGE_SHIFT,
                             node_info.bootmap_pages << PAGE_SHIFT);

        /*
         * These should likewise go elsewhere.  They pre-reserve
         * the screen memory region at the start of main system
         * memory. FIXME - screen RAM is not 512K!
         */
        reserve_bootmem_node(pgdat, 0x02000000, 0x00080000);

#ifdef CONFIG_BLK_DEV_INITRD
        initrd_start = phys_initrd_start;
        initrd_end = initrd_start + phys_initrd_size;

        /* Achimedes machines only have one node, so initrd is in node 0 */
#ifdef CONFIG_XIP_KERNEL
	/* Only reserve initrd space if it is in RAM */
        if(initrd_start && initrd_start < 0x03000000){
#else
        if(initrd_start){
#endif
                reserve_bootmem_node(pgdat, __pa(initrd_start),
                                             initrd_end - initrd_start);
	}
#endif   /* CONFIG_BLK_DEV_INITRD */


}

/*
 * paging_init() sets up the page tables, initialises the zone memory
 * maps, and sets up the zero page, bad page and bad page tables.
 */
void __init paging_init(struct meminfo *mi)
{
	void *zero_page;
	unsigned long zone_size[MAX_NR_ZONES];
        unsigned long zhole_size[MAX_NR_ZONES];
        struct bootmem_data *bdata;
        pg_data_t *pgdat;
	int i;

	memcpy(&meminfo, mi, sizeof(meminfo));

	/*
	 * allocate the zero page.  Note that we count on this going ok.
	 */
	zero_page = alloc_bootmem_low_pages(PAGE_SIZE);

	/*
	 * initialise the page tables.
	 */
	memtable_init(mi);
	flush_tlb_all();

	/*
	 * initialise the zones in node 0 (archimedes have only 1 node)
	 */

	for (i = 0; i < MAX_NR_ZONES; i++) {
		zone_size[i]  = 0;
		zhole_size[i] = 0;
	}

	pgdat = NODE_DATA(0);
	bdata = pgdat->bdata;
	zone_size[0] = bdata->node_low_pfn -
			(bdata->node_boot_start >> PAGE_SHIFT);
	if (!zone_size[0])
		BUG();
	pgdat->node_mem_map = NULL;
	free_area_init_node(0, pgdat, zone_size,
			bdata->node_boot_start >> PAGE_SHIFT, zhole_size);

	/*
	 * finish off the bad pages once
	 * the mem_map is initialised
	 */
	memzero(zero_page, PAGE_SIZE);
	empty_zero_page = virt_to_page(zero_page);
}

static inline void free_area(unsigned long addr, unsigned long end, char *s)
{
	unsigned int size = (end - addr) >> 10;

	for (; addr < end; addr += PAGE_SIZE) {
		struct page *page = virt_to_page(addr);
		ClearPageReserved(page);
		init_page_count(page);
		free_page(addr);
		totalram_pages++;
	}

	if (size && s)
		printk(KERN_INFO "Freeing %s memory: %dK\n", s, size);
}

/*
 * mem_init() marks the free areas in the mem_map and tells us how much
 * memory is free.  This is done after various parts of the system have
 * claimed their memory after the kernel image.
 */
void __init mem_init(void)
{
	unsigned int codepages, datapages, initpages;
	pg_data_t *pgdat = NODE_DATA(0);
	extern int sysctl_overcommit_memory;


	/* Note: data pages includes BSS */
#ifdef CONFIG_XIP_KERNEL
	codepages = &_endtext - &_text;
	datapages = &_end - &_sdata;
#else
	codepages = &_etext - &_text;
	datapages = &_end - &_etext;
#endif
	initpages = &__init_end - &__init_begin;

	high_memory = (void *)__va(meminfo.end);
	max_mapnr   = virt_to_page(high_memory) - mem_map;

	/* this will put all unused low memory onto the freelists */
	if (pgdat->node_spanned_pages != 0)
		totalram_pages += free_all_bootmem_node(pgdat);

	num_physpages = meminfo.bank[0].size >> PAGE_SHIFT;

	printk(KERN_INFO "Memory: %luMB total\n", num_physpages >> (20 - PAGE_SHIFT));
	printk(KERN_NOTICE "Memory: %luKB available (%dK code, "
		"%dK data, %dK init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		codepages >> 10, datapages >> 10, initpages >> 10);

	/*
	 * Turn on overcommit on tiny machines
	 */
	if (PAGE_SIZE >= 16384 && num_physpages <= 128) {
		sysctl_overcommit_memory = OVERCOMMIT_ALWAYS;
		printk("Turning on overcommit\n");
	}
}

void free_initmem(void){
#ifndef CONFIG_XIP_KERNEL
	free_area((unsigned long)(&__init_begin),
		  (unsigned long)(&__init_end),
		  "init");
#endif
}

#ifdef CONFIG_BLK_DEV_INITRD

static int keep_initrd;

void free_initrd_mem(unsigned long start, unsigned long end)
{
#ifdef CONFIG_XIP_KERNEL
	/* Only bin initrd if it is in RAM... */
	if(!keep_initrd && start < 0x03000000)
#else
	if (!keep_initrd)
#endif
		free_area(start, end, "initrd");
}

static int __init keepinitrd_setup(char *__unused)
{
	keep_initrd = 1;
	return 1;
}

__setup("keepinitrd", keepinitrd_setup);
#endif
