// SPDX-License-Identifier: GPL-2.0

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/memblock.h>
#include <asm/pgalloc.h>
#include <mm/mmu_decl.h>

int __init kasan_init_region(void *start, size_t size)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow(start);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow(start + size);
	unsigned long k_cur = k_start;
	int k_size = k_end - k_start;
	int k_size_base = 1 << (ffs(k_size) - 1);
	int ret;
	void *block;

	block = memblock_alloc(k_size, k_size_base);

	if (block && k_size_base >= SZ_128K && k_start == ALIGN(k_start, k_size_base)) {
		int k_size_more = 1 << (ffs(k_size - k_size_base) - 1);

		setbat(-1, k_start, __pa(block), k_size_base, PAGE_KERNEL);
		if (k_size_more >= SZ_128K)
			setbat(-1, k_start + k_size_base, __pa(block) + k_size_base,
			       k_size_more, PAGE_KERNEL);
		if (v_block_mapped(k_start))
			k_cur = k_start + k_size_base;
		if (v_block_mapped(k_start + k_size_base))
			k_cur = k_start + k_size_base + k_size_more;

		update_bats();
	}

	if (!block)
		block = memblock_alloc(k_size, PAGE_SIZE);
	if (!block)
		return -ENOMEM;

	ret = kasan_init_shadow_page_tables(k_start, k_end);
	if (ret)
		return ret;

	kasan_update_early_region(k_start, k_cur, __pte(0));

	for (; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_off_k(k_cur);
		void *va = block + k_cur - k_start;
		pte_t pte = pfn_pte(PHYS_PFN(__pa(va)), PAGE_KERNEL);

		__set_pte_at(&init_mm, k_cur, pte_offset_kernel(pmd, k_cur), pte, 0);
	}
	flush_tlb_kernel_range(k_start, k_end);
	return 0;
}
