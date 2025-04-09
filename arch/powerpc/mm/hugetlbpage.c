/*
 * PPC Huge TLB Page Support for Kernel.
 *
 * Copyright (C) 2003 David Gibson, IBM Corporation.
 * Copyright (C) 2011 Becky Bruce, Freescale Semiconductor
 *
 * Based on the IA-32 version:
 * Copyright (C) 2002, Rohit Seth <rohit.seth@intel.com>
 */

#include <linux/mm.h>
#include <linux/io.h>
#include <linux/slab.h>
#include <linux/hugetlb.h>
#include <linux/export.h>
#include <linux/of_fdt.h>
#include <linux/memblock.h>
#include <linux/moduleparam.h>
#include <linux/swap.h>
#include <linux/swapops.h>
#include <linux/kmemleak.h>
#include <asm/pgalloc.h>
#include <asm/tlb.h>
#include <asm/setup.h>
#include <asm/hugetlb.h>
#include <asm/pte-walk.h>
#include <asm/firmware.h>

bool hugetlb_disabled = false;

#define PTE_T_ORDER	(__builtin_ffs(sizeof(pte_basic_t)) - \
			 __builtin_ffs(sizeof(void *)))

pte_t *huge_pte_offset(struct mm_struct *mm, unsigned long addr, unsigned long sz)
{
	/*
	 * Only called for hugetlbfs pages, hence can ignore THP and the
	 * irq disabled walk.
	 */
	return __find_linux_pte(mm->pgd, addr, NULL, NULL);
}

pte_t *huge_pte_alloc(struct mm_struct *mm, struct vm_area_struct *vma,
		      unsigned long addr, unsigned long sz)
{
	p4d_t *p4d;
	pud_t *pud;
	pmd_t *pmd;

	addr &= ~(sz - 1);

	p4d = p4d_offset(pgd_offset(mm, addr), addr);
	if (!mm_pud_folded(mm) && sz >= P4D_SIZE)
		return (pte_t *)p4d;

	pud = pud_alloc(mm, p4d, addr);
	if (!pud)
		return NULL;
	if (!mm_pmd_folded(mm) && sz >= PUD_SIZE)
		return (pte_t *)pud;

	pmd = pmd_alloc(mm, pud, addr);
	if (!pmd)
		return NULL;

	if (sz >= PMD_SIZE) {
		/* On 8xx, all hugepages are handled as contiguous PTEs */
		if (IS_ENABLED(CONFIG_PPC_8xx)) {
			int i;

			for (i = 0; i < sz / PMD_SIZE; i++) {
				if (!pte_alloc_huge(mm, pmd + i, addr))
					return NULL;
			}
		}
		return (pte_t *)pmd;
	}

	return pte_alloc_huge(mm, pmd, addr);
}

#ifdef CONFIG_PPC_BOOK3S_64
/*
 * Tracks gpages after the device tree is scanned and before the
 * huge_boot_pages list is ready on pseries.
 */
#define MAX_NUMBER_GPAGES	1024
__initdata static u64 gpage_freearray[MAX_NUMBER_GPAGES];
__initdata static unsigned nr_gpages;

/*
 * Build list of addresses of gigantic pages.  This function is used in early
 * boot before the buddy allocator is setup.
 */
void __init pseries_add_gpage(u64 addr, u64 page_size, unsigned long number_of_pages)
{
	if (!addr)
		return;
	while (number_of_pages > 0) {
		gpage_freearray[nr_gpages] = addr;
		nr_gpages++;
		number_of_pages--;
		addr += page_size;
	}
}

static int __init pseries_alloc_bootmem_huge_page(struct hstate *hstate)
{
	struct huge_bootmem_page *m;
	if (nr_gpages == 0)
		return 0;
	m = phys_to_virt(gpage_freearray[--nr_gpages]);
	gpage_freearray[nr_gpages] = 0;
	list_add(&m->list, &huge_boot_pages[0]);
	m->hstate = hstate;
	m->flags = 0;
	return 1;
}

bool __init hugetlb_node_alloc_supported(void)
{
	return false;
}
#endif


int __init alloc_bootmem_huge_page(struct hstate *h, int nid)
{

#ifdef CONFIG_PPC_BOOK3S_64
	if (firmware_has_feature(FW_FEATURE_LPAR) && !radix_enabled())
		return pseries_alloc_bootmem_huge_page(h);
#endif
	return __alloc_bootmem_huge_page(h, nid);
}

bool __init arch_hugetlb_valid_size(unsigned long size)
{
	int shift = __ffs(size);
	int mmu_psize;

	/* Check that it is a page size supported by the hardware and
	 * that it fits within pagetable and slice limits. */
	if (size <= PAGE_SIZE || !is_power_of_2(size))
		return false;

	mmu_psize = check_and_get_huge_psize(shift);
	if (mmu_psize < 0)
		return false;

	BUG_ON(mmu_psize_defs[mmu_psize].shift != shift);

	return true;
}

static int __init add_huge_page_size(unsigned long long size)
{
	int shift = __ffs(size);

	if (!arch_hugetlb_valid_size((unsigned long)size))
		return -EINVAL;

	hugetlb_add_hstate(shift - PAGE_SHIFT);
	return 0;
}

static int __init hugetlbpage_init(void)
{
	bool configured = false;
	int psize;

	if (hugetlb_disabled) {
		pr_info("HugeTLB support is disabled!\n");
		return 0;
	}

	if (IS_ENABLED(CONFIG_PPC_BOOK3S_64) && !radix_enabled() &&
	    !mmu_has_feature(MMU_FTR_16M_PAGE))
		return -ENODEV;

	for (psize = 0; psize < MMU_PAGE_COUNT; ++psize) {
		unsigned shift;

		if (!mmu_psize_defs[psize].shift)
			continue;

		shift = mmu_psize_to_shift(psize);

		if (add_huge_page_size(1ULL << shift) < 0)
			continue;

		configured = true;
	}

	if (!configured)
		pr_info("Failed to initialize. Disabling HugeTLB");

	return 0;
}

arch_initcall(hugetlbpage_init);

void __init gigantic_hugetlb_cma_reserve(void)
{
	unsigned long order = 0;

	if (radix_enabled())
		order = PUD_SHIFT - PAGE_SHIFT;
	else if (!firmware_has_feature(FW_FEATURE_LPAR) && mmu_psize_defs[MMU_PAGE_16G].shift)
		/*
		 * For pseries we do use ibm,expected#pages for reserving 16G pages.
		 */
		order = mmu_psize_to_shift(MMU_PAGE_16G) - PAGE_SHIFT;

	if (order)
		hugetlb_cma_reserve(order);
}
