// SPDX-License-Identifier: GPL-2.0

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/memblock.h>
#include <mm/mmu_decl.h>

int __init kasan_init_region(void *start, size_t size)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow(start);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow(start + size);
	unsigned long k_nobat = k_start;
	unsigned long k_cur;
	phys_addr_t phys;
	int ret;

	while (k_nobat < k_end) {
		unsigned int k_size = bat_block_size(k_nobat, k_end);
		int idx = find_free_bat();

		if (idx == -1)
			break;
		if (k_size < SZ_128K)
			break;
		phys = memblock_phys_alloc_range(k_size, k_size, 0,
						 MEMBLOCK_ALLOC_ANYWHERE);
		if (!phys)
			break;

		setbat(idx, k_nobat, phys, k_size, PAGE_KERNEL);
		k_nobat += k_size;
	}
	if (k_nobat != k_start)
		update_bats();

	if (k_nobat < k_end) {
		phys = memblock_phys_alloc_range(k_end - k_nobat, PAGE_SIZE, 0,
						 MEMBLOCK_ALLOC_ANYWHERE);
		if (!phys)
			return -ENOMEM;
	}

	ret = kasan_init_shadow_page_tables(k_start, k_end);
	if (ret)
		return ret;

	kasan_update_early_region(k_start, k_nobat, __pte(0));

	for (k_cur = k_nobat; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_off_k(k_cur);
		pte_t pte = pfn_pte(PHYS_PFN(phys + k_cur - k_nobat), PAGE_KERNEL);

		__set_pte_at(&init_mm, k_cur, pte_offset_kernel(pmd, k_cur), pte, 0);
	}
	flush_tlb_kernel_range(k_start, k_end);
	memset(kasan_mem_to_shadow(start), 0, k_end - k_start);

	return 0;
}
