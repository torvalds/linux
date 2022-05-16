// SPDX-License-Identifier: GPL-2.0
#include <linux/hugetlb.h>
#include <linux/err.h>

int pud_huge(pud_t pud)
{
	return pud_leaf(pud);
}

int pmd_huge(pmd_t pmd)
{
	return pmd_leaf(pmd);
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	if (size == HPAGE_SIZE)
		return true;
	else if (IS_ENABLED(CONFIG_64BIT) && size == PUD_SIZE)
		return true;
	else
		return false;
}

#ifdef CONFIG_CONTIG_ALLOC
static __init int gigantic_pages_init(void)
{
	/* With CONTIG_ALLOC, we can allocate gigantic pages at runtime */
	if (IS_ENABLED(CONFIG_64BIT))
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	return 0;
}
arch_initcall(gigantic_pages_init);
#endif
