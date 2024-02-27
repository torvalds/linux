// SPDX-License-Identifier: GPL-2.0

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/printk.h>
#include <linux/memblock.h>
#include <linux/sched/task.h>
#include <asm/pgalloc.h>
#include <asm/code-patching.h>
#include <mm/mmu_decl.h>

static pgprot_t __init kasan_prot_ro(void)
{
	if (early_mmu_has_feature(MMU_FTR_HPTE_TABLE))
		return PAGE_READONLY;

	return PAGE_KERNEL_RO;
}

static void __init kasan_populate_pte(pte_t *ptep, pgprot_t prot)
{
	unsigned long va = (unsigned long)kasan_early_shadow_page;
	phys_addr_t pa = __pa(kasan_early_shadow_page);
	int i;

	for (i = 0; i < PTRS_PER_PTE; i++, ptep++)
		__set_pte_at(&init_mm, va, ptep, pfn_pte(PHYS_PFN(pa), prot), 1);
}

int __init kasan_init_shadow_page_tables(unsigned long k_start, unsigned long k_end)
{
	pmd_t *pmd;
	unsigned long k_cur, k_next;

	pmd = pmd_off_k(k_start);

	for (k_cur = k_start; k_cur != k_end; k_cur = k_next, pmd++) {
		pte_t *new;

		k_next = pgd_addr_end(k_cur, k_end);
		if ((void *)pmd_page_vaddr(*pmd) != kasan_early_shadow_pte)
			continue;

		new = memblock_alloc(PTE_FRAG_SIZE, PTE_FRAG_SIZE);

		if (!new)
			return -ENOMEM;
		kasan_populate_pte(new, PAGE_KERNEL);
		pmd_populate_kernel(&init_mm, pmd, new);
	}
	return 0;
}

int __init __weak kasan_init_region(void *start, size_t size)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow(start);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow(start + size);
	unsigned long k_cur;
	int ret;
	void *block;

	ret = kasan_init_shadow_page_tables(k_start, k_end);
	if (ret)
		return ret;

	k_start = k_start & PAGE_MASK;
	block = memblock_alloc(k_end - k_start, PAGE_SIZE);
	if (!block)
		return -ENOMEM;

	for (k_cur = k_start & PAGE_MASK; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_off_k(k_cur);
		void *va = block + k_cur - k_start;
		pte_t pte = pfn_pte(PHYS_PFN(__pa(va)), PAGE_KERNEL);

		__set_pte_at(&init_mm, k_cur, pte_offset_kernel(pmd, k_cur), pte, 0);
	}
	flush_tlb_kernel_range(k_start, k_end);
	return 0;
}

void __init
kasan_update_early_region(unsigned long k_start, unsigned long k_end, pte_t pte)
{
	unsigned long k_cur;

	for (k_cur = k_start; k_cur != k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_off_k(k_cur);
		pte_t *ptep = pte_offset_kernel(pmd, k_cur);

		if (pte_page(*ptep) != virt_to_page(lm_alias(kasan_early_shadow_page)))
			continue;

		__set_pte_at(&init_mm, k_cur, ptep, pte, 0);
	}

	flush_tlb_kernel_range(k_start, k_end);
}

static void __init kasan_remap_early_shadow_ro(void)
{
	pgprot_t prot = kasan_prot_ro();
	phys_addr_t pa = __pa(kasan_early_shadow_page);

	kasan_populate_pte(kasan_early_shadow_pte, prot);

	kasan_update_early_region(KASAN_SHADOW_START, KASAN_SHADOW_END,
				  pfn_pte(PHYS_PFN(pa), prot));
}

static void __init kasan_unmap_early_shadow_vmalloc(void)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow((void *)VMALLOC_START);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow((void *)VMALLOC_END);

	kasan_update_early_region(k_start, k_end, __pte(0));

#ifdef MODULES_VADDR
	k_start = (unsigned long)kasan_mem_to_shadow((void *)MODULES_VADDR);
	k_end = (unsigned long)kasan_mem_to_shadow((void *)MODULES_END);
	kasan_update_early_region(k_start, k_end, __pte(0));
#endif
}

void __init kasan_mmu_init(void)
{
	int ret;

	if (early_mmu_has_feature(MMU_FTR_HPTE_TABLE)) {
		ret = kasan_init_shadow_page_tables(KASAN_SHADOW_START, KASAN_SHADOW_END);

		if (ret)
			panic("kasan: kasan_init_shadow_page_tables() failed");
	}
}

void __init kasan_init(void)
{
	phys_addr_t base, end;
	u64 i;
	int ret;

	for_each_mem_range(i, &base, &end) {
		phys_addr_t top = min(end, total_lowmem);

		if (base >= top)
			continue;

		ret = kasan_init_region(__va(base), top - base);
		if (ret)
			panic("kasan: kasan_init_region() failed");
	}

	if (IS_ENABLED(CONFIG_KASAN_VMALLOC)) {
		ret = kasan_init_shadow_page_tables(KASAN_SHADOW_START, KASAN_SHADOW_END);

		if (ret)
			panic("kasan: kasan_init_shadow_page_tables() failed");
	}

	kasan_remap_early_shadow_ro();

	clear_page(kasan_early_shadow_page);

	/* At this point kasan is fully initialized. Enable error messages */
	init_task.kasan_depth = 0;
	pr_info("KASAN init done\n");
}

void __init kasan_late_init(void)
{
	if (IS_ENABLED(CONFIG_KASAN_VMALLOC))
		kasan_unmap_early_shadow_vmalloc();
}

void __init kasan_early_init(void)
{
	unsigned long addr = KASAN_SHADOW_START;
	unsigned long end = KASAN_SHADOW_END;
	unsigned long next;
	pmd_t *pmd = pmd_off_k(addr);

	BUILD_BUG_ON(KASAN_SHADOW_START & ~PGDIR_MASK);

	kasan_populate_pte(kasan_early_shadow_pte, PAGE_KERNEL);

	do {
		next = pgd_addr_end(addr, end);
		pmd_populate_kernel(&init_mm, pmd, kasan_early_shadow_pte);
	} while (pmd++, addr = next, addr != end);
}
