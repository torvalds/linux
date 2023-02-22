// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 */

#include <linux/kvm_host.h>
#include <asm/kvm_s2mpu.h>
#include <asm/kvm_host.h>
#include <asm/kvm_asm.h>

/* For an nvhe symbol get the kernel linear address of it. */
#define ksym_ref_addr_nvhe(x)			kvm_ksym_ref(&kvm_nvhe_sym(x))

static int init_s2mpu_driver(u32 version)
{
	static DEFINE_MUTEX(lock);
	static bool init_done;

	struct mpt *mpt;
	unsigned int gb;
	unsigned long addr;
	u64 pfn;
	int ret = 0;
	const int smpt_order = smpt_order_from_version(version);

	mutex_lock(&lock);
	if (init_done)
		goto out;

	/* Allocate a page for driver data. Must fit MPT descriptor. */
	BUILD_BUG_ON(sizeof(*mpt) > PAGE_SIZE);
	addr = __get_free_page(GFP_KERNEL);
	if (!addr) {
		ret = -ENOMEM;
		goto out;
	}

	mpt = (struct mpt *)addr;

	/* Allocate SMPT buffers. */
	for_each_gb(gb) {
		addr = __get_free_pages(GFP_KERNEL, smpt_order);
		if (!addr) {
			ret = -ENOMEM;
			goto out_free;
		}
		mpt->fmpt[gb].smpt = (u32 *)addr;
	}
	mpt->version = version;

	/* Share MPT descriptor with hyp. */
	pfn = __pa(mpt) >> PAGE_SHIFT;
	ret = kvm_call_hyp_nvhe(__pkvm_host_share_hyp, pfn);
	if (ret)
		goto out_free;

	/* Hypercall to initialize EL2 driver. */
	ret = pkvm_iommu_driver_init(ksym_ref_addr_nvhe(pkvm_s2mpu_driver),
				     mpt, sizeof(*mpt));
	if (ret)
		goto out_unshare;

	init_done = true;

out_unshare:
	WARN_ON(kvm_call_hyp_nvhe(__pkvm_host_unshare_hyp, pfn));
out_free:
	/* TODO - will driver return the memory? */
	if (ret) {
		for_each_gb(gb)
			free_pages((unsigned long)mpt->fmpt[gb].smpt, smpt_order);
		free_page((unsigned long)mpt);
	}
out:
	mutex_unlock(&lock);
	return ret;
}
int pkvm_iommu_s2mpu_init(u32 version)
{
	if (!is_protected_kvm_enabled())
		return -ENODEV;

	return init_s2mpu_driver(version);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_s2mpu_init);

int pkvm_iommu_s2mpu_register(struct device *dev, phys_addr_t addr)
{
	if (!is_protected_kvm_enabled())
		return -ENODEV;

	return pkvm_iommu_register(dev, ksym_ref_addr_nvhe(pkvm_s2mpu_driver),
				   addr, S2MPU_MMIO_SIZE, NULL);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_s2mpu_register);

static int init_sysmmu_sync_driver(void)
{
	static DEFINE_MUTEX(lock);
	static bool init_done;

	int ret = 0;

	mutex_lock(&lock);
	if (!init_done) {
		ret = pkvm_iommu_driver_init(ksym_ref_addr_nvhe(pkvm_sysmmu_sync_driver),
					     NULL, 0);
		init_done = !ret;
	}
	mutex_unlock(&lock);
	return ret;
}

int pkvm_iommu_sysmmu_sync_register(struct device *dev, phys_addr_t addr,
				    struct device *parent)
{
	int ret;

	if (!is_protected_kvm_enabled())
		return -ENODEV;

	ret = init_sysmmu_sync_driver();
	if (ret)
		return ret;

	return pkvm_iommu_register(dev, ksym_ref_addr_nvhe(pkvm_sysmmu_sync_driver),
				   addr + SYSMMU_SYNC_S2_OFFSET,
				   SYSMMU_SYNC_S2_MMIO_SIZE, parent);
}
EXPORT_SYMBOL_GPL(pkvm_iommu_sysmmu_sync_register);
