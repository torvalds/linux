// SPDX-License-Identifier: GPL-2.0
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
#include <linux/memblock.h>
#include <linux/gfp.h>

#include <asm/setup.h>
#include <linux/uaccess.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/traps.h>
#include <asm/machdep.h>
#include <asm/io.h>
#ifdef CONFIG_ATARI
#include <asm/atari_stram.h>
#endif
#include <asm/sections.h>
#include <asm/tlb.h>

/*
 * ZERO_PAGE is a special page that is used for zero-initialized
 * data and COW.
 */
void *empty_zero_page;
EXPORT_SYMBOL(empty_zero_page);

#ifdef CONFIG_MMU

int m68k_virt_to_node_shift;

void __init m68k_setup_node(int node)
{
	node_set_online(node);
}

#else /* CONFIG_MMU */

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses of available kernel virtual memory.
 */
void __init paging_init(void)
{
	/*
	 * Make sure start_mem is page aligned, otherwise bootmem and
	 * page_alloc get different views of the world.
	 */
	unsigned long end_mem = memory_end & PAGE_MASK;
	unsigned long max_zone_pfn[MAX_NR_ZONES] = { 0, };

	high_memory = (void *) end_mem;

	empty_zero_page = memblock_alloc_or_panic(PAGE_SIZE, PAGE_SIZE);
	max_zone_pfn[ZONE_DMA] = end_mem >> PAGE_SHIFT;
	free_area_init(max_zone_pfn);
}

#endif /* CONFIG_MMU */

void free_initmem(void)
{
#ifndef CONFIG_MMU_SUN3
	free_initmem_default(-1);
#endif /* CONFIG_MMU_SUN3 */
}

#if defined(CONFIG_MMU) && !defined(CONFIG_COLDFIRE)
#define VECTORS	&vectors[0]
#else
#define VECTORS	_ramvec
#endif

static inline void init_pointer_tables(void)
{
#if defined(CONFIG_MMU) && !defined(CONFIG_SUN3) && !defined(CONFIG_COLDFIRE)
	int i, j;

	/* insert pointer tables allocated so far into the tablelist */
	init_pointer_table(kernel_pg_dir, TABLE_PGD);
	for (i = 0; i < PTRS_PER_PGD; i++) {
		pud_t *pud = (pud_t *)&kernel_pg_dir[i];
		pmd_t *pmd_dir;

		if (!pud_present(*pud))
			continue;

		pmd_dir = (pmd_t *)pgd_page_vaddr(kernel_pg_dir[i]);
		init_pointer_table(pmd_dir, TABLE_PMD);

		for (j = 0; j < PTRS_PER_PMD; j++) {
			pmd_t *pmd = &pmd_dir[j];
			pte_t *pte_dir;

			if (!pmd_present(*pmd))
				continue;

			pte_dir = (pte_t *)pmd_page_vaddr(*pmd);
			init_pointer_table(pte_dir, TABLE_PTE);
		}
	}
#endif
}

void __init mem_init(void)
{
	/* this will put all memory onto the freelists */
	memblock_free_all();
	init_pointer_tables();
}
