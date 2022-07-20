// SPDX-License-Identifier: GPL-2.0
/*
 * KASAN for 64-bit Book3S powerpc
 *
 * Copyright 2019-2022, Daniel Axtens, IBM Corporation.
 */

/*
 * ppc64 turns on virtual memory late in boot, after calling into generic code
 * like the device-tree parser, so it uses this in conjunction with a hook in
 * outline mode to avoid invalid access early in boot.
 */

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/printk.h>
#include <linux/sched/task.h>
#include <linux/memblock.h>
#include <asm/pgalloc.h>

DEFINE_STATIC_KEY_FALSE(powerpc_kasan_enabled_key);

static void __init kasan_init_phys_region(void *start, void *end)
{
	unsigned long k_start, k_end, k_cur;
	void *va;

	if (start >= end)
		return;

	k_start = ALIGN_DOWN((unsigned long)kasan_mem_to_shadow(start), PAGE_SIZE);
	k_end = ALIGN((unsigned long)kasan_mem_to_shadow(end), PAGE_SIZE);

	va = memblock_alloc(k_end - k_start, PAGE_SIZE);
	for (k_cur = k_start; k_cur < k_end; k_cur += PAGE_SIZE, va += PAGE_SIZE)
		map_kernel_page(k_cur, __pa(va), PAGE_KERNEL);
}

void __init kasan_init(void)
{
	/*
	 * We want to do the following things:
	 *  1) Map real memory into the shadow for all physical memblocks
	 *     This takes us from c000... to c008...
	 *  2) Leave a hole over the shadow of vmalloc space. KASAN_VMALLOC
	 *     will manage this for us.
	 *     This takes us from c008... to c00a...
	 *  3) Map the 'early shadow'/zero page over iomap and vmemmap space.
	 *     This takes us up to where we start at c00e...
	 */

	void *k_start = kasan_mem_to_shadow((void *)RADIX_VMALLOC_END);
	void *k_end = kasan_mem_to_shadow((void *)RADIX_VMEMMAP_END);
	phys_addr_t start, end;
	u64 i;
	pte_t zero_pte = pfn_pte(virt_to_pfn(kasan_early_shadow_page), PAGE_KERNEL);

	if (!early_radix_enabled()) {
		pr_warn("KASAN not enabled as it requires radix!");
		return;
	}

	for_each_mem_range(i, &start, &end)
		kasan_init_phys_region((void *)start, (void *)end);

	for (i = 0; i < PTRS_PER_PTE; i++)
		__set_pte_at(&init_mm, (unsigned long)kasan_early_shadow_page,
			     &kasan_early_shadow_pte[i], zero_pte, 0);

	for (i = 0; i < PTRS_PER_PMD; i++)
		pmd_populate_kernel(&init_mm, &kasan_early_shadow_pmd[i],
				    kasan_early_shadow_pte);

	for (i = 0; i < PTRS_PER_PUD; i++)
		pud_populate(&init_mm, &kasan_early_shadow_pud[i],
			     kasan_early_shadow_pmd);

	/* map the early shadow over the iomap and vmemmap space */
	kasan_populate_early_shadow(k_start, k_end);

	/* mark early shadow region as RO and wipe it */
	zero_pte = pfn_pte(virt_to_pfn(kasan_early_shadow_page), PAGE_KERNEL_RO);
	for (i = 0; i < PTRS_PER_PTE; i++)
		__set_pte_at(&init_mm, (unsigned long)kasan_early_shadow_page,
			     &kasan_early_shadow_pte[i], zero_pte, 0);

	/*
	 * clear_page relies on some cache info that hasn't been set up yet.
	 * It ends up looping ~forever and blows up other data.
	 * Use memset instead.
	 */
	memset(kasan_early_shadow_page, 0, PAGE_SIZE);

	static_branch_inc(&powerpc_kasan_enabled_key);

	/* Enable error messages */
	init_task.kasan_depth = 0;
	pr_info("KASAN init done\n");
}

void __init kasan_late_init(void) { }
