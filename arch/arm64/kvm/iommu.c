// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2022 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>

static unsigned long dev_to_id(struct device *dev)
{
	/* Use the struct device pointer as a unique identifier. */
	return (unsigned long)dev;
}

int pkvm_iommu_driver_init(enum pkvm_iommu_driver_id id, void *data, size_t size)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_driver_init, id, data, size);
}

int pkvm_iommu_register(struct device *dev, enum pkvm_iommu_driver_id drv_id,
			phys_addr_t pa, size_t size, struct device *parent)
{
	void *mem;
	int ret;

	/*
	 * Hypcall to register the device. It will return -ENOMEM if it needs
	 * more memory. In that case allocate a page and retry.
	 * We assume that hyp never allocates more than a page per hypcall.
	 */
	ret = kvm_call_hyp_nvhe(__pkvm_iommu_register, dev_to_id(dev),
				drv_id, pa, size, dev_to_id(parent), NULL, 0);
	if (ret == -ENOMEM) {
		mem = (void *)__get_free_page(GFP_KERNEL);
		if (!mem)
			return -ENOMEM;

		ret = kvm_call_hyp_nvhe(__pkvm_iommu_register, dev_to_id(dev),
					drv_id, pa, size, dev_to_id(parent),
					mem, PAGE_SIZE);
	}
	return ret;
}

int pkvm_iommu_suspend(struct device *dev)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_pm_notify, dev_to_id(dev),
				 PKVM_IOMMU_PM_SUSPEND);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_suspend);

int pkvm_iommu_resume(struct device *dev)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_pm_notify, dev_to_id(dev),
				 PKVM_IOMMU_PM_RESUME);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_resume);

int pkvm_iommu_finalize(void)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_finalize);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_finalize);
