/* init.c: memory initialisation for FRV
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Derived from:
 *  - linux/arch/m68knommu/mm/init.c
 *    - Copyright (C) 1998  D. Jeff Dionne <jeff@lineo.ca>, Kenneth Albanowski <kjahds@kjahds.com>,
 *    - Copyright (C) 2000  Lineo, Inc.  (www.lineo.com)
 *  - linux/arch/m68k/mm/init.c
 *    - Copyright (C) 1995  Hamish Macdonald
 */

#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/pagemap.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/system.h>
#include <asm/mmu_context.h>
#include <asm/virtconvert.h>
#include <asm/sections.h>
#include <asm/tlb.h>

#undef DEBUG

DEFINE_PER_CPU(struct mmu_gather, mmu_gathers);

/*
 * BAD_PAGE is the page that is used for page faults when linux
 * is out-of-memory. Older versions of linux just did a
 * do_exit(), but using this instead means there is less risk
 * for a process dying in kernel mode, possibly leaving a inode
 * unused etc..
 *
 * BAD_PAGETABLE is the accompanying page-table: it is initialized
 * to point to BAD_PAGE entries.
 *
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
static unsigned long empty_bad_page_table;
static unsigned long empty_bad_page;
unsigned long empty_zero_page;

/*****************************************************************************/
/*
 *
 */
void show_mem(void)
{
	unsigned long i;
	int free = 0, total = 0, reserved = 0, shared = 0;

	printk("\nMem-info:\n");
	show_free_areas();
	i = max_mapnr;
	while (i-- > 0) {
		struct page *page = &mem_map[i];

		total++;
		if (PageReserved(page))
			reserved++;
		else if (!page_count(page))
			free++;
		else
			shared += page_count(page) - 1;
	}

	printk("%d pages of RAM\n",total);
	printk("%d free pages\n",free);
	printk("%d reserved pages\n",reserved);
	printk("%d pages shared\n",shared);

} /* end show_mem() */

/*****************************************************************************/
/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses  of available kernel virtual memory.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES] = {0, };

	/* allocate some pages for kernel housekeeping tasks */
	empty_bad_page_table	= (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
	empty_bad_page		= (unsigned long) alloc_bootmem_pages(PAGE_SIZE);
	empty_zero_page		= (unsigned long) alloc_bootmem_pages(PAGE_SIZE);

	memset((void *) empty_zero_page, 0, PAGE_SIZE);

#ifdef CONFIG_HIGHMEM
	if (num_physpages - num_mappedpages) {
		pgd_t *pge;
		pud_t *pue;
		pmd_t *pme;

		pkmap_page_table = alloc_bootmem_pages(PAGE_SIZE);

		memset(pkmap_page_table, 0, PAGE_SIZE);

		pge = swapper_pg_dir + pgd_index_k(PKMAP_BASE);
		pue = pud_offset(pge, PKMAP_BASE);
		pme = pmd_offset(pue, PKMAP_BASE);
		__set_pmd(pme, virt_to_phys(pkmap_page_table) | _PAGE_TABLE);
	}
#endif

	/* distribute the allocatable pages across the various zones and pass them to the allocator
	 */
	zones_size[ZONE_NORMAL]  = max_low_pfn - min_low_pfn;
#ifdef CONFIG_HIGHMEM
	zones_size[ZONE_HIGHMEM] = num_physpages - num_mappedpages;
#endif

	free_area_init(zones_size);

#ifdef CONFIG_MMU
	/* initialise init's MMU context */
	init_new_context(&init_task, &init_mm);
#endif

} /* end paging_init() */

/*****************************************************************************/
/*
 *
 */
void __init mem_init(void)
{
	unsigned long npages = (memory_end - memory_start) >> PAGE_SHIFT;
	unsigned long tmp;
#ifdef CONFIG_MMU
	unsigned long loop, pfn;
	int datapages = 0;
#endif
	int codek = 0, datak = 0;

	/* this will put all memory onto the freelists */
	totalram_pages = free_all_bootmem();

#ifdef CONFIG_MMU
	for (loop = 0 ; loop < npages ; loop++)
		if (PageReserved(&mem_map[loop]))
			datapages++;

#ifdef CONFIG_HIGHMEM
	for (pfn = num_physpages - 1; pfn >= num_mappedpages; pfn--) {
		struct page *page = &mem_map[pfn];

		ClearPageReserved(page);
		init_page_count(page);
		__free_page(page);
		totalram_pages++;
	}
#endif

	codek = ((unsigned long) &_etext - (unsigned long) &_stext) >> 10;
	datak = datapages << (PAGE_SHIFT - 10);

#else
	codek = (_etext - _stext) >> 10;
	datak = 0; //(_ebss - _sdata) >> 10;
#endif

	tmp = nr_free_pages() << PAGE_SHIFT;
	printk("Memory available: %luKiB/%luKiB RAM, %luKiB/%luKiB ROM (%dKiB kernel code, %dKiB data)\n",
	       tmp >> 10,
	       npages << (PAGE_SHIFT - 10),
	       (rom_length > 0) ? ((rom_length >> 10) - codek) : 0,
	       rom_length >> 10,
	       codek,
	       datak
	       );

} /* end mem_init() */

/*****************************************************************************/
/*
 * free the memory that was only required for initialisation
 */
void free_initmem(void)
{
#if defined(CONFIG_RAMKERNEL) && !defined(CONFIG_PROTECT_KERNEL)
	unsigned long start, end, addr;

	start = PAGE_ALIGN((unsigned long) &__init_begin);	/* round up */
	end   = ((unsigned long) &__init_end) & PAGE_MASK;	/* round down */

	/* next to check that the page we free is not a partial page */
	for (addr = start; addr < end; addr += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(addr));
		init_page_count(virt_to_page(addr));
		free_page(addr);
		totalram_pages++;
	}

	printk("Freeing unused kernel memory: %ldKiB freed (0x%lx - 0x%lx)\n",
	       (end - start) >> 10, start, end);
#endif
} /* end free_initmem() */

/*****************************************************************************/
/*
 * free the initial ramdisk memory
 */
#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	int pages = 0;
	for (; start < end; start += PAGE_SIZE) {
		ClearPageReserved(virt_to_page(start));
		init_page_count(virt_to_page(start));
		free_page(start);
		totalram_pages++;
		pages++;
	}
	printk("Freeing initrd memory: %dKiB freed\n", (pages * PAGE_SIZE) >> 10);
} /* end free_initrd_mem() */
#endif
