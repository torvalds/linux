/*
 * Copyright © 2015 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * Authors: David Woodhouse <dwmw2@infradead.org>
 */

#include <linux/intel-iommu.h>

int intel_svm_alloc_pasid_tables(struct intel_iommu *iommu)
{
	struct page *pages;
	int order;

	order = ecap_pss(iommu->ecap) + 7 - PAGE_SHIFT;
	if (order < 0)
		order = 0;

	pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
	if (!pages) {
		pr_warn("IOMMU: %s: Failed to allocate PASID table\n",
			iommu->name);
		return -ENOMEM;
	}
	iommu->pasid_table = page_address(pages);
	pr_info("%s: Allocated order %d PASID table.\n", iommu->name, order);

	if (ecap_dis(iommu->ecap)) {
		pages = alloc_pages(GFP_KERNEL | __GFP_ZERO, order);
		if (pages)
			iommu->pasid_state_table = page_address(pages);
		else
			pr_warn("IOMMU: %s: Failed to allocate PASID state table\n",
				iommu->name);
	}

	return 0;
}

int intel_svm_free_pasid_tables(struct intel_iommu *iommu)
{
	int order;

	order = ecap_pss(iommu->ecap) + 7 - PAGE_SHIFT;
	if (order < 0)
		order = 0;

	if (iommu->pasid_table) {
		free_pages((unsigned long)iommu->pasid_table, order);
		iommu->pasid_table = NULL;
	}
	if (iommu->pasid_state_table) {
		free_pages((unsigned long)iommu->pasid_state_table, order);
		iommu->pasid_state_table = NULL;
	}
	return 0;
}
