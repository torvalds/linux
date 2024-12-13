// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_accel.h>
#include <drm/drm_drv.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_managed.h>
#include <drm/gpu_scheduler.h>
#include <linux/iommu.h>
#include <linux/pci.h>
#include <linux/pm_runtime.h>

#include "amdxdna_ctx.h"
#include "amdxdna_gem.h"
#include "amdxdna_pci_drv.h"

#define AMDXDNA_AUTOSUSPEND_DELAY	5000 /* milliseconds */

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

	ret = pm_runtime_resume_and_get(ddev->dev);
	if (ret) {
		XDNA_ERR(xdna, "Failed to get rpm, ret %d", ret);
		return ret;
	}

	client = kzalloc(sizeof(*client), GFP_KERNEL);
	if (!client) {
		ret = -ENOMEM;
		goto put_rpm;
	}

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
	init_srcu_struct(&client->hwctx_srcu);
	idr_init_base(&client->hwctx_idr, AMDXDNA_INVALID_CTX_HANDLE + 1);
	mutex_init(&client->mm_lock);

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
put_rpm:
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);

	return ret;
}

static void amdxdna_drm_close(struct drm_device *ddev, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(ddev);

	XDNA_DBG(xdna, "closing pid %d", client->pid);

	idr_destroy(&client->hwctx_idr);
	cleanup_srcu_struct(&client->hwctx_srcu);
	mutex_destroy(&client->hwctx_lock);
	mutex_destroy(&client->mm_lock);
	if (client->dev_heap)
		drm_gem_object_put(to_gobj(client->dev_heap));

	iommu_sva_unbind_device(client->sva);

	XDNA_DBG(xdna, "pid %d closed", client->pid);
	kfree(client);
	pm_runtime_mark_last_busy(ddev->dev);
	pm_runtime_put_autosuspend(ddev->dev);
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

static int amdxdna_drm_get_info_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_drm_get_info *args = data;
	int ret;

	if (!xdna->dev_info->ops->get_aie_info)
		return -EOPNOTSUPP;

	XDNA_DBG(xdna, "Request parameter %u", args->param);
	mutex_lock(&xdna->dev_lock);
	ret = xdna->dev_info->ops->get_aie_info(client, args);
	mutex_unlock(&xdna->dev_lock);
	return ret;
}

