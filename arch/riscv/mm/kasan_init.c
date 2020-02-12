// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2019 Andes Technology Corporation

#include <linux/pfn.h>
#include <linux/init_task.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <asm/tlbflush.h>
#include <asm/pgtable.h>
#include <asm/fixmap.h>

extern pgd_t early_pg_dir[PTRS_PER_PGD];
asmlinkage void __init kasan_early_init(void)
{
	uintptr_t i;
	pgd_t *pgd = early_pg_dir + pgd_index(KASAN_SHADOW_START);

	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_early_shadow_pte + i,
			mk_pte(virt_to_page(kasan_early_shadow_page),
			PAGE_KERNEL));

	for (i = 0; i < PTRS_PER_PMD; ++i)
		set_pmd(kasan_early_shadow_pmd + i,
		 pfn_pmd(PFN_DOWN(__pa((uintptr_t)kasan_early_shadow_pte)),
			__pgprot(_PAGE_TABLE)));

	for (i = KASAN_SHADOW_START; i < KASAN_SHADOW_END;
	     i += PGDIR_SIZE, ++pgd)
		set_pgd(pgd,
		 pfn_pgd(PFN_DOWN(__pa(((uintptr_t)kasan_early_shadow_pmd))),
			__pgprot(_PAGE_TABLE)));

	/* init for swapper_pg_dir */
	pgd = pgd_offset_k(KASAN_SHADOW_START);

	for (i = KASAN_SHADOW_START; i < KASAN_SHADOW_END;
	     i += PGDIR_SIZE, ++pgd)
		set_pgd(pgd,
		 pfn_pgd(PFN_DOWN(__pa(((uintptr_t)kasan_early_shadow_pmd))),
			__pgprot(_PAGE_TABLE)));

	flush_tlb_all();
}

static void __init populate(void *start, void *end)
{
	unsigned long i;
	unsigned long vaddr = (unsigned long)start & PAGE_MASK;
	unsigned long vend = PAGE_ALIGN((unsigned long)end);
	unsigned long n_pages = (vend - vaddr) / PAGE_SIZE;
	unsigned long n_pmds =
		(n_pages % PTRS_PER_PTE) ? n_pages / PTRS_PER_PTE + 1 :
						n_pages / PTRS_PER_PTE;
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = memblock_alloc(n_pmds * sizeof(pmd_t), PAGE_SIZE);
	pte_t *pte = memblock_alloc(n_pages * sizeof(pte_t), PAGE_SIZE);

	for (i = 0; i < n_pages; i++) {
		phys_addr_t phys = memblock_phys_alloc(PAGE_SIZE, PAGE_SIZE);

		set_pte(pte + i, pfn_pte(PHYS_PFN(phys), PAGE_KERNEL));
	}

	for (i = 0; i < n_pmds; ++pgd, i += PTRS_PER_PMD)
		set_pgd(pgd, pfn_pgd(PFN_DOWN(__pa(((uintptr_t)(pmd + i)))),
				__pgprot(_PAGE_TABLE)));

	for (i = 0; i < n_pages; ++pmd, i += PTRS_PER_PTE)
		set_pmd(pmd, pfn_pmd(PFN_DOWN(__pa((uintptr_t)(pte + i))),
				__pgprot(_PAGE_TABLE)));

	flush_tlb_all();
	memset(start, 0, end - start);
}

void __init kasan_init(void)
{
	struct memblock_region *reg;
	unsigned long i;

	kasan_populate_early_shadow((void *)KASAN_SHADOW_START,
			(void *)kasan_mem_to_shadow((void *)VMALLOC_END));

	for_each_memblock(memory, reg) {
		void *start = (void *)__va(reg->base);
		void *end = (void *)__va(reg->base + reg->size);

		if (start >= end)
			break;

		populate(kasan_mem_to_shadow(start),
			 kasan_mem_to_shadow(end));
	};

	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_early_shadow_pte[i],
			mk_pte(virt_to_page(kasan_early_shadow_page),
			__pgprot(_PAGE_PRESENT | _PAGE_READ | _PAGE_ACCESSED)));

	memset(kasan_early_shadow_page, 0, PAGE_SIZE);
	init_task.kasan_depth = 0;
}
