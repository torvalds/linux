// SPDX-License-Identifier: MIT
/*
 * Copyright(c) 2019-2025, Intel Corporation. All rights reserved.
 */

#include <linux/intel_dg_nvm_aux.h>
#include <linux/pci.h>

#include "xe_device.h"
#include "xe_device_types.h"
#include "xe_mmio.h"
#include "xe_nvm.h"
#include "regs/xe_gsc_regs.h"
#include "xe_sriov.h"

#define GEN12_GUNIT_NVM_BASE 0x00102040
#define GEN12_DEBUG_NVM_BASE 0x00101018

#define GEN12_CNTL_PROTECTED_NVM_REG 0x0010100C

#define GEN12_GUNIT_NVM_SIZE 0x80
#define GEN12_DEBUG_NVM_SIZE 0x4

#define NVM_NON_POSTED_ERASE_CHICKEN_BIT BIT(13)

#define HECI_FW_STATUS_2_NVM_ACCESS_MODE BIT(3)

static const struct intel_dg_nvm_region regions[INTEL_DG_NVM_REGIONS] = {
	[0] = { .name = "DESCRIPTOR", },
	[2] = { .name = "GSC", },
	[9] = { .name = "PADDING", },
	[11] = { .name = "OptionROM", },
	[12] = { .name = "DAM", },
};

static void xe_nvm_release_dev(struct device *dev)
{
	struct auxiliary_device *aux = container_of(dev, struct auxiliary_device, dev);
	struct intel_dg_nvm_dev *nvm = container_of(aux, struct intel_dg_nvm_dev, aux_dev);

	kfree(nvm);
}

static bool xe_nvm_non_posted_erase(struct xe_device *xe)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);

	if (xe->info.platform != XE_BATTLEMAGE)
		return false;
	return !(xe_mmio_read32(mmio, XE_REG(GEN12_CNTL_PROTECTED_NVM_REG)) &
		 NVM_NON_POSTED_ERASE_CHICKEN_BIT);
}

static bool xe_nvm_writable_override(struct xe_device *xe)
{
	struct xe_mmio *mmio = xe_root_tile_mmio(xe);
	bool writable_override;
	resource_size_t base;

	switch (xe->info.platform) {
	case XE_BATTLEMAGE:
		base = DG2_GSC_HECI2_BASE;
		break;
	case XE_PVC:
		base = PVC_GSC_HECI2_BASE;
		break;
	case XE_DG2:
		base = DG2_GSC_HECI2_BASE;
		break;
	case XE_DG1:
		base = DG1_GSC_HECI2_BASE;
		break;
	default:
		drm_err(&xe->drm, "Unknown platform\n");
		return true;
	}

	writable_override =
		!(xe_mmio_read32(mmio, HECI_FWSTS2(base)) &
		  HECI_FW_STATUS_2_NVM_ACCESS_MODE);
	if (writable_override)
		drm_info(&xe->drm, "NVM access overridden by jumper\n");
	return writable_override;
}

int xe_nvm_init(struct xe_device *xe)
{
	struct pci_dev *pdev = to_pci_dev(xe->drm.dev);
	struct auxiliary_device *aux_dev;
	struct intel_dg_nvm_dev *nvm;
	int ret;

	if (!xe->info.has_gsc_nvm)
		return 0;

	/* No access to internal NVM from VFs */
	if (IS_SRIOV_VF(xe))
		return 0;

	/* Nvm pointer should be NULL here */
	if (WARN_ON(xe->nvm))
		return -EFAULT;

	xe->nvm = kzalloc(sizeof(*nvm), GFP_KERNEL);
	if (!xe->nvm)
		return -ENOMEM;

	nvm = xe->nvm;

	nvm->writable_override = xe_nvm_writable_override(xe);
	nvm->non_posted_erase = xe_nvm_non_posted_erase(xe);
	nvm->bar.parent = &pdev->resource[0];
	nvm->bar.start = GEN12_GUNIT_NVM_BASE + pdev->resource[0].start;
	nvm->bar.end = nvm->bar.start + GEN12_GUNIT_NVM_SIZE - 1;
	nvm->bar.flags = IORESOURCE_MEM;
	nvm->bar.desc = IORES_DESC_NONE;
	nvm->regions = regions;

	nvm->bar2.parent = &pdev->resource[0];
	nvm->bar2.start = GEN12_DEBUG_NVM_BASE + pdev->resource[0].start;
	nvm->bar2.end = nvm->bar2.start + GEN12_DEBUG_NVM_SIZE - 1;
	nvm->bar2.flags = IORESOURCE_MEM;
	nvm->bar2.desc = IORES_DESC_NONE;

	aux_dev = &nvm->aux_dev;

	aux_dev->name = "nvm";
	aux_dev->id = (pci_domain_nr(pdev->bus) << 16) | pci_dev_id(pdev);
	aux_dev->dev.parent = &pdev->dev;
	aux_dev->dev.release = xe_nvm_release_dev;

	ret = auxiliary_device_init(aux_dev);
	if (ret) {
		drm_err(&xe->drm, "xe-nvm aux init failed %d\n", ret);
		goto err;
	}

	ret = auxiliary_device_add(aux_dev);
	if (ret) {
		drm_err(&xe->drm, "xe-nvm aux add failed %d\n", ret);
		auxiliary_device_uninit(aux_dev);
		goto err;
	}
	return 0;

err:
	kfree(nvm);
	xe->nvm = NULL;
	return ret;
}

void xe_nvm_fini(struct xe_device *xe)
{
	struct intel_dg_nvm_dev *nvm = xe->nvm;

	if (!xe->info.has_gsc_nvm)
		return;

	/* No access to internal NVM from VFs */
	if (IS_SRIOV_VF(xe))
		return;

	/* Nvm pointer should not be NULL here */
	if (WARN_ON(!nvm))
		return;

	auxiliary_device_delete(&nvm->aux_dev);
	auxiliary_device_uninit(&nvm->aux_dev);
	xe->nvm = NULL;
}
