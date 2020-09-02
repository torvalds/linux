// SPDX-License-Identifier: GPL-2.0

#define DISABLE_BRANCH_PROFILING

#include <linux/kasan.h>
#include <linux/memblock.h>
#include <linux/hugetlb.h>

static int __init
kasan_init_shadow_8M(unsigned long k_start, unsigned long k_end, void *block)
{
	pmd_t *pmd = pmd_off_k(k_start);
	unsigned long k_cur, k_next;

	for (k_cur = k_start; k_cur != k_end; k_cur = k_next, pmd += 2, block += SZ_8M) {
		pte_basic_t *new;

		k_next = pgd_addr_end(k_cur, k_end);
		k_next = pgd_addr_end(k_next, k_end);
		if ((void *)pmd_page_vaddr(*pmd) != kasan_early_shadow_pte)
			continue;

		new = memblock_alloc(sizeof(pte_basic_t), SZ_4K);
		if (!new)
			return -ENOMEM;

		*new = pte_val(pte_mkhuge(pfn_pte(PHYS_PFN(__pa(block)), PAGE_KERNEL)));

		hugepd_populate_kernel((hugepd_t *)pmd, (pte_t *)new, PAGE_SHIFT_8M);
		hugepd_populate_kernel((hugepd_t *)pmd + 1, (pte_t *)new, PAGE_SHIFT_8M);
	}
	return 0;
}

int __init kasan_init_region(void *start, size_t size)
{
	unsigned long k_start = (unsigned long)kasan_mem_to_shadow(start);
	unsigned long k_end = (unsigned long)kasan_mem_to_shadow(start + size);
	unsigned long k_cur;
	int ret;
	void *block;

	block = memblock_alloc(k_end - k_start, SZ_8M);
	if (!block)
		return -ENOMEM;

	if (IS_ALIGNED(k_start, SZ_8M)) {
		kasan_init_shadow_8M(k_start, ALIGN_DOWN(k_end, SZ_8M), block);
		k_cur = ALIGN_DOWN(k_end, SZ_8M);
		if (k_cur == k_end)
			goto finish;
	} else {
		k_cur = k_start;
	}

	ret = kasan_init_shadow_page_tables(k_start, k_end);
	if (ret)
		return ret;

	for (; k_cur < k_end; k_cur += PAGE_SIZE) {
		pmd_t *pmd = pmd_off_k(k_cur);
		void *va = block + k_cur - k_start;
		pte_t pte = pfn_pte(PHYS_PFN(__pa(va)), PAGE_KERNEL);

		if (k_cur < ALIGN_DOWN(k_end, SZ_512K))
			pte = pte_mkhuge(pte);

		__set_pte_at(&init_mm, k_cur, pte_offset_kernel(pmd, k_cur), pte, 0);
	}
finish:
	flush_tlb_kernel_range(k_start, k_end);
	return 0;
}
