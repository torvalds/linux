/*
 * Copyright (C) 2004-2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/swap.h>
#include <linux/init.h>
#include <linux/initrd.h>
#include <linux/mmzone.h>
#include <linux/bootmem.h>
#include <linux/pagemap.h>
#include <linux/pfn.h>
#include <linux/nodemask.h>

#include <asm/page.h>
#include <asm/mmu_context.h>
#include <asm/tlb.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/setup.h>
#include <asm/sections.h>

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

pgd_t swapper_pg_dir[PTRS_PER_PGD];

struct page *empty_zero_page;

/*
 * Cache of MMU context last used.
 */
unsigned long mmu_context_cache = NO_CONTEXT;

#define START_PFN	(NODE_DATA(0)->bdata->node_boot_start >> PAGE_SHIFT)
#define MAX_LOW_PFN	(NODE_DATA(0)->bdata->node_low_pfn)

void show_mem(void)
{
	int total = 0, reserved = 0, cached = 0;
	int slab = 0, free = 0, shared = 0;
	pg_data_t *pgdat;

	printk("Mem-info:\n");
	show_free_areas();

	for_each_online_pgdat(pgdat) {
		struct page *page, *end;

		page = pgdat->node_mem_map;
		end = page + pgdat->node_spanned_pages;

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
	}

	printk ("%d pages of RAM\n", total);
	printk ("%d free pages\n", free);
	printk ("%d reserved pages\n", reserved);
	printk ("%d slab pages\n", slab);
	printk ("%d pages shared\n", shared);
	printk ("%d pages swap cached\n", cached);
}

static void __init print_memory_map(const char *what,
				    struct tag_mem_range *mem)
{
	printk ("%s:\n", what);
	for (; mem; mem = mem->next) {
		printk ("  %08lx - %08lx\n",
			(unsigned long)mem->addr,
			(unsigned long)(mem->addr + mem->size));
	}
}

#define MAX_LOWMEM	HIGHMEM_START
#define MAX_LOWMEM_PFN	PFN_DOWN(MAX_LOWMEM)

/*
 * Sort a list of memory regions in-place by ascending address.
 *
 * We're using bubble sort because we only have singly linked lists
 * with few elements.
 */
static void __init sort_mem_list(struct tag_mem_range **pmem)
{
	int done;
	struct tag_mem_range **a, **b;

	if (!*pmem)
		return;

	do {
		done = 1;
		a = pmem, b = &(*pmem)->next;
		while (*b) {
			if ((*a)->addr > (*b)->addr) {
				struct tag_mem_range *tmp;
				tmp = (*b)->next;
				(*b)->next = *a;
				*a = *b;
				*b = tmp;
				done = 0;
			}
			a = &(*a)->next;
			b = &(*a)->next;
		}
	} while (!done);
}

/*
 * Find a free memory region large enough for storing the
 * bootmem bitmap.
 */
static unsigned long __init
find_bootmap_pfn(const struct tag_mem_range *mem)
{
	unsigned long bootmap_pages, bootmap_len;
	unsigned long node_pages = PFN_UP(mem->size);
	unsigned long bootmap_addr = mem->addr;
	struct tag_mem_range *reserved = mem_reserved;
	struct tag_mem_range *ramdisk = mem_ramdisk;
	unsigned long kern_start = virt_to_phys(_stext);
	unsigned long kern_end = virt_to_phys(_end);

	bootmap_pages = bootmem_bootmap_pages(node_pages);
	bootmap_len = bootmap_pages << PAGE_SHIFT;

	/*
	 * Find a large enough region without reserved pages for
	 * storing the bootmem bitmap. We can take advantage of the
	 * fact that all lists have been sorted.
	 *
	 * We have to check explicitly reserved regions as well as the
	 * kernel image and any RAMDISK images...
	 *
	 * Oh, and we have to make sure we don't overwrite the taglist
	 * since we're going to use it until the bootmem allocator is
	 * fully up and running.
	 */
	while (1) {
		if ((bootmap_addr < kern_end) &&
		    ((bootmap_addr + bootmap_len) > kern_start))
			bootmap_addr = kern_end;

		while (reserved &&
		       (bootmap_addr >= (reserved->addr + reserved->size)))
			reserved = reserved->next;

		if (reserved &&
		    ((bootmap_addr + bootmap_len) >= reserved->addr)) {
			bootmap_addr = reserved->addr + reserved->size;
			continue;
		}

		while (ramdisk &&
		       (bootmap_addr >= (ramdisk->addr + ramdisk->size)))
			ramdisk = ramdisk->next;

		if (!ramdisk ||
		    ((bootmap_addr + bootmap_len) < ramdisk->addr))
			break;

		bootmap_addr = ramdisk->addr + ramdisk->size;
	}

	if ((PFN_UP(bootmap_addr) + bootmap_len) >= (mem->addr + mem->size))
		return ~0UL;

	return PFN_UP(bootmap_addr);
}

