// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <linux/pci.h>

#include "amdxdna_pci_drv.h"

/*
 * Bind the driver base on (vendor_id, device_id) pair and later use the
 * (device_id, rev_id) pair as a key to select the devices. The devices with
 * same device_id have very similar interface to host driver.
 */
static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x1502) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x17f0) },
	{0}
};

MODULE_DEVICE_TABLE(pci, pci_ids);

static const struct amdxdna_device_id amdxdna_ids[] = {
	{ 0x1502, 0x0,  &dev_npu1_info },
	{ 0x17f0, 0x0,  &dev_npu2_info },
	{ 0x17f0, 0x10, &dev_npu4_info },
	{ 0x17f0, 0x11, &dev_npu5_info },
	{0}
};

DEFINE_DRM_ACCEL_FOPS(amdxdna_fops);

const struct drm_driver amdxdna_drm_drv = {
	.driver_features = DRIVER_GEM | DRIVER_COMPUTE_ACCEL |
		DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.fops = &amdxdna_fops,
	.name = "amdxdna_accel_driver",
	.desc = "AMD XDNA DRM implementation",
};

static const struct amdxdna_dev_info *
amdxdna_get_dev_info(struct pci_dev *pdev)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(amdxdna_ids); i++) {
		if (pdev->device == amdxdna_ids[i].device &&
		    pdev->revision == amdxdna_ids[i].revision)
			return amdxdna_ids[i].dev_info;
	}
	return NULL;
}

static int amdxdna_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct amdxdna_dev *xdna;
	int ret;

	xdna = devm_drm_dev_alloc(&pdev->dev, &amdxdna_drm_drv, typeof(*xdna), ddev);
	if (IS_ERR(xdna))
		return PTR_ERR(xdna);

	xdna->dev_info = amdxdna_get_dev_info(pdev);
	if (!xdna->dev_info)
		return -ENODEV;

	drmm_mutex_init(&xdna->ddev, &xdna->dev_lock);
	pci_set_drvdata(pdev, xdna);

	mutex_lock(&xdna->dev_lock);
	ret = xdna->dev_info->ops->init(xdna);
	mutex_unlock(&xdna->dev_lock);
	if (ret) {
		XDNA_ERR(xdna, "Hardware init failed, ret %d", ret);
		return ret;
	}

	ret = amdxdna_sysfs_init(xdna);
	if (ret) {
		XDNA_ERR(xdna, "Create amdxdna attrs failed: %d", ret);
		goto failed_dev_fini;
	}

	ret = drm_dev_register(&xdna->ddev, 0);
	if (ret) {
		XDNA_ERR(xdna, "DRM register failed, ret %d", ret);
		goto failed_sysfs_fini;
	}

	return 0;

failed_sysfs_fini:
	amdxdna_sysfs_fini(xdna);
failed_dev_fini:
	mutex_lock(&xdna->dev_lock);
	xdna->dev_info->ops->fini(xdna);
	mutex_unlock(&xdna->dev_lock);
	return ret;
}

static void amdxdna_remove(struct pci_dev *pdev)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(pdev);

	drm_dev_unplug(&xdna->ddev);
	amdxdna_sysfs_fini(xdna);

	mutex_lock(&xdna->dev_lock);
	xdna->dev_info->ops->fini(xdna);
	mutex_unlock(&xdna->dev_lock);
}

static struct pci_driver amdxdna_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pci_ids,
	.probe = amdxdna_probe,
	.remove = amdxdna_remove,
};

module_pci_driver(amdxdna_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XRT Team <runtimeca39d@amd.com>");
MODULE_DESCRIPTION("amdxdna driver");
