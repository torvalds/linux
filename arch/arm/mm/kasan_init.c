// SPDX-License-Identifier: GPL-2.0-only
/*
 * This file contains kasan initialization code for ARM.
 *
 * Copyright (c) 2018 Samsung Electronics Co., Ltd.
 * Author: Andrey Ryabinin <ryabinin.a.a@gmail.com>
 * Author: Linus Walleij <linus.walleij@linaro.org>
 */

#define pr_fmt(fmt) "kasan: " fmt
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <linux/memblock.h>
#include <linux/sched/task.h>
#include <linux/start_kernel.h>
#include <linux/pgtable.h>
#include <asm/cputype.h>
#include <asm/highmem.h>
#include <asm/mach/map.h>
#include <asm/memory.h>
#include <asm/page.h>
#include <asm/pgalloc.h>
#include <asm/procinfo.h>
#include <asm/proc-fns.h>

#include "mm.h"

static pgd_t tmp_pgd_table[PTRS_PER_PGD] __initdata __aligned(PGD_SIZE);

pmd_t tmp_pmd_table[PTRS_PER_PMD] __page_aligned_bss;

static __init void *kasan_alloc_block(size_t size)
{
	return memblock_alloc_try_nid(size, size, __pa(MAX_DMA_ADDRESS),
				      MEMBLOCK_ALLOC_NOLEAKTRACE, NUMA_NO_NODE);
}

static void __init kasan_pte_populate(pmd_t *pmdp, unsigned long addr,
				      unsigned long end, bool early)
{
	unsigned long next;
	pte_t *ptep = pte_offset_kernel(pmdp, addr);

	do {
		pte_t entry;
		void *p;

		next = addr + PAGE_SIZE;

		if (!early) {
			if (!pte_none(READ_ONCE(*ptep)))
				continue;

			p = kasan_alloc_block(PAGE_SIZE);
			if (!p) {
				panic("%s failed to allocate shadow page for address 0x%lx\n",
				      __func__, addr);
				return;
			}
			memset(p, KASAN_SHADOW_INIT, PAGE_SIZE);
			entry = pfn_pte(virt_to_pfn(p),
					__pgprot(pgprot_val(PAGE_KERNEL)));
		} else if (pte_none(READ_ONCE(*ptep))) {
			/*
			 * The early shadow memory is mapping all KASan
			 * operations to one and the same page in memory,
			 * "kasan_early_shadow_page" so that the instrumentation
			 * will work on a scratch area until we can set up the
			 * proper KASan shadow memory.
			 */
			entry = pfn_pte(virt_to_pfn(kasan_early_shadow_page),
					__pgprot(_L_PTE_DEFAULT | L_PTE_DIRTY | L_PTE_XN));
		} else {
			/*
			 * Early shadow mappings are PMD_SIZE aligned, so if the
			 * first entry is already set, they must all be set.
			 */
			return;
		}

		set_pte_at(&init_mm, addr, ptep, entry);
	} while (ptep++, addr = next, addr != end);
}

/*
 * The pmd (page middle directory) is only used on LPAE
 */
static void __init kasan_pmd_populate(pud_t *pudp, unsigned long addr,
				      unsigned long end, bool early)
{
	unsigned long next;
	pmd_t *pmdp = pmd_offset(pudp, addr);

	do {
		if (pmd_none(*pmdp)) {
			/*
			 * We attempt to allocate a shadow block for the PMDs
			 * used by the PTEs for this address if it isn't already
			 * allocated.
			 */
			void *p = early ? kasan_early_shadow_pte :
				kasan_alloc_block(PAGE_SIZE);

			if (!p) {
				panic("%s failed to allocate shadow block for address 0x%lx\n",
				      __func__, addr);
				return;
			}
			pmd_populate_kernel(&init_mm, pmdp, p);
			flush_pmd_entry(pmdp);
		}

		next = pmd_addr_end(addr, end);
		kasan_pte_populate(pmdp, addr, next, early);
	} while (pmdp++, addr = next, addr != end);
}

static void __init kasan_pgd_populate(unsigned long addr, unsigned long end,
				      bool early)
{
	unsigned long next;
	pgd_t *pgdp;
	p4d_t *p4dp;
	pud_t *pudp;

	pgdp = pgd_offset_k(addr);

	do {
		/*
		 * Allocate and populate the shadow block of p4d folded into
		 * pud folded into pmd if it doesn't already exist
		 */
		if (!early && pgd_none(*pgdp)) {
			void *p = kasan_alloc_block(PAGE_SIZE);

			if (!p) {
				panic("%s failed to allocate shadow block for address 0x%lx\n",
				      __func__, addr);
				return;
			}
			pgd_populate(&init_mm, pgdp, p);
		}

		next = pgd_addr_end(addr, end);
		/*
		 * We just immediately jump over the p4d and pud page
		 * directories since we believe ARM32 will never gain four
		 * nor five level page tables.
		 */
		p4dp = p4d_offset(pgdp, addr);
		pudp = pud_offset(p4dp, addr);

		kasan_pmd_populate(pudp, addr, next, early);
	} while (pgdp++, addr = next, addr != end);
}

extern struct proc_info_list *lookup_processor_type(unsigned int);

void __init kasan_early_init(void)
{
	struct proc_info_list *list;

	/*
	 * locate processor in the list of supported processor
	 * types.  The linker builds this table for us from the
	 * entries in arch/arm/mm/proc-*.S
	 */
	list = lookup_processor_type(read_cpuid_id());
	if (list) {
#ifdef MULTI_CPU
		processor = *list->proc;
#endif
	}

	BUILD_BUG_ON((KASAN_SHADOW_END - (1UL << 29)) != KASAN_SHADOW_OFFSET);
	/*
	 * We walk the page table and set all of the shadow memory to point
	 * to the scratch page.
	 */
	kasan_pgd_populate(KASAN_SHADOW_START, KASAN_SHADOW_END, true);
}

