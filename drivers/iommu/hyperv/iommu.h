/* SPDX-License-Identifier: GPL-2.0 */

/*
 * Hyper-V IOMMU driver.
 *
 * Copyright (C) 2024-2026, Microsoft, Inc.
 *
 */

#ifndef _HYPERV_IOMMU_H
#define _HYPERV_IOMMU_H

struct hv_iommu_dev {
	struct iommu_device iommu;
	struct ida domain_ids;

	/* Device configuration */
	u8  max_iova_width;
	u8  max_pasid_width;
	u64 cap;
	u64 pgsize_bitmap;

	u64 first_domain;
	u64 last_domain;
};

struct hv_iommu_domain {
	union {
		struct iommu_domain    domain;
		struct pt_iommu        pt_iommu;
		struct pt_iommu_x86_64 pt_iommu_x86_64;
	};
	struct hv_iommu_dev *hv_iommu;
	struct hv_input_device_domain device_domain;
	u64		pgsize_bitmap;
};

PT_IOMMU_CHECK_DOMAIN(struct hv_iommu_domain, pt_iommu, domain);
PT_IOMMU_CHECK_DOMAIN(struct hv_iommu_domain, pt_iommu_x86_64.iommu, domain);

#define to_hv_iommu_domain(d) \
	container_of(d, struct hv_iommu_domain, domain)

#endif /* _HYPERV_IOMMU_H */
