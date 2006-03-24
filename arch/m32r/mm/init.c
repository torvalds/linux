/*
 *  linux/arch/m32r/mm/init.c
 *
 *  Copyright (c) 2001, 2002  Hitoshi Yamamoto
 *
 *  Some code taken from sh version.
 *    Copyright (C) 1999  Niibe Yutaka
 *    Based on linux/arch/i386/mm/init.c:
 *      Copyright (C) 1995  Linus Torvalds
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/swap.h>
#include <linux/highmem.h>
#include <linux/bitops.h>
#include <linux/nodemask.h>
#include <asm/types.h>
#include <asm/processor.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/setup.h>
#include <asm/tlb.h>

/* References to section boundaries */
extern char _text, _etext, _edata;
extern char __init_begin, __init_end;

pgd_t swapper_pg_dir[1024];

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

void show_mem(void)
{
	int total = 0, reserved = 0;
	int shared = 0, cached = 0;
	int highmem = 0;
	struct page *page;
	pg_data_t *pgdat;
	unsigned long i;

	printk("Mem-info:\n");
	show_free_areas();
	printk("Free swap:       %6ldkB\n",nr_swap_pages<<(PAGE_SHIFT-10));
	for_each_pgdat(pgdat) {
		unsigned long flags;
		pgdat_resize_lock(pgdat, &flags);
		for (i = 0; i < pgdat->node_spanned_pages; ++i) {
			page = pgdat_page_nr(pgdat, i);
			total++;
			if (PageHighMem(page))
				highmem++;
			if (PageReserved(page))
				reserved++;
			else if (PageSwapCache(page))
				cached++;
			else if (page_count(page))
				shared += page_count(page) - 1;
		}
		pgdat_resize_unlock(pgdat, &flags);
	}
	printk("%d pages of RAM\n", total);
	printk("%d pages of HIGHMEM\n",highmem);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);
	printk("%d pages swap cached\n",cached);
}

/*
 * Cache of MMU context last used.
 */
#ifndef CONFIG_SMP
unsigned long mmu_context_cache_dat;
#else
unsigned long mmu_context_cache_dat[NR_CPUS];
#endif
static unsigned long hole_pages;

/*
 * function prototype
 */
void __init paging_init(void);
void __init mem_init(void);
void free_initmem(void);
#ifdef CONFIG_BLK_DEV_INITRD
void free_initrd_mem(unsigned long, unsigned long);
#endif

/* It'd be good if these lines were in the standard header file. */
#define START_PFN(nid)	\
	(NODE_DATA(nid)->bdata->node_boot_start >> PAGE_SHIFT)
#define MAX_LOW_PFN(nid)	(NODE_DATA(nid)->bdata->node_low_pfn)

#ifndef CONFIG_DISCONTIGMEM
unsigned long __init zone_sizes_init(void)
{
	unsigned long  zones_size[MAX_NR_ZONES] = {0, 0, 0};
	unsigned long  max_dma;
	unsigned long  low;
	unsigned long  start_pfn;

#ifdef CONFIG_MMU
	start_pfn = START_PFN(0);
	max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;
	low = MAX_LOW_PFN(0);

	if (low < max_dma){
		zones_size[ZONE_DMA] = low - start_pfn;
		zones_size[ZONE_NORMAL] = 0;
	} else {
		zones_size[ZONE_DMA] = low - start_pfn;
		zones_size[ZONE_NORMAL] = low - max_dma;
	}
#else
	zones_size[ZONE_DMA] = 0 >> PAGE_SHIFT;
	zones_size[ZONE_NORMAL] = __MEMORY_SIZE >> PAGE_SHIFT;
	start_pfn = __MEMORY_START >> PAGE_SHIFT;
#endif /* CONFIG_MMU */

	free_area_init_node(0, NODE_DATA(0), zones_size, start_pfn, 0);

	return 0;
}
#else	/* CONFIG_DISCONTIGMEM */
extern unsigned long zone_sizes_init(void);
#endif	/* CONFIG_DISCONTIGMEM */

/*======================================================================*
 * paging_init() : sets up the page tables
 *======================================================================*/
void __init paging_init(void)
{
#ifdef CONFIG_MMU
	int  i;
	pgd_t *pg_dir;

	/* We don't need kernel mapping as hardware support that. */
	pg_dir = swapper_pg_dir;

	for (i = 0 ; i < USER_PTRS_PER_PGD * 2 ; i++)
		pgd_val(pg_dir[i]) = 0;
#endif /* CONFIG_MMU */
	hole_pages = zone_sizes_init();
}

int __init reservedpages_count(void)
{
	int reservedpages, nid, i;

	reservedpages = 0;
	for_each_online_node(nid) {
		unsigned long flags;
		pgdat_resize_lock(NODE_DATA(nid), &flags);
		for (i = 0 ; i < MAX_LOW_PFN(nid) - START_PFN(nid) ; i++)
			if (PageReserved(nid_page_nr(nid, i)))
				reservedpages++;
		pgdat_resize_unlock(NODE_DATA(nid), &flags);
	}

	return reservedpages;
}

/*======================================================================*
 * mem_init() :
 * orig : arch/sh/mm/init.c
 *======================================================================*/
void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	int nid;
#ifndef CONFIG_MMU
	extern unsigned long memory_end;
#endif

	num_physpages = 0;
	for_each_online_node(nid)
		num_physpages += MAX_LOW_PFN(nid) - START_PFN(nid) + 1;

	num_physpages -= hole_pages;

#ifndef CONFIG_DISCONTIGMEM
	max_mapnr = num_physpages;
#endif	/* CONFIG_DISCONTIGMEM */

#ifdef CONFIG_MMU
	high_memory = (void *)__va(PFN_PHYS(MAX_LOW_PFN(0)));
#else
	high_memory = (void *)(memory_end & PAGE_MASK);
#endif /* CONFIG_MMU */

	/* clear the zero-page */
	memset(empty_zero_page, 0, PAGE_SIZE);

	/* this will put all low memory onto the freelists */
	for_each_online_node(nid)
		totalram_pages += free_all_bootmem_node(NODE_DATA(nid));

	reservedpages = reservedpages_count() - hole_pages;
	codesize = (unsigned long) &_etext - (unsigned long)&_text;
	datasize = (unsigned long) &_edata - (unsigned long)&_etext;
	initsize = (unsigned long) &__init_end - (unsigned long)&__init_begin;

	printk(KERN_INFO "Memory: %luk/%luk available (%dk kernel code, "
		"%dk reserved, %dk data, %dk init)\n",
		(unsigned long) nr_free_pages() << (PAGE_SHIFT-10),
		num_physpages << (PAGE_SHIFT-10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT-10),
		datasize >> 10,
		initsize >> 10);
}

/*======================================================================*
 * free_initmem() :
 * orig : arch/sh/mm/init.c
 *======================================================================*/
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
	printk (KERN_INFO "Freeing unused kernel memory: %dk freed\n", \
	  (int)(&__init_end - &__init_begin) >> 10);
}

#ifdef CONFIG_BLK_DEV_INITRD
/*======================================================================*
 * free_initrd_mem() :
 * orig : arch/sh/mm/init.c
 *======================================================================*/
void free_initrd_mem(unsigned long start, unsigned long end)
{
	unsigned long p;
	for (p = start; p < end; p += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(p));
		init_page_count(virt_to_page(p));
		free_page(p);
		totalram_pages++;
	}
	printk (KERN_INFO "Freeing initrd memory: %ldk freed\n", (end - start) >> 10);
}
#endif

