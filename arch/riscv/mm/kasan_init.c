// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Andes Technology Corporation

#include <linux/pfn.h>
#include <linux/init_task.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/pgtable.h>
#include <asm/tlbflush.h>
#include <asm/fixmap.h>
#include <asm/pgalloc.h>

extern pgd_t early_pg_dir[PTRS_PER_PGD];
asmlinkage void __init kasan_early_init(void)
{
	uintptr_t i;
	pgd_t *pgd = early_pg_dir + pgd_index(KASAN_SHADOW_START);

	BUILD_BUG_ON(KASAN_SHADOW_OFFSET !=
		KASAN_SHADOW_END - (1UL << (64 - KASAN_SHADOW_SCALE_SHIFT)));

	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_early_shadow_pte + i,
			pfn_pte(virt_to_pfn(kasan_early_shadow_page), PAGE_KERNEL));

	for (i = 0; i < PTRS_PER_PMD; ++i)
		set_pmd(kasan_early_shadow_pmd + i,
			pfn_pmd(PFN_DOWN
				(__pa((uintptr_t) kasan_early_shadow_pte)),
				__pgprot(_PAGE_TABLE)));

	for (i = KASAN_SHADOW_START; i < KASAN_SHADOW_END;
	     i += PGDIR_SIZE, ++pgd)
		set_pgd(pgd,
			pfn_pgd(PFN_DOWN
				(__pa(((uintptr_t) kasan_early_shadow_pmd))),
				__pgprot(_PAGE_TABLE)));

	/* init for swapper_pg_dir */
	pgd = pgd_offset_k(KASAN_SHADOW_START);

	for (i = KASAN_SHADOW_START; i < KASAN_SHADOW_END;
	     i += PGDIR_SIZE, ++pgd)
		set_pgd(pgd,
			pfn_pgd(PFN_DOWN
				(__pa(((uintptr_t) kasan_early_shadow_pmd))),
				__pgprot(_PAGE_TABLE)));

	local_flush_tlb_all();
}

static void __init kasan_populate_pte(pmd_t *pmd, unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pte_t *ptep, *base_pte;

	if (pmd_none(*pmd))
		base_pte = memblock_alloc(PTRS_PER_PTE * sizeof(pte_t), PAGE_SIZE);
	else
		base_pte = (pte_t *)pmd_page_vaddr(*pmd);

	ptep = base_pte + pte_index(vaddr);

	do {
		if (pte_none(*ptep)) {
			phys_addr = memblock_phys_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pte(ptep, pfn_pte(PFN_DOWN(phys_addr), PAGE_KERNEL));
		}
	} while (ptep++, vaddr += PAGE_SIZE, vaddr != end);

	set_pmd(pmd, pfn_pmd(PFN_DOWN(__pa(base_pte)), PAGE_TABLE));
}

static void __init kasan_populate_pmd(pgd_t *pgd, unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pmd_t *pmdp, *base_pmd;
	unsigned long next;

	base_pmd = (pmd_t *)pgd_page_vaddr(*pgd);
	if (base_pmd == lm_alias(kasan_early_shadow_pmd))
		base_pmd = memblock_alloc(PTRS_PER_PMD * sizeof(pmd_t), PAGE_SIZE);

	pmdp = base_pmd + pmd_index(vaddr);

	do {
		next = pmd_addr_end(vaddr, end);

		if (pmd_none(*pmdp) && IS_ALIGNED(vaddr, PMD_SIZE) && (next - vaddr) >= PMD_SIZE) {
			phys_addr = memblock_phys_alloc(PMD_SIZE, PMD_SIZE);
			if (phys_addr) {
				set_pmd(pmdp, pfn_pmd(PFN_DOWN(phys_addr), PAGE_KERNEL));
				continue;
			}
		}

		kasan_populate_pte(pmdp, vaddr, next);
	} while (pmdp++, vaddr = next, vaddr != end);

	/*
	 * Wait for the whole PGD to be populated before setting the PGD in
	 * the page table, otherwise, if we did set the PGD before populating
	 * it entirely, memblock could allocate a page at a physical address
	 * where KASAN is not populated yet and then we'd get a page fault.
	 */
	set_pgd(pgd, pfn_pgd(PFN_DOWN(__pa(base_pmd)), PAGE_TABLE));
}

