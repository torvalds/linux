/*
 * Xtensa KASAN shadow map initialization
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2017 Cadence Design Systems Inc.
 */

#include <linux/memblock.h>
#include <linux/init_task.h>
#include <linux/kasan.h>
#include <linux/kernel.h>
#include <asm/initialize_mmu.h>
#include <asm/tlbflush.h>
#include <asm/traps.h>

void __init kasan_early_init(void)
{
	unsigned long vaddr = KASAN_SHADOW_START;
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = pmd_offset(pgd, vaddr);
	int i;

	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_early_shadow_pte + i,
			mk_pte(virt_to_page(kasan_early_shadow_page),
				PAGE_KERNEL));

	for (vaddr = 0; vaddr < KASAN_SHADOW_SIZE; vaddr += PMD_SIZE, ++pmd) {
		BUG_ON(!pmd_none(*pmd));
		set_pmd(pmd, __pmd((unsigned long)kasan_early_shadow_pte));
	}
	early_trap_init();
}

static void __init populate(void *start, void *end)
{
	unsigned long n_pages = (end - start) / PAGE_SIZE;
	unsigned long n_pmds = n_pages / PTRS_PER_PTE;
	unsigned long i, j;
	unsigned long vaddr = (unsigned long)start;
	pgd_t *pgd = pgd_offset_k(vaddr);
	pmd_t *pmd = pmd_offset(pgd, vaddr);
	pte_t *pte = memblock_alloc(n_pages * sizeof(pte_t), PAGE_SIZE);

	pr_debug("%s: %p - %p\n", __func__, start, end);

	for (i = j = 0; i < n_pmds; ++i) {
		int k;

		for (k = 0; k < PTRS_PER_PTE; ++k, ++j) {
			phys_addr_t phys =
				memblock_alloc_base(PAGE_SIZE, PAGE_SIZE,
						    MEMBLOCK_ALLOC_ANYWHERE);

			set_pte(pte + j, pfn_pte(PHYS_PFN(phys), PAGE_KERNEL));
		}
	}

	for (i = 0; i < n_pmds ; ++i, pte += PTRS_PER_PTE)
		set_pmd(pmd + i, __pmd((unsigned long)pte));

	local_flush_tlb_all();
	memset(start, 0, end - start);
}

void __init kasan_init(void)
{
	int i;

	BUILD_BUG_ON(KASAN_SHADOW_OFFSET != KASAN_SHADOW_START -
		     (KASAN_START_VADDR >> KASAN_SHADOW_SCALE_SHIFT));
	BUILD_BUG_ON(VMALLOC_START < KASAN_START_VADDR);

	/*
	 * Replace shadow map pages that cover addresses from VMALLOC area
	 * start to the end of KSEG with clean writable pages.
	 */
	populate(kasan_mem_to_shadow((void *)VMALLOC_START),
		 kasan_mem_to_shadow((void *)XCHAL_KSEG_BYPASS_VADDR));

	/*
	 * Write protect kasan_early_shadow_page and zero-initialize it again.
	 */
	for (i = 0; i < PTRS_PER_PTE; ++i)
		set_pte(kasan_early_shadow_pte + i,
			mk_pte(virt_to_page(kasan_early_shadow_page),
				PAGE_KERNEL_RO));

	local_flush_tlb_all();
	memset(kasan_early_shadow_page, 0, PAGE_SIZE);

	/* At this point kasan is fully initialized. Enable error messages. */
	current->kasan_depth = 0;
	pr_info("KernelAddressSanitizer initialized\n");
}
