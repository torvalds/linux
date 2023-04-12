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

int pkvm_iommu_driver_init(u64 drv, void *data, size_t size)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_driver_init, drv, data, size);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_driver_init);

int pkvm_iommu_register(struct device *dev, u64 drv, phys_addr_t pa,
			size_t size, struct device *parent, u8 flags)
{
	void *mem;
	int ret;

	/*
	 * Hypcall to register the device. It will return -ENOMEM if it needs
	 * more memory. In that case allocate a page and retry.
	 * We assume that hyp never allocates more than a page per hypcall.
	 */
	ret = kvm_call_hyp_nvhe(__pkvm_iommu_register, dev_to_id(dev),
				drv, pa, size, dev_to_id(parent), flags, NULL);
	if (ret == -ENOMEM) {
		mem = (void *)__get_free_page(GFP_KERNEL);
		if (!mem)
			return -ENOMEM;

		ret = kvm_call_hyp_nvhe(__pkvm_iommu_register, dev_to_id(dev),
					drv, pa, size, dev_to_id(parent), flags, mem);
	}
	return ret;
}
EXPORT_SYMBOL_GPL(pkvm_iommu_register);

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

int pkvm_iommu_finalize(int err)
{
	return kvm_call_hyp_nvhe(__pkvm_iommu_finalize, err);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_finalize);
