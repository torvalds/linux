// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <linux/iommu.h>
#include <linux/pci.h>

#include "amdxdna_ctx.h"
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

static int amdxdna_drm_open(struct drm_device *ddev, struct drm_file *filp)
{
	struct amdxdna_dev *xdna = to_xdna_dev(ddev);
	struct amdxdna_client *client;
	int ret;

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client)
		return -ENOMEM;

	client->pid = pid_nr(filp->pid);
	client->xdna = xdna;

	client->sva = iommu_sva_bind_device(xdna->ddev.dev, current->mm);
	if (IS_ERR(client->sva)) {
		ret = PTR_ERR(client->sva);
		XDNA_ERR(xdna, "SVA bind device failed, ret %d", ret);
		goto failed;
	}
	client->pasid = iommu_sva_get_pasid(client->sva);
	if (client->pasid == IOMMU_PASID_INVALID) {
		XDNA_ERR(xdna, "SVA get pasid failed");
		ret = -ENODEV;
		goto unbind_sva;
	}
	mutex_init(&client->hwctx_lock);
	idr_init_base(&client->hwctx_idr, AMDXDNA_INVALID_CTX_HANDLE + 1);

	mutex_lock(&xdna->dev_lock);
	list_add_tail(&client->node, &xdna->client_list);
	mutex_unlock(&xdna->dev_lock);

	filp->driver_priv = client;
	client->filp = filp;

	XDNA_DBG(xdna, "pid %d opened", client->pid);
	return 0;

unbind_sva:
	iommu_sva_unbind_device(client->sva);
failed:
	kfree(client);

	return ret;
}

static void amdxdna_drm_close(struct drm_device *ddev, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(ddev);

	XDNA_DBG(xdna, "closing pid %d", client->pid);

	idr_destroy(&client->hwctx_idr);
	mutex_destroy(&client->hwctx_lock);

	iommu_sva_unbind_device(client->sva);

	XDNA_DBG(xdna, "pid %d closed", client->pid);
	kfree(client);
}

static int amdxdna_flush(struct file *f, fl_owner_t id)
{
	struct drm_file *filp = f->private_data;
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = client->xdna;
	int idx;

	XDNA_DBG(xdna, "PID %d flushing...", client->pid);
	if (!drm_dev_enter(&xdna->ddev, &idx))
		return 0;

	mutex_lock(&xdna->dev_lock);
	list_del_init(&client->node);
	mutex_unlock(&xdna->dev_lock);
	amdxdna_hwctx_remove_all(client);

	drm_dev_exit(idx);
	return 0;
}

static const struct drm_ioctl_desc amdxdna_drm_ioctls[] = {
	/* Context */
	DRM_IOCTL_DEF_DRV(AMDXDNA_CREATE_HWCTX, amdxdna_drm_create_hwctx_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_DESTROY_HWCTX, amdxdna_drm_destroy_hwctx_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_CONFIG_HWCTX, amdxdna_drm_config_hwctx_ioctl, 0),
};

static const struct file_operations amdxdna_fops = {
	.owner		= THIS_MODULE,
	.open		= accel_open,
	.release	= drm_release,
	.flush		= amdxdna_flush,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= noop_llseek,
	.mmap		= drm_gem_mmap,
	.fop_flags	= FOP_UNSIGNED_OFFSET,
};

const struct drm_driver amdxdna_drm_drv = {
	.driver_features = DRIVER_GEM | DRIVER_COMPUTE_ACCEL |
		DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.fops = &amdxdna_fops,
	.name = "amdxdna_accel_driver",
	.desc = "AMD XDNA DRM implementation",
	.open = amdxdna_drm_open,
	.postclose = amdxdna_drm_close,
	.ioctls = amdxdna_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(amdxdna_drm_ioctls),
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
	INIT_LIST_HEAD(&xdna->client_list);
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
	struct amdxdna_client *client;

	drm_dev_unplug(&xdna->ddev);
	amdxdna_sysfs_fini(xdna);

	mutex_lock(&xdna->dev_lock);
	client = list_first_entry_or_null(&xdna->client_list,
					  struct amdxdna_client, node);
	while (client) {
		list_del_init(&client->node);
		mutex_unlock(&xdna->dev_lock);

		amdxdna_hwctx_remove_all(client);

		mutex_lock(&xdna->dev_lock);
		client = list_first_entry_or_null(&xdna->client_list,
						  struct amdxdna_client, node);
	}

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
