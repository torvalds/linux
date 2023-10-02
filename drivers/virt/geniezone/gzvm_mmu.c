// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2023 MediaTek Inc.
 */

#include <linux/gzvm_drv.h>

/**
 * hva_to_pa_fast() - converts hva to pa in generic fast way
 * @hva: Host virtual address.
 *
 * Return: GZVM_PA_ERR_BAD for translation error
 */
u64 hva_to_pa_fast(u64 hva)
{
	struct page *page[1];
	u64 pfn;

	if (get_user_page_fast_only(hva, 0, page)) {
		pfn = page_to_phys(page[0]);
		put_page((struct page *)page);
		return pfn;
	}
	return GZVM_PA_ERR_BAD;
}

/**
 * hva_to_pa_slow() - converts hva to pa in a slow way
 * @hva: Host virtual address
 *
 * This function converts HVA to PA in a slow way because the target hva is not
 * yet allocated and mapped in the host stage1 page table, we cannot find it
 * directly from current page table.
 * Thus, we have to allocate it and this operation is much slower than directly
 * find via current page table.
 *
 * Context: This function may sleep
 * Return: PA or GZVM_PA_ERR_BAD for translation error
 */
u64 hva_to_pa_slow(u64 hva)
{
	struct page *page = NULL;
	u64 pfn = 0;
	int npages;

	npages = get_user_pages_unlocked(hva, 1, &page, 0);
	if (npages != 1)
		return GZVM_PA_ERR_BAD;

	if (page) {
		pfn = page_to_phys(page);
		put_page(page);
		return pfn;
	}

	return GZVM_PA_ERR_BAD;
}

static u64 __gzvm_gfn_to_pfn_memslot(struct gzvm_memslot *memslot, u64 gfn)
{
	u64 hva, pa;

	hva = gzvm_gfn_to_hva_memslot(memslot, gfn);

	pa = gzvm_hva_to_pa_arch(hva);
	if (pa != GZVM_PA_ERR_BAD)
		return PHYS_PFN(pa);

	pa = hva_to_pa_fast(hva);
	if (pa != GZVM_PA_ERR_BAD)
		return PHYS_PFN(pa);

	pa = hva_to_pa_slow(hva);
	if (pa != GZVM_PA_ERR_BAD)
		return PHYS_PFN(pa);

	return GZVM_PA_ERR_BAD;
}

/**
 * gzvm_gfn_to_pfn_memslot() - Translate gfn (guest ipa) to pfn (host pa),
 *			       result is in @pfn
 * @memslot: Pointer to struct gzvm_memslot.
 * @gfn: Guest frame number.
 * @pfn: Host page frame number.
 *
 * Return:
 * * 0			- Succeed
 * * -EFAULT		- Failed to convert
 */
int gzvm_gfn_to_pfn_memslot(struct gzvm_memslot *memslot, u64 gfn,
			    u64 *pfn)
{
	u64 __pfn;

	if (!memslot)
		return -EFAULT;

	__pfn = __gzvm_gfn_to_pfn_memslot(memslot, gfn);
	if (__pfn == GZVM_PA_ERR_BAD) {
		*pfn = 0;
		return -EFAULT;
	}

	*pfn = __pfn;

	return 0;
}