void __init setup_bootmem(void)
{
	unsigned bootmap_size;
	unsigned long first_pfn, bootmap_pfn, pages;
	unsigned long max_pfn, max_low_pfn;
	unsigned long kern_start = virt_to_phys(_stext);
	unsigned long kern_end = virt_to_phys(_end);
	unsigned node = 0;
	struct tag_mem_range *bank, *res;

	sort_mem_list(&mem_phys);
	sort_mem_list(&mem_reserved);

	print_memory_map("Physical memory", mem_phys);
	print_memory_map("Reserved memory", mem_reserved);

	nodes_clear(node_online_map);

	if (mem_ramdisk) {
#ifdef CONFIG_BLK_DEV_INITRD
		initrd_start = (unsigned long)__va(mem_ramdisk->addr);
		initrd_end = initrd_start + mem_ramdisk->size;

		print_memory_map("RAMDISK images", mem_ramdisk);
		if (mem_ramdisk->next)
			printk(KERN_WARNING
			       "Warning: Only the first RAMDISK image "
			       "will be used\n");
		sort_mem_list(&mem_ramdisk);
#else
		printk(KERN_WARNING "RAM disk image present, but "
		       "no initrd support in kernel!\n");
#endif
	}

	if (mem_phys->next)
		printk(KERN_WARNING "Only using first memory bank\n");

	for (bank = mem_phys; bank; bank = NULL) {
		first_pfn = PFN_UP(bank->addr);
		max_low_pfn = max_pfn = PFN_DOWN(bank->addr + bank->size);
		bootmap_pfn = find_bootmap_pfn(bank);
		if (bootmap_pfn > max_pfn)
			panic("No space for bootmem bitmap!\n");

		if (max_low_pfn > MAX_LOWMEM_PFN) {
			max_low_pfn = MAX_LOWMEM_PFN;
#ifndef CONFIG_HIGHMEM
			/*
			 * Lowmem is memory that can be addressed
			 * directly through P1/P2
			 */
			printk(KERN_WARNING
			       "Node %u: Only %ld MiB of memory will be used.\n",
			       node, MAX_LOWMEM >> 20);
			printk(KERN_WARNING "Use a HIGHMEM enabled kernel.\n");
#else
#error HIGHMEM is not supported by AVR32 yet
#endif
		}

		/* Initialize the boot-time allocator with low memory only. */
		bootmap_size = init_bootmem_node(NODE_DATA(node), bootmap_pfn,
						 first_pfn, max_low_pfn);

		printk("Node %u: bdata = %p, bdata->node_bootmem_map = %p\n",
		       node, NODE_DATA(node)->bdata,
		       NODE_DATA(node)->bdata->node_bootmem_map);

		/*
		 * Register fully available RAM pages with the bootmem
		 * allocator.
		 */
		pages = max_low_pfn - first_pfn;
		free_bootmem_node (NODE_DATA(node), PFN_PHYS(first_pfn),
				   PFN_PHYS(pages));

		/*
		 * Reserve space for the kernel image (if present in
		 * this node)...
		 */
		if ((kern_start >= PFN_PHYS(first_pfn)) &&
		    (kern_start < PFN_PHYS(max_pfn))) {
			printk("Node %u: Kernel image %08lx - %08lx\n",
			       node, kern_start, kern_end);
			reserve_bootmem_node(NODE_DATA(node), kern_start,
					     kern_end - kern_start);
		}

		/* ...the bootmem bitmap... */
		reserve_bootmem_node(NODE_DATA(node),
				     PFN_PHYS(bootmap_pfn),
				     bootmap_size);

		/* ...any RAMDISK images... */
		for (res = mem_ramdisk; res; res = res->next) {
			if (res->addr > PFN_PHYS(max_pfn))
				break;

			if (res->addr >= PFN_PHYS(first_pfn)) {
				printk("Node %u: RAMDISK %08lx - %08lx\n",
				       node,
				       (unsigned long)res->addr,
				       (unsigned long)(res->addr + res->size));
				reserve_bootmem_node(NODE_DATA(node),
						     res->addr, res->size);
			}
		}

		/* ...and any other reserved regions. */
		for (res = mem_reserved; res; res = res->next) {
			if (res->addr > PFN_PHYS(max_pfn))
				break;

			if (res->addr >= PFN_PHYS(first_pfn)) {
				printk("Node %u: Reserved %08lx - %08lx\n",
				       node,
				       (unsigned long)res->addr,
				       (unsigned long)(res->addr + res->size));
				reserve_bootmem_node(NODE_DATA(node),
						     res->addr, res->size);
			}
		}

		node_set_online(node);
	}
}

