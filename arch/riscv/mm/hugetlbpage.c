// SPDX-License-Identifier: GPL-2.0
#include <linux/hugetlb.h>
#include <linux/err.h>

int pud_huge(pud_t pud)
{
	return pud_present(pud) &&
		(pud_val(pud) & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

int pmd_huge(pmd_t pmd)
{
	return pmd_present(pmd) &&
		(pmd_val(pmd) & (_PAGE_READ | _PAGE_WRITE | _PAGE_EXEC));
}

static __init int setup_hugepagesz(char *opt)
{
	unsigned long ps = memparse(opt, &opt);

	if (ps == HPAGE_SIZE) {
		hugetlb_add_hstate(HPAGE_SHIFT - PAGE_SHIFT);
	} else if (IS_ENABLED(CONFIG_64BIT) && ps == PUD_SIZE) {
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	} else {
		hugetlb_bad_size();
		pr_err("hugepagesz: Unsupported page size %lu M\n", ps >> 20);
		return 0;
	}

	return 1;
}
__setup("hugepagesz=", setup_hugepagesz);

#ifdef CONFIG_CONTIG_ALLOC
static __init int gigantic_pages_init(void)
{
	/* With CONTIG_ALLOC, we can allocate gigantic pages at runtime */
	if (IS_ENABLED(CONFIG_64BIT) && !size_to_hstate(1UL << PUD_SHIFT))
		hugetlb_add_hstate(PUD_SHIFT - PAGE_SHIFT);
	return 0;
}
arch_initcall(gigantic_pages_init);
#endif
