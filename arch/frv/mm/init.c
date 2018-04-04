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
#include <linux/sched/task.h>
#include <linux/pagemap.h>
#include <linux/gfp.h>
#include <linux/swap.h>
#include <linux/mm.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/bootmem.h>
#include <linux/highmem.h>
#include <linux/module.h>

#include <asm/setup.h>
#include <asm/segment.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/mmu_context.h>
#include <asm/virtconvert.h>
#include <asm/sections.h>
#include <asm/tlb.h>

#undef DEBUG

/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
unsigned long empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

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
	empty_zero_page		= (unsigned long) alloc_bootmem_pages(PAGE_SIZE);

	memset((void *) empty_zero_page, 0, PAGE_SIZE);

#ifdef CONFIG_HIGHMEM
	if (get_num_physpages() - num_mappedpages) {
		pgd_t *pge;
		pud_t *pue;
		pmd_t *pme;

		pkmap_page_table = alloc_bootmem_pages(PAGE_SIZE);

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
	zones_size[ZONE_HIGHMEM] = get_num_physpages() - num_mappedpages;
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
	unsigned long code_size = _etext - _stext;

	/* this will put all low memory onto the freelists */
	free_all_bootmem();
#if defined(CONFIG_MMU) && defined(CONFIG_HIGHMEM)
	{
		unsigned long pfn;

		for (pfn = get_num_physpages() - 1;
		     pfn >= num_mappedpages; pfn--)
			free_highmem_page(&mem_map[pfn]);
	}
#endif

	mem_init_print_info(NULL);
	if (rom_length > 0 && rom_length >= code_size)
		printk("Memory available:  %luKiB/%luKiB ROM\n",
			(rom_length - code_size) >> 10, rom_length >> 10);
} /* end mem_init() */

/*****************************************************************************/
/*
 * free the memory that was only required for initialisation
 */
void free_initmem(void)
{
#if defined(CONFIG_RAMKERNEL) && !defined(CONFIG_PROTECT_KERNEL)
	free_initmem_default(-1);
#endif
} /* end free_initmem() */

/*****************************************************************************/
/*
 * free the initial ramdisk memory
 */
#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area((void *)start, (void *)end, -1, "initrd");
} /* end free_initrd_mem() */
#endif
