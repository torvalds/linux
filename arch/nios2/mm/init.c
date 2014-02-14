/*
 * Copyright (C) 2010 Tobias Klauser <tklauser@distanz.ch>
 * Copyright (C) 2009 Wind River Systems Inc
 *   Implemented by fredrik.markstrom@gmail.com and ivarholmqvist@gmail.com
 * Copyright (C) 2004 Microtronix Datacom Ltd
 *
 * based on arch/m68k/mm/init.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
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
#include <linux/init.h>
#include <linux/pagemap.h>
#include <linux/bootmem.h>
#include <linux/slab.h>

#include <asm/setup.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/tlb.h>
#include <asm/mmu_context.h>
#include <asm/cpuinfo.h>

pgd_t *pgd_current;

/*
 * paging_init() continues the virtual memory environment setup which
 * was begun by the code in arch/head.S.
 * The parameters are pointers to where to stick the starting and ending
 * addresses of available kernel virtual memory.
 */
void __init paging_init(void)
{
	unsigned long zones_size[MAX_NR_ZONES];
	unsigned long start_mem, end_mem;

	memset(zones_size, 0, sizeof(zones_size));

	/*
	 * Make sure start_mem is page aligned, otherwise bootmem and
	 * page_alloc get different views of the world.
	 */
	start_mem = PHYS_OFFSET;
	end_mem   = memory_end;

	pagetable_init();
	pgd_current = swapper_pg_dir;

	/*
	 * Set up SFC/DFC registers (user data space).
	 */
	zones_size[ZONE_DMA] = ((end_mem - start_mem) >> PAGE_SHIFT);

	/* pass the memory from the bootmem allocator to the main allocator */
	free_area_init(zones_size);
}

void __init mem_init(void)
{
	unsigned int codek = 0, datak = 0;
	unsigned long end_mem   = memory_end; /* this must not include
						kernel stack at top */

	pr_debug("mem_init: start=%lx, end=%lx\n", memory_start, memory_end);

	end_mem &= PAGE_MASK;
	high_memory = __va(end_mem);

	max_mapnr = ((unsigned long)end_mem) >> PAGE_SHIFT;

	num_physpages = max_mapnr;
	pr_debug("We have %ld pages of RAM\n", num_physpages);

	/* this will put all memory onto the freelists */
	totalram_pages = free_all_bootmem();

	codek = (_etext - _stext) >> 10;
	datak = (_end - _etext) >> 10;

	pr_info("Memory available: %luk/%luk RAM (%dk kernel code, %dk data)\n",
		nr_free_pages() << (PAGE_SHIFT - 10),
		(unsigned long)((_end - _stext) >> 10),
		codek, datak);
}

void __init mmu_init(void)
{
	flush_tlb_all();
}

#ifdef CONFIG_BLK_DEV_INITRD
void __init free_initrd_mem(unsigned long start, unsigned long end)
{
	free_reserved_area(start, end, 0, "initrd");
}
#endif

void __init_refok free_initmem(void)
{
	free_initmem_default(0);
}

#define __page_aligned(order) __aligned(PAGE_SIZE << (order))
pgd_t swapper_pg_dir[PTRS_PER_PGD] __page_aligned(PGD_ORDER);
pte_t invalid_pte_table[PTRS_PER_PTE] __page_aligned(PTE_ORDER);
