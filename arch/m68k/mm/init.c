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

#if !defined(CONFIG_SUN3) && !defined(CONFIG_COLDFIRE)
extern void init_pointer_table(unsigned long ptable);
extern pmd_t *zero_pgtable;
#endif

#ifdef CONFIG_MMU

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
	struct m68k_mem_info *info = m68k_memory + node;
	int i, end;

	i = (unsigned long)phys_to_virt(info->addr) >> __virt_to_node_shift();
	end = (unsigned long)phys_to_virt(info->addr + info->size - 1) >> __virt_to_node_shift();
	for (; i <= end; i++) {
		if (pg_data_table[i])
			pr_warn("overlap at %u for chunk %u\n", i, node);
		pg_data_table[i] = pg_data_map + node;
	}
#endif
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
	unsigned long zones_size[MAX_NR_ZONES] = { 0, };

	high_memory = (void *) end_mem;

	empty_zero_page = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
	if (!empty_zero_page)
		panic("%s: Failed to allocate %lu bytes align=0x%lx\n",
		      __func__, PAGE_SIZE, PAGE_SIZE);

	/*
	 * Set up SFC/DFC registers (user data space).
	 */
	set_fs (USER_DS);

	zones_size[ZONE_DMA] = (end_mem - PAGE_OFFSET) >> PAGE_SHIFT;
	free_area_init(zones_size);
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
	int i;

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
}

void __init mem_init(void)
{
	/* this will put all memory onto the freelists */
	memblock_free_all();
	init_pointer_tables();
	mem_init_print_info(NULL);
}
