/*
 * This file contains kasan initialization code for ARM64.
 *
 * Copyright (c) 2015 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#define pr_fmt(fmt) "kasan: " fmt
#include <linux/bootmem.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/sched/task.h>
#include <linux/memblock.h>
#include <linux/start_kernel.h>
#include <linux/mm.h>

#include <asm/mmu_context.h>
#include <asm/kernel-pgtable.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/sections.h>
#include <asm/tlbflush.h>

static pgd_t tmp_pg_dir[PTRS_PER_PGD] __initdata __aligned(PGD_SIZE);

/*
 * The p*d_populate functions call virt_to_phys implicitly so they can't be used
 * directly on kernel symbols (bm_p*d). All the early functions are called too
 * early to use lm_alias so __p*d_populate functions must be used to populate
 * with the physical address from __pa_symbol.
 */

static phys_addr_t __init kasan_alloc_zeroed_page(int node)
{
	void *p = memblock_virt_alloc_try_nid(PAGE_SIZE, PAGE_SIZE,
					      __pa(MAX_DMA_ADDRESS),
					      MEMBLOCK_ALLOC_KASAN, node);
	return __pa(p);
}

static pte_t *__init kasan_pte_offset(pmd_t *pmdp, unsigned long addr, int node,
				      bool early)
{
	if (pmd_none(READ_ONCE(*pmdp))) {
		phys_addr_t pte_phys = early ?
				__pa_symbol(kasan_early_shadow_pte)
					: kasan_alloc_zeroed_page(node);
		__pmd_populate(pmdp, pte_phys, PMD_TYPE_TABLE);
	}

	return early ? pte_offset_kimg(pmdp, addr)
		     : pte_offset_kernel(pmdp, addr);
}

static pmd_t *__init kasan_pmd_offset(pud_t *pudp, unsigned long addr, int node,
				      bool early)
{
	if (pud_none(READ_ONCE(*pudp))) {
		phys_addr_t pmd_phys = early ?
				__pa_symbol(kasan_early_shadow_pmd)
					: kasan_alloc_zeroed_page(node);
		__pud_populate(pudp, pmd_phys, PMD_TYPE_TABLE);
	}

	return early ? pmd_offset_kimg(pudp, addr) : pmd_offset(pudp, addr);
}

static pud_t *__init kasan_pud_offset(pgd_t *pgdp, unsigned long addr, int node,
				      bool early)
{
	if (pgd_none(READ_ONCE(*pgdp))) {
		phys_addr_t pud_phys = early ?
				__pa_symbol(kasan_early_shadow_pud)
					: kasan_alloc_zeroed_page(node);
		__pgd_populate(pgdp, pud_phys, PMD_TYPE_TABLE);
	}

	return early ? pud_offset_kimg(pgdp, addr) : pud_offset(pgdp, addr);
}

static void __init kasan_pte_populate(pmd_t *pmdp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pte_t *ptep = kasan_pte_offset(pmdp, addr, node, early);

	do {
		phys_addr_t page_phys = early ?
				__pa_symbol(kasan_early_shadow_page)
					: kasan_alloc_zeroed_page(node);
		if (!early)
			memset(__va(page_phys), KASAN_SHADOW_INIT, PAGE_SIZE);
		next = addr + PAGE_SIZE;
		set_pte(ptep, pfn_pte(__phys_to_pfn(page_phys), PAGE_KERNEL));
	} while (ptep++, addr = next, addr != end && pte_none(READ_ONCE(*ptep)));
}

static void __init kasan_pmd_populate(pud_t *pudp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pmd_t *pmdp = kasan_pmd_offset(pudp, addr, node, early);

	do {
		next = pmd_addr_end(addr, end);
		kasan_pte_populate(pmdp, addr, next, node, early);
	} while (pmdp++, addr = next, addr != end && pmd_none(READ_ONCE(*pmdp)));
}

static void __init kasan_pud_populate(pgd_t *pgdp, unsigned long addr,
				      unsigned long end, int node, bool early)
{
	unsigned long next;
	pud_t *pudp = kasan_pud_offset(pgdp, addr, node, early);

	do {
		next = pud_addr_end(addr, end);
		kasan_pmd_populate(pudp, addr, next, node, early);
	} while (pudp++, addr = next, addr != end && pud_none(READ_ONCE(*pudp)));
}

static void __init kasan_pgd_populate(unsigned long addr, unsigned long end,
				      int node, bool early)
{
	unsigned long next;
	pgd_t *pgdp;

	pgdp = pgd_offset_k(addr);
	do {
		next = pgd_addr_end(addr, end);
		kasan_pud_populate(pgdp, addr, next, node, early);
	} while (pgdp++, addr = next, addr != end);
}