/*
 * paging_init() sets up the page tables
 *
 * This routine also unmaps the page at virtual kernel address 0, so
 * that we can trap those pesky NULL-reference errors in the kernel.
 */
void __init paging_init(void)
{
	extern unsigned long _evba;
	void *zero_page;
	int nid;

	/*
	 * Make sure we can handle exceptions before enabling
	 * paging. Not that we should ever _get_ any exceptions this
	 * early, but you never know...
	 */
	printk("Exception vectors start at %p\n", &_evba);
	sysreg_write(EVBA, (unsigned long)&_evba);

	/*
	 * Since we are ready to handle exceptions now, we should let
	 * the CPU generate them...
	 */
	__asm__ __volatile__ ("csrf %0" : : "i"(SR_EM_BIT));

	/*
	 * Allocate the zero page. The allocator will panic if it
	 * can't satisfy the request, so no need to check.
	 */
	zero_page = alloc_bootmem_low_pages_node(NODE_DATA(0),
						 PAGE_SIZE);

	{
		pgd_t *pg_dir;
		int i;

		pg_dir = swapper_pg_dir;
		sysreg_write(PTBR, (unsigned long)pg_dir);

		for (i = 0; i < PTRS_PER_PGD; i++)
			pgd_val(pg_dir[i]) = 0;

		enable_mmu();
		printk ("CPU: Paging enabled\n");
	}

	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long zones_size[MAX_NR_ZONES];
		unsigned long low, start_pfn;

		start_pfn = pgdat->bdata->node_boot_start;
		start_pfn >>= PAGE_SHIFT;
		low = pgdat->bdata->node_low_pfn;

		memset(zones_size, 0, sizeof(zones_size));
		zones_size[ZONE_NORMAL] = low - start_pfn;

		printk("Node %u: start_pfn = 0x%lx, low = 0x%lx\n",
		       nid, start_pfn, low);

		free_area_init_node(nid, pgdat, zones_size, start_pfn, NULL);

		printk("Node %u: mem_map starts at %p\n",
		       pgdat->node_id, pgdat->node_mem_map);
	}

	mem_map = NODE_DATA(0)->node_mem_map;

	memset(zero_page, 0, PAGE_SIZE);
	empty_zero_page = virt_to_page(zero_page);
	flush_dcache_page(empty_zero_page);
}

void __init mem_init(void)
{
	int codesize, reservedpages, datasize, initsize;
	int nid, i;

	reservedpages = 0;
	high_memory = NULL;

	/* this will put all low memory onto the freelists */
	for_each_online_node(nid) {
		pg_data_t *pgdat = NODE_DATA(nid);
		unsigned long node_pages = 0;
		void *node_high_memory;

		num_physpages += pgdat->node_present_pages;

		if (pgdat->node_spanned_pages != 0)
			node_pages = free_all_bootmem_node(pgdat);

		totalram_pages += node_pages;

		for (i = 0; i < node_pages; i++)
			if (PageReserved(pgdat->node_mem_map + i))
				reservedpages++;

		node_high_memory = (void *)((pgdat->node_start_pfn
					     + pgdat->node_spanned_pages)
					    << PAGE_SHIFT);
		if (node_high_memory > high_memory)
			high_memory = node_high_memory;
	}

	max_mapnr = MAP_NR(high_memory);

	codesize = (unsigned long)_etext - (unsigned long)_text;
	datasize = (unsigned long)_edata - (unsigned long)_data;
	initsize = (unsigned long)__init_end - (unsigned long)__init_begin;

	printk ("Memory: %luk/%luk available (%dk kernel code, "
		"%dk reserved, %dk data, %dk init)\n",
		(unsigned long)nr_free_pages() << (PAGE_SHIFT - 10),
		totalram_pages << (PAGE_SHIFT - 10),
		codesize >> 10,
		reservedpages << (PAGE_SHIFT - 10),
		datasize >> 10,
		initsize >> 10);
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
		printk(KERN_INFO "Freeing %s memory: %dK (%lx - %lx)\n",
		       s, size, end - (size << 10), end);
}

void free_initmem(void)
{
	free_area((unsigned long)__init_begin, (unsigned long)__init_end,
		  "init");
}

#ifdef CONFIG_BLK_DEV_INITRD

static int keep_initrd;

void free_initrd_mem(unsigned long start, unsigned long end)
{
	if (!keep_initrd)
		free_area(start, end, "initrd");
}

static int __init keepinitrd_setup(char *__unused)
{
	keep_initrd = 1;
	return 1;
}

__setup("keepinitrd", keepinitrd_setup);
#endif
