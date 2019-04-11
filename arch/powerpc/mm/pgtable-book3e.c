/*
 * Copyright 2005, Paul Mackerras, IBM Corporation.
 * Copyright 2009, Benjamin Herrenschmidt, IBM Corporation.
 * Copyright 2015-2016, Aneesh Kumar K.V, IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include <linux/sched.h>
#include <linux/memblock.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/dma.h>

#include "mmu_decl.h"

#ifdef CONFIG_SPARSEMEM_VMEMMAP
/*
 * On Book3E CPUs, the vmemmap is currently mapped in the top half of
 * the vmalloc space using normal page tables, though the size of
 * pages encoded in the PTEs can be different
 */
int __meminit vmemmap_create_mapping(unsigned long start,
				     unsigned long page_size,
				     unsigned long phys)
{
	/* Create a PTE encoding without page size */
	unsigned long i, flags = _PAGE_PRESENT | _PAGE_ACCESSED |
		_PAGE_KERNEL_RW;

	/* PTEs only contain page size encodings up to 32M */
	BUG_ON(mmu_psize_defs[mmu_vmemmap_psize].enc > 0xf);

	/* Encode the size in the PTE */
	flags |= mmu_psize_defs[mmu_vmemmap_psize].enc << 8;

	/* For each PTE for that area, map things. Note that we don't
	 * increment phys because all PTEs are of the large size and
	 * thus must have the low bits clear
	 */
	for (i = 0; i < page_size; i += PAGE_SIZE)
		BUG_ON(map_kernel_page(start + i, phys, __pgprot(flags)));

	return 0;
}

#ifdef CONFIG_MEMORY_HOTPLUG
void vmemmap_remove_mapping(unsigned long start,
			    unsigned long page_size)
{
}
#endif
#endif /* CONFIG_SPARSEMEM_VMEMMAP */

static __ref void *early_alloc_pgtable(unsigned long size)
{
	void *ptr;

	ptr = memblock_alloc_try_nid(size, size, MEMBLOCK_LOW_LIMIT,
				     __pa(MAX_DMA_ADDRESS), NUMA_NO_NODE);

	if (!ptr)
		panic("%s: Failed to allocate %lu bytes align=0x%lx max_addr=%lx\n",
		      __func__, size, size, __pa(MAX_DMA_ADDRESS));

	return ptr;
}

/*
 * map_kernel_page currently only called by __ioremap
 * map_kernel_page adds an entry to the ioremap page table
 * and adds an entry to the HPT, possibly bolting it
 */
int map_kernel_page(unsigned long ea, unsigned long pa, pgprot_t prot)
{
	pgd_t *pgdp;
	pud_t *pudp;
	pmd_t *pmdp;
	pte_t *ptep;

	BUILD_BUG_ON(TASK_SIZE_USER64 > PGTABLE_RANGE);
	if (slab_is_available()) {
		pgdp = pgd_offset_k(ea);
		pudp = pud_alloc(&init_mm, pgdp, ea);
		if (!pudp)
			return -ENOMEM;
		pmdp = pmd_alloc(&init_mm, pudp, ea);
		if (!pmdp)
			return -ENOMEM;
		ptep = pte_alloc_kernel(pmdp, ea);
		if (!ptep)
			return -ENOMEM;
	} else {
		pgdp = pgd_offset_k(ea);
#ifndef __PAGETABLE_PUD_FOLDED
		if (pgd_none(*pgdp)) {
			pudp = early_alloc_pgtable(PUD_TABLE_SIZE);
			BUG_ON(pudp == NULL);
			pgd_populate(&init_mm, pgdp, pudp);
		}
#endif /* !__PAGETABLE_PUD_FOLDED */
		pudp = pud_offset(pgdp, ea);
		if (pud_none(*pudp)) {
			pmdp = early_alloc_pgtable(PMD_TABLE_SIZE);
			BUG_ON(pmdp == NULL);
			pud_populate(&init_mm, pudp, pmdp);
		}
		pmdp = pmd_offset(pudp, ea);
		if (!pmd_present(*pmdp)) {
			ptep = early_alloc_pgtable(PAGE_SIZE);
			BUG_ON(ptep == NULL);
			pmd_populate_kernel(&init_mm, pmdp, ptep);
		}
		ptep = pte_offset_kernel(pmdp, ea);
	}
	set_pte_at(&init_mm, ea, ptep, pfn_pte(pa >> PAGE_SHIFT, prot));

	smp_wmb();
	return 0;
}