/* The early shadow maps everything to a single page of zeroes */
asmlinkage void __init kasan_early_init(void)
{
	BUILD_BUG_ON(KASAN_SHADOW_OFFSET !=
		KASAN_SHADOW_END - (1UL << (64 - KASAN_SHADOW_SCALE_SHIFT)));
	BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_START, PGDIR_SIZE));
	BUILD_BUG_ON(!IS_ALIGNED(KASAN_SHADOW_END, PGDIR_SIZE));
	kasan_pgd_populate(KASAN_SHADOW_START, KASAN_SHADOW_END, NUMA_NO_NODE,
			   true);
}

/* Set up full kasan mappings, ensuring that the mapped pages are zeroed */
static void __init kasan_map_populate(unsigned long start, unsigned long end,
				      int node)
{
	kasan_pgd_populate(start & PAGE_MASK, PAGE_ALIGN(end), node, false);
}

/*
 * Copy the current shadow region into a new pgdir.
 */
void __init kasan_copy_shadow(pgd_t *pgdir)
{
	pgd_t *pgdp, *pgdp_new, *pgdp_end;

	pgdp = pgd_offset_k(KASAN_SHADOW_START);
	pgdp_end = pgd_offset_k(KASAN_SHADOW_END);
	pgdp_new = pgd_offset_raw(pgdir, KASAN_SHADOW_START);
	do {
		set_pgd(pgdp_new, READ_ONCE(*pgdp));
	} while (pgdp++, pgdp_new++, pgdp != pgdp_end);
}

static void __init clear_pgds(unsigned long start,
			unsigned long end)
{
	/*
	 * Remove references to kasan page tables from
	 * swapper_pg_dir. pgd_clear() can't be used
	 * here because it's nop on 2,3-level pagetable setups
	 */
	for (; start < end; start += PGDIR_SIZE)
		set_pgd(pgd_offset_k(start), __pgd(0));
}

void __init kasan_init(void)
{
	u64 kimg_shadow_start, kimg_shadow_end;
	u64 mod_shadow_start, mod_shadow_end;
	struct memblock_region *reg;
	int i;

	kimg_shadow_start = (u64)kasan_mem_to_shadow(_text) & PAGE_MASK;
	kimg_shadow_end = PAGE_ALIGN((u64)kasan_mem_to_shadow(_end));

	mod_shadow_start = (u64)kasan_mem_to_shadow((void *)MODULES_VADDR);
	mod_shadow_end = (u64)kasan_mem_to_shadow((void *)MODULES_END);

	/*
	 * We are going to perform proper setup of shadow memory.
	 * At first we should unmap early shadow (clear_pgds() call below).
	 * However, instrumented code couldn't execute without shadow memory.
	 * tmp_pg_dir used to keep early shadow mapped until full shadow
	 * setup will be finished.
	 */
	memcpy(tmp_pg_dir, swapper_pg_dir, sizeof(tmp_pg_dir));
	dsb(ishst);
	cpu_replace_ttbr1(lm_alias(tmp_pg_dir));

	clear_pgds(KASAN_SHADOW_START, KASAN_SHADOW_END);

	kasan_map_populate(kimg_shadow_start, kimg_shadow_end,
			   early_pfn_to_nid(virt_to_pfn(lm_alias(_text))));

	kasan_populate_early_shadow((void *)KASAN_SHADOW_START,
				    (void *)mod_shadow_start);
	kasan_populate_early_shadow((void *)kimg_shadow_end,
				    kasan_mem_to_shadow((void *)PAGE_OFFSET));

	if (kimg_shadow_start > mod_shadow_end)
		kasan_populate_early_shadow((void *)mod_shadow_end,
					    (void *)kimg_shadow_start);

	for_each_memblock(memory, reg) {
		void *start = (void *)__phys_to_virt(reg->base);
		void *end = (void *)__phys_to_virt(reg->base + reg->size);

		if (start >= end)
			break;

		kasan_map_populate((unsigned long)kasan_mem_to_shadow(start),
				   (unsigned long)kasan_mem_to_shadow(end),
				   early_pfn_to_nid(virt_to_pfn(start)));
	}

	/*
	 * KAsan may reuse the contents of kasan_early_shadow_pte directly,
	 * so we should make sure that it maps the zero page read-only.
	 */
	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte(&kasan_early_shadow_pte[i],
			pfn_pte(sym_to_pfn(kasan_early_shadow_page),
				PAGE_KERNEL_RO));

	memset(kasan_early_shadow_page, KASAN_SHADOW_INIT, PAGE_SIZE);
	cpu_replace_ttbr1(lm_alias(swapper_pg_dir));

	/* At this point kasan is fully initialized. Enable error messages */
	init_task.kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");
}
