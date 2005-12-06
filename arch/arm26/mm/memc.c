/*
 *  linux/arch/arm26/mm/memc.c
 *
 *  Copyright (C) 1998-2000 Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  Page table sludge for older ARM processor architectures.
 */
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/bootmem.h>

#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/page.h>
#include <asm/memory.h>
#include <asm/hardware.h>

#include <asm/map.h>

#define MEMC_TABLE_SIZE (256*sizeof(unsigned long))

kmem_cache_t *pte_cache, *pgd_cache;
int page_nr;

/*
 * Allocate space for a page table and a MEMC table.
 * Note that we place the MEMC
 * table before the page directory.  This means we can
 * easily get to both tightly-associated data structures
 * with a single pointer.
 */
static inline pgd_t *alloc_pgd_table(void)
{
	void *pg2k = kmem_cache_alloc(pgd_cache, GFP_KERNEL);

	if (pg2k)
		pg2k += MEMC_TABLE_SIZE;

	return (pgd_t *)pg2k;
}

/*
 * Free a page table. this function is the counterpart to get_pgd_slow
 * below, not alloc_pgd_table above.
 */
void free_pgd_slow(pgd_t *pgd)
{
	unsigned long tbl = (unsigned long)pgd;

	tbl -= MEMC_TABLE_SIZE;

	kmem_cache_free(pgd_cache, (void *)tbl);
}

/*
 * Allocate a new pgd and fill it in ready for use
 *
 * A new tasks pgd is completely empty (all pages !present) except for:
 *
 * o The machine vectors at virtual address 0x0
 * o The vmalloc region at the top of address space
 *
 */
#define FIRST_KERNEL_PGD_NR     (FIRST_USER_PGD_NR + USER_PTRS_PER_PGD)

pgd_t *get_pgd_slow(struct mm_struct *mm)
{
	pgd_t *new_pgd, *init_pgd;
	pmd_t *new_pmd, *init_pmd;
	pte_t *new_pte, *init_pte;

	new_pgd = alloc_pgd_table();
	if (!new_pgd)
		goto no_pgd;

	/*
	 * On ARM, first page must always be allocated since it contains
	 * the machine vectors.
	 */
	new_pmd = pmd_alloc(mm, new_pgd, 0);
	if (!new_pmd)
		goto no_pmd;

	new_pte = pte_alloc_map(mm, new_pmd, 0);
	if (!new_pte)
		goto no_pte;

	init_pgd = pgd_offset(&init_mm, 0);
	init_pmd = pmd_offset(init_pgd, 0);
	init_pte = pte_offset(init_pmd, 0);

	set_pte(new_pte, *init_pte);
	pte_unmap(new_pte);

	/*
	 * the page table entries are zeroed
	 * when the table is created. (see the cache_ctor functions below)
	 * Now we need to plonk the kernel (vmalloc) area at the end of
	 * the address space. We copy this from the init thread, just like
	 * the init_pte we copied above...
	 */
	memcpy(new_pgd + FIRST_KERNEL_PGD_NR, init_pgd + FIRST_KERNEL_PGD_NR,
		(PTRS_PER_PGD - FIRST_KERNEL_PGD_NR) * sizeof(pgd_t));

	/* update MEMC tables */
	cpu_memc_update_all(new_pgd);
	return new_pgd;

no_pte:
	pmd_free(new_pmd);
no_pmd:
	free_pgd_slow(new_pgd);
no_pgd:
	return NULL;
}

/*
 * No special code is required here.
 */
void setup_mm_for_reboot(char mode)
{
}

/*
 * This contains the code to setup the memory map on an ARM2/ARM250/ARM3
 *  o swapper_pg_dir = 0x0207d000
 *  o kernel proper starts at 0x0208000
 *  o create (allocate) a pte to contain the machine vectors
 *  o populate the pte (points to 0x02078000) (FIXME - is it zeroed?)
 *  o populate the init tasks page directory (pgd) with the new pte
 *  o zero the rest of the init tasks pgdir (FIXME - what about vmalloc?!)
 */
void __init memtable_init(struct meminfo *mi)
{
	pte_t *pte;
	int i;

	page_nr = max_low_pfn;

	pte = alloc_bootmem_low_pages(PTRS_PER_PTE * sizeof(pte_t));
	pte[0] = mk_pte_phys(PAGE_OFFSET + SCREEN_SIZE, PAGE_READONLY);
	pmd_populate(&init_mm, pmd_offset(swapper_pg_dir, 0), pte);

	for (i = 1; i < PTRS_PER_PGD; i++)
		pgd_val(swapper_pg_dir[i]) = 0;
}

void __init iotable_init(struct map_desc *io_desc)
{
	/* nothing to do */
}

/*
 * We never have holes in the memmap
 */
void __init create_memmap_holes(struct meminfo *mi)
{
}

static void pte_cache_ctor(void *pte, kmem_cache_t *cache, unsigned long flags)
{
	memzero(pte, sizeof(pte_t) * PTRS_PER_PTE);
}

static void pgd_cache_ctor(void *pgd, kmem_cache_t *cache, unsigned long flags)
{
	memzero(pgd + MEMC_TABLE_SIZE, USER_PTRS_PER_PGD * sizeof(pgd_t));
}

void __init pgtable_cache_init(void)
{
	pte_cache = kmem_cache_create("pte-cache",
				sizeof(pte_t) * PTRS_PER_PTE,
				0, 0, pte_cache_ctor, NULL);
	if (!pte_cache)
		BUG();

	pgd_cache = kmem_cache_create("pgd-cache", MEMC_TABLE_SIZE +
				sizeof(pgd_t) * PTRS_PER_PGD,
				0, 0, pgd_cache_ctor, NULL);
	if (!pgd_cache)
		BUG();
}