static const struct drm_ioctl_desc amdxdna_drm_ioctls[] = {
	/* Context */
	DRM_IOCTL_DEF_DRV(AMDXDNA_CREATE_HWCTX, amdxdna_drm_create_hwctx_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_DESTROY_HWCTX, amdxdna_drm_destroy_hwctx_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_CONFIG_HWCTX, amdxdna_drm_config_hwctx_ioctl, 0),
	/* BO */
	DRM_IOCTL_DEF_DRV(AMDXDNA_CREATE_BO, amdxdna_drm_create_bo_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_GET_BO_INFO, amdxdna_drm_get_bo_info_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_SYNC_BO, amdxdna_drm_sync_bo_ioctl, 0),
	/* Execution */
	DRM_IOCTL_DEF_DRV(AMDXDNA_EXEC_CMD, amdxdna_drm_submit_cmd_ioctl, 0),
	/* AIE hardware */
	DRM_IOCTL_DEF_DRV(AMDXDNA_GET_INFO, amdxdna_drm_get_info_ioctl, 0),
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

	.gem_create_object = amdxdna_gem_create_object_cb,
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
	struct device *dev = &pdev->dev;
	struct amdxdna_dev *xdna;
	int ret;

	xdna = devm_drm_dev_alloc(dev, &amdxdna_drm_drv, typeof(*xdna), ddev);
	if (IS_ERR(xdna))
		return PTR_ERR(xdna);

	xdna->dev_info = amdxdna_get_dev_info(pdev);
	if (!xdna->dev_info)
		return -ENODEV;

	drmm_mutex_init(&xdna->ddev, &xdna->dev_lock);
	init_rwsem(&xdna->notifier_lock);
	INIT_LIST_HEAD(&xdna->client_list);
	pci_set_drvdata(pdev, xdna);

	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		might_lock(&xdna->notifier_lock);
		fs_reclaim_release(GFP_KERNEL);
	}

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

	pm_runtime_set_autosuspend_delay(dev, AMDXDNA_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_allow(dev);

	ret = drm_dev_register(&xdna->ddev, 0);
	if (ret) {
		XDNA_ERR(xdna, "DRM register failed, ret %d", ret);
		pm_runtime_forbid(dev);
		goto failed_sysfs_fini;
	}

	pm_runtime_mark_last_busy(dev);
	pm_runtime_put_autosuspend(dev);
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
	struct device *dev = &pdev->dev;
	struct amdxdna_client *client;

	pm_runtime_get_noresume(dev);
	pm_runtime_forbid(dev);

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

static int amdxdna_dev_suspend_nolock(struct amdxdna_dev *xdna)
{
	if (xdna->dev_info->ops->suspend)
		xdna->dev_info->ops->suspend(xdna);

	return 0;
}

static int amdxdna_dev_resume_nolock(struct amdxdna_dev *xdna)
{
	if (xdna->dev_info->ops->resume)
		return xdna->dev_info->ops->resume(xdna);

	return 0;
}

static int amdxdna_pmops_suspend(struct device *dev)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(to_pci_dev(dev));
	struct amdxdna_client *client;

	mutex_lock(&xdna->dev_lock);
	list_for_each_entry(client, &xdna->client_list, node)
		amdxdna_hwctx_suspend(client);

	amdxdna_dev_suspend_nolock(xdna);
	mutex_unlock(&xdna->dev_lock);

	return 0;
}

static int amdxdna_pmops_resume(struct device *dev)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(to_pci_dev(dev));
	struct amdxdna_client *client;
	int ret;

	XDNA_INFO(xdna, "firmware resuming...");
	mutex_lock(&xdna->dev_lock);
	ret = amdxdna_dev_resume_nolock(xdna);
	if (ret) {
		XDNA_ERR(xdna, "resume NPU firmware failed");
		mutex_unlock(&xdna->dev_lock);
		return ret;
	}

	XDNA_INFO(xdna, "hardware context resuming...");
	list_for_each_entry(client, &xdna->client_list, node)
		amdxdna_hwctx_resume(client);
	mutex_unlock(&xdna->dev_lock);

	return 0;
}

static int amdxdna_rpmops_suspend(struct device *dev)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(to_pci_dev(dev));
	int ret;

	mutex_lock(&xdna->dev_lock);
	ret = amdxdna_dev_suspend_nolock(xdna);
	mutex_unlock(&xdna->dev_lock);

	XDNA_DBG(xdna, "Runtime suspend done ret: %d", ret);
	return ret;
}

static int amdxdna_rpmops_resume(struct device *dev)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(to_pci_dev(dev));
	int ret;

	mutex_lock(&xdna->dev_lock);
	ret = amdxdna_dev_resume_nolock(xdna);
	mutex_unlock(&xdna->dev_lock);

	XDNA_DBG(xdna, "Runtime resume done ret: %d", ret);
	return ret;
}

static const struct dev_pm_ops amdxdna_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(amdxdna_pmops_suspend, amdxdna_pmops_resume)
	RUNTIME_PM_OPS(amdxdna_rpmops_suspend, amdxdna_rpmops_resume, NULL)
};

static struct pci_driver amdxdna_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pci_ids,
	.probe = amdxdna_probe,
	.remove = amdxdna_remove,
	.driver.pm = &amdxdna_pm_ops,
};

module_pci_driver(amdxdna_pci_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("XRT Team <runtimeca39d@amd.com>");
MODULE_DESCRIPTION("amdxdna driver");