static void __init kasan_populate_pgd(unsigned long vaddr, unsigned long end)
{
	phys_addr_t phys_addr;
	pgd_t *pgdp = pgd_offset_k(vaddr);
	unsigned long next;

	do {
		next = pgd_addr_end(vaddr, end);

		/*
		 * pgdp can't be none since kasan_early_init initialized all KASAN
		 * shadow region with kasan_early_shadow_pmd: if this is stillthe case,
		 * that means we can try to allocate a hugepage as a replacement.
		 */
		if (pgd_page_vaddr(*pgdp) == (unsigned long)lm_alias(kasan_early_shadow_pmd) &&
		    IS_ALIGNED(vaddr, PGDIR_SIZE) && (next - vaddr) >= PGDIR_SIZE) {
			phys_addr = memblock_phys_alloc(PGDIR_SIZE, PGDIR_SIZE);
			if (phys_addr) {
				set_pgd(pgdp, pfn_pgd(PFN_DOWN(phys_addr), PAGE_KERNEL));
				continue;
			}
		}

		kasan_populate_pmd(pgdp, vaddr, next);
	} while (pgdp++, vaddr = next, vaddr != end);
}

static void __init kasan_populate(void *start, void *end)
{
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);

	kasan_populate_pgd(vaddr, vend);

	local_flush_tlb_all();
	memset(start, KASAN_SHADOW_INIT, end - start);
}

static void __init kasan_shallow_populate_pgd(unsigned long vaddr, unsigned long end)
{
	unsigned long next;
	void *p;
	pgd_t *pgd_k = pgd_offset_k(vaddr);

	do {
		next = pgd_addr_end(vaddr, end);
		if (pgd_page_vaddr(*pgd_k) == (unsigned long)lm_alias(kasan_early_shadow_pmd)) {
			p = memblock_alloc(PAGE_SIZE, PAGE_SIZE);
			set_pgd(pgd_k, pfn_pgd(PFN_DOWN(__pa(p)), PAGE_TABLE));
		}
	} while (pgd_k++, vaddr = next, vaddr != end);
}

static void __init kasan_shallow_populate(void *start, void *end)
{
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);

	kasan_shallow_populate_pgd(vaddr, vend);
	local_flush_tlb_all();
}

void __init kasan_init(void)
{
	phys_addr_t p_start, p_end;
	u64 i;

	if (IS_ENABLED(CONFIG_KASAN_VMALLOC))
		kasan_shallow_populate(
			(void *)kasan_mem_to_shadow((void *)VMALLOC_START),
			(void *)kasan_mem_to_shadow((void *)VMALLOC_END));

	/* Populate the linear mapping */
	for_each_mem_range(i, &p_start, &p_end) {
		void *start = (void *)__va(p_start);
		void *end = (void *)__va(p_end);

		if (start >= end)
			break;

		kasan_populate(kasan_mem_to_shadow(start), kasan_mem_to_shadow(end));
	}

	/* Populate kernel, BPF, modules mapping */
	kasan_populate(kasan_mem_to_shadow((const void *)MODULES_VADDR),
		       kasan_mem_to_shadow((const void *)MODULES_VADDR + SZ_2G));

	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_early_shadow_pte[i],
			mk_pte(virt_to_page(kasan_early_shadow_page),
			       __pgprot(_PAGE_PRESENT | _PAGE_READ |
					_PAGE_ACCESSED)));

	memset(kasan_early_shadow_page, KASAN_SHADOW_INIT, PAGE_SIZE);
	init_task.kasan_depth = 0;
}