static void __init clear_pgds(unsigned long start,
			unsigned long end)
{
	for (; start && start < end; start += PMD_SIZE)
		pmd_clear(pmd_off_k(start));
}

static int __init create_mapping(void *start, void *end)
{
	void *shadow_start, *shadow_end;

	shadow_start = kasan_mem_to_shadow(start);
	shadow_end = kasan_mem_to_shadow(end);

	pr_info("Mapping kernel virtual memory block: %px-%px at shadow: %px-%px\n",
		start, end, shadow_start, shadow_end);

	kasan_pgd_populate((unsigned long)shadow_start & PAGE_MASK,
			   PAGE_ALIGN((unsigned long)shadow_end), false);
	return 0;
}

void __init kasan_init(void)
{
	phys_addr_t pa_start, pa_end;
	u64 i;

	/*
	 * We are going to perform proper setup of shadow memory.
	 *
	 * At first we should unmap early shadow (clear_pgds() call bellow).
	 * However, instrumented code can't execute without shadow memory.
	 *
	 * To keep the early shadow memory MMU tables around while setting up
	 * the proper shadow memory, we copy swapper_pg_dir (the initial page
	 * table) to tmp_pgd_table and use that to keep the early shadow memory
	 * mapped until the full shadow setup is finished. Then we swap back
	 * to the proper swapper_pg_dir.
	 */

	memcpy(tmp_pgd_table, swapper_pg_dir, sizeof(tmp_pgd_table));
#ifdef CONFIG_ARM_LPAE
	/* We need to be in the same PGD or this won't work */
	BUILD_BUG_ON(pgd_index(KASAN_SHADOW_START) !=
		     pgd_index(KASAN_SHADOW_END));
	memcpy(tmp_pmd_table,
	       (void*)pgd_page_vaddr(*pgd_offset_k(KASAN_SHADOW_START)),
	       sizeof(tmp_pmd_table));
	set_pgd(&tmp_pgd_table[pgd_index(KASAN_SHADOW_START)],
		__pgd(__pa(tmp_pmd_table) | PMD_TYPE_TABLE | L_PGD_SWAPPER));
#endif
	cpu_switch_mm(tmp_pgd_table, &init_mm);
	local_flush_tlb_all();

	clear_pgds(KASAN_SHADOW_START, KASAN_SHADOW_END);

	if (!IS_ENABLED(CONFIG_KASAN_VMALLOC))
		kasan_populate_early_shadow(kasan_mem_to_shadow((void *)VMALLOC_START),
					    kasan_mem_to_shadow((void *)VMALLOC_END));

	kasan_populate_early_shadow(kasan_mem_to_shadow((void *)VMALLOC_END),
				    kasan_mem_to_shadow((void *)-1UL) + 1);

	for_each_mem_range(i, &pa_start, &pa_end) {
		void *start = __va(pa_start);
		void *end = __va(pa_end);

		/* Do not attempt to shadow highmem */
		if (pa_start >= arm_lowmem_limit) {
			pr_info("Skip highmem block at %pa-%pa\n", &pa_start, &pa_end);
			continue;
		}
		if (pa_end > arm_lowmem_limit) {
			pr_info("Truncating shadow for memory block at %pa-%pa to lowmem region at %pa\n",
				&pa_start, &pa_end, &arm_lowmem_limit);
			end = __va(arm_lowmem_limit);
		}
		if (start >= end) {
			pr_info("Skipping invalid memory block %pa-%pa (virtual %p-%p)\n",
				&pa_start, &pa_end, start, end);
			continue;
		}

		create_mapping(start, end);
	}

	/*
	 * 1. The module global variables are in MODULES_VADDR ~ MODULES_END,
	 *    so we need to map this area if CONFIG_KASAN_VMALLOC=n. With
	 *    VMALLOC support KASAN will manage this region dynamically,
	 *    refer to kasan_populate_vmalloc() and ARM's implementation of
	 *    module_alloc().
	 * 2. PKMAP_BASE ~ PKMAP_BASE+PMD_SIZE's shadow and MODULES_VADDR
	 *    ~ MODULES_END's shadow is in the same PMD_SIZE, so we can't
	 *    use kasan_populate_zero_shadow.
	 */
	if (!IS_ENABLED(CONFIG_KASAN_VMALLOC) && IS_ENABLED(CONFIG_MODULES))
		create_mapping((void *)MODULES_VADDR, (void *)(MODULES_END));
	create_mapping((void *)PKMAP_BASE, (void *)(PKMAP_BASE + PMD_SIZE));

	/*
	 * KAsan may reuse the contents of kasan_early_shadow_pte directly, so
	 * we should make sure that it maps the zero page read-only.
	 */
	for (i = 0; i < PTRS_PER_PTE; i++)
		set_pte_at(&init_mm, KASAN_SHADOW_START + i*PAGE_SIZE,
			   &kasan_early_shadow_pte[i],
			   pfn_pte(virt_to_pfn(kasan_early_shadow_page),
				__pgprot(pgprot_val(PAGE_KERNEL)
					 | L_PTE_RDONLY)));

	cpu_switch_mm(swapper_pg_dir, &init_mm);
	local_flush_tlb_all();

	memset(kasan_early_shadow_page, 0, PAGE_SIZE);
	pr_info("Kernel address sanitizer initialized\n");
	init_task.kasan_depth = 0;
}
