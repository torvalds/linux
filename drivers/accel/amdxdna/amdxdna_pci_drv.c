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

#include "amdxdna_cbuf.h"
#include "amdxdna_ctx.h"
#include "amdxdna_debugfs.h"
#include "amdxdna_gem.h"
#include "amdxdna_pci_drv.h"
#include "amdxdna_pm.h"

MODULE_FIRMWARE("amdnpu/1502_00/npu.sbin");
MODULE_FIRMWARE("amdnpu/17f0_10/npu.sbin");
MODULE_FIRMWARE("amdnpu/17f0_11/npu.sbin");
MODULE_FIRMWARE("amdnpu/17f0_20/npu.sbin");
MODULE_FIRMWARE("amdnpu/1502_00/npu_7.sbin");
MODULE_FIRMWARE("amdnpu/17f0_10/npu_7.sbin");
MODULE_FIRMWARE("amdnpu/17f0_11/npu_7.sbin");

/*
 * 0.0: Initial version
 * 0.1: Support getting all hardware contexts by DRM_IOCTL_AMDXDNA_GET_ARRAY
 * 0.2: Support getting last error hardware error
 * 0.3: Support firmware debug buffer
 * 0.4: Support getting resource information
 * 0.5: Support getting telemetry data
 * 0.6: Support preemption
 * 0.7: Support getting power and utilization data
 * 0.8: Support BO usage query
 * 0.9: Add new device type AMDXDNA_DEV_TYPE_PF
 * 0.10: Support AIE4 UMQ
 */
#define AMDXDNA_DRIVER_MAJOR		0
#define AMDXDNA_DRIVER_MINOR		10

/*
 * Bind the driver base on (vendor_id, device_id) pair and later use the
 * (device_id, rev_id) pair as a key to select the devices. The devices with
 * same device_id have very similar interface to host driver.
 */
static const struct pci_device_id pci_ids[] = {
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x1502) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x17f0) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x17f2) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x17f3) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x1B0B) },
	{ PCI_DEVICE(PCI_VENDOR_ID_AMD, 0x1B0C) },
	{0}
};

MODULE_DEVICE_TABLE(pci, pci_ids);

static const struct amdxdna_device_id amdxdna_ids[] = {
	{ 0x1502, 0x0,  &dev_npu1_info },
	{ 0x17f0, 0x10, &dev_npu4_info },
	{ 0x17f0, 0x11, &dev_npu5_info },
	{ 0x17f0, 0x20, &dev_npu6_info },
	{ 0x17f2, 0x10, &dev_npu3_pf_info },
	{ 0x17f3, 0x10, &dev_npu3_vf_info },
	{ 0x1B0B, 0x10, &dev_npu3_pf_info },
	{ 0x1B0C, 0x10, &dev_npu3_vf_info },
	{0}
};

static int amdxdna_sva_init(struct amdxdna_client *client)
{
	struct amdxdna_dev *xdna = client->xdna;

	client->sva = iommu_sva_bind_device(xdna->ddev.dev, client->mm);
	if (IS_ERR(client->sva)) {
		XDNA_ERR(xdna, "SVA bind device failed, ret %ld", PTR_ERR(client->sva));
		return PTR_ERR(client->sva);
	}

	client->pasid = iommu_sva_get_pasid(client->sva);
	if (client->pasid == IOMMU_PASID_INVALID) {
		iommu_sva_unbind_device(client->sva);
		client->sva = NULL;
		XDNA_ERR(xdna, "SVA get pasid failed");
		return -ENODEV;
	}

	return 0;
}

static void amdxdna_sva_fini(struct amdxdna_client *client)
{
	if (IS_ERR_OR_NULL(client->sva))
		return;

	iommu_sva_unbind_device(client->sva);
	client->sva = NULL;
	client->pasid = IOMMU_PASID_INVALID;
}

static int amdxdna_drm_open(struct drm_device *ddev, struct drm_file *filp)
{
	struct amdxdna_dev *xdna = to_xdna_dev(ddev);
	struct amdxdna_client *client;

	client = kzalloc_obj(*client);
	if (!client)
		return -ENOMEM;

	client->pid = pid_nr(rcu_access_pointer(filp->pid));
	client->xdna = xdna;
	client->pasid = IOMMU_PASID_INVALID;
	client->mm = current->mm;

	if (!amdxdna_iova_on(xdna)) {
		/* No need to fail open since user may use pa + carveout later. */
		if (amdxdna_sva_init(client)) {
			XDNA_WARN(xdna, "PASID not available for pid %d", client->pid);
			if (!amdxdna_use_carveout(xdna)) {
				XDNA_ERR(xdna, "PASID unavailable and carveout not configured");
				kfree(client);
				return -EINVAL;
			}
		}
	}
	mmgrab(client->mm);
	init_srcu_struct(&client->hwctx_srcu);
	xa_init_flags(&client->hwctx_xa, XA_FLAGS_ALLOC);
	xa_init_flags(&client->dev_heap_xa, XA_FLAGS_ALLOC);
	drm_mm_init(&client->dev_heap_mm, xdna->dev_info->dev_mem_base,
		    xdna->dev_info->dev_heap_max_size);
	mutex_init(&client->mm_lock);

	mutex_lock(&xdna->client_lock);
	mutex_lock(&xdna->dev_lock);
	list_add_tail(&client->node, &xdna->client_list);
	mutex_unlock(&xdna->dev_lock);
	mutex_unlock(&xdna->client_lock);

	filp->driver_priv = client;
	client->filp = filp;

	XDNA_DBG(xdna, "pid %d opened", client->pid);
	return 0;
}

static void amdxdna_client_cleanup(struct amdxdna_client *client)
{
	struct amdxdna_gem_obj *heap;
	unsigned long heap_id;

	list_del(&client->node);
	amdxdna_hwctx_remove_all(client);
	xa_destroy(&client->hwctx_xa);
	cleanup_srcu_struct(&client->hwctx_srcu);

	xa_for_each(&client->dev_heap_xa, heap_id, heap)
		drm_gem_object_put(to_gobj(heap));
	xa_destroy(&client->dev_heap_xa);
	drm_mm_takedown(&client->dev_heap_mm);

	mutex_destroy(&client->mm_lock);
	mmdrop(client->mm);
	amdxdna_sva_fini(client);
	kfree(client);
}

static void amdxdna_drm_close(struct drm_device *ddev, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(ddev);

	XDNA_DBG(xdna, "closing pid %d", client->pid);

	mutex_lock(&xdna->client_lock);
	mutex_lock(&xdna->dev_lock);
	amdxdna_client_cleanup(client);
	mutex_unlock(&xdna->dev_lock);
	mutex_unlock(&xdna->client_lock);
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

static int amdxdna_drm_get_array_ioctl(struct drm_device *dev, void *data,
				       struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_drm_get_array *args = data;

	if (!xdna->dev_info->ops->get_array)
		return -EOPNOTSUPP;

	if (args->pad || !args->num_element || !args->element_size)
		return -EINVAL;

	guard(mutex)(&xdna->dev_lock);
	return xdna->dev_info->ops->get_array(client, args);
}

static int amdxdna_drm_set_state_ioctl(struct drm_device *dev, void *data, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	struct amdxdna_dev *xdna = to_xdna_dev(dev);
	struct amdxdna_drm_set_state *args = data;
	int ret;

	if (!xdna->dev_info->ops->set_aie_state)
		return -EOPNOTSUPP;

	XDNA_DBG(xdna, "Request parameter %u", args->param);
	mutex_lock(&xdna->dev_lock);
	ret = xdna->dev_info->ops->set_aie_state(client, args);
	mutex_unlock(&xdna->dev_lock);

	return ret;
}

static int amdxdna_drm_gem_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct drm_file *drm_filp = filp->private_data;
	struct amdxdna_client *client = drm_filp->driver_priv;
	struct amdxdna_dev *xdna = client->xdna;

	if (likely(vma->vm_pgoff >= DRM_FILE_PAGE_OFFSET_START))
		return drm_gem_mmap(filp, vma);

	if (!xdna->dev_info->ops->mmap)
		return -EOPNOTSUPP;

	return xdna->dev_info->ops->mmap(client, vma);
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
	DRM_IOCTL_DEF_DRV(AMDXDNA_WAIT_CMD, amdxdna_drm_wait_cmd_ioctl, 0),
	/* AIE hardware */
	DRM_IOCTL_DEF_DRV(AMDXDNA_GET_INFO, amdxdna_drm_get_info_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_GET_ARRAY, amdxdna_drm_get_array_ioctl, 0),
	DRM_IOCTL_DEF_DRV(AMDXDNA_SET_STATE, amdxdna_drm_set_state_ioctl, DRM_ROOT_ONLY),
};

static void amdxdna_show_fdinfo(struct drm_printer *p, struct drm_file *filp)
{
	struct amdxdna_client *client = filp->driver_priv;
	size_t heap_usage, external_usage, internal_usage;
	char *drv_name = filp->minor->dev->driver->name;

	mutex_lock(&client->mm_lock);

	heap_usage = client->heap_usage;
	internal_usage = client->total_int_bo_usage;
	external_usage = client->total_bo_usage - internal_usage;

	mutex_unlock(&client->mm_lock);

	/*
	 * Note for driver specific BO memory usage stat.
	 * Total memory in use = amdxdna-internal-alloc + amdxdna-external-alloc, which
	 * includes both imported and created BOs. To avoid double counts, it includes
	 * HEAP BO, but not DEV BO. DEV BO is counted by amdxdna-heap-alloc.
	 */
	drm_fdinfo_print_size(p, drv_name, "heap", "alloc", heap_usage);
	drm_fdinfo_print_size(p, drv_name, "internal", "alloc", internal_usage);
	drm_fdinfo_print_size(p, drv_name, "external", "alloc", external_usage);
	/*
	 * Note for DRM standard BO memory stat.
	 * drm-total-memory counts both DEV BO and HEAP BO. The DEV BO size is double counted.
	 * drm-shared-memory counts BO shared with other processes/devices.
	 */
	drm_show_memory_stats(p, filp);
}

static const struct file_operations amdxdna_fops = {
	.owner		= THIS_MODULE,
	.open		= accel_open,
	.release	= drm_release,
	.unlocked_ioctl	= drm_ioctl,
	.compat_ioctl	= drm_compat_ioctl,
	.poll		= drm_poll,
	.read		= drm_read,
	.llseek		= noop_llseek,
	.mmap		= amdxdna_drm_gem_mmap,
	.show_fdinfo	= drm_show_fdinfo,
	.fop_flags	= FOP_UNSIGNED_OFFSET,
};

const struct drm_driver amdxdna_drm_drv = {
	.driver_features = DRIVER_GEM | DRIVER_COMPUTE_ACCEL |
		DRIVER_SYNCOBJ | DRIVER_SYNCOBJ_TIMELINE,
	.fops = &amdxdna_fops,
	.name = "amdxdna_accel_driver",
	.desc = "AMD XDNA DRM implementation",
	.major = AMDXDNA_DRIVER_MAJOR,
	.minor = AMDXDNA_DRIVER_MINOR,
	.open = amdxdna_drm_open,
	.postclose = amdxdna_drm_close,
	.ioctls = amdxdna_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(amdxdna_drm_ioctls),
	.show_fdinfo = amdxdna_show_fdinfo,
	.gem_create_object = amdxdna_gem_create_shmem_object_cb,
	.gem_prime_import = amdxdna_gem_prime_import,
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

static void amdxdna_xdna_drm_release(struct drm_device *drm, void *res)
{
	struct amdxdna_dev *xdna = res;

	amdxdna_carveout_fini(xdna);
}

static int amdxdna_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct device *dev = &pdev->dev;
	struct amdxdna_dev *xdna;
	struct drm_device *ddev;
	int ret;

	xdna = devm_drm_dev_alloc(dev, &amdxdna_drm_drv, typeof(*xdna), ddev);
	if (IS_ERR(xdna))
		return PTR_ERR(xdna);
	ddev = &xdna->ddev;

	xdna->dev_info = amdxdna_get_dev_info(pdev);
	if (!xdna->dev_info)
		return -ENODEV;

	ret = drmm_mutex_init(ddev, &xdna->client_lock);
	if (ret)
		return ret;

	drmm_mutex_init(ddev, &xdna->dev_lock);
	init_rwsem(&xdna->notifier_lock);
	INIT_LIST_HEAD(&xdna->client_list);
	pci_set_drvdata(pdev, xdna);

	ret = drmm_add_action(ddev, amdxdna_xdna_drm_release, xdna);
	if (ret)
		return ret;

	if (IS_ENABLED(CONFIG_LOCKDEP)) {
		fs_reclaim_acquire(GFP_KERNEL);
		might_lock(&xdna->notifier_lock);
		fs_reclaim_release(GFP_KERNEL);
	}

	ret = amdxdna_iommu_init(xdna);
	if (ret)
		return ret;

	xdna->notifier_wq = drmm_alloc_ordered_workqueue(ddev, "notifier_wq", WQ_MEM_RECLAIM);
	if (IS_ERR(xdna->notifier_wq)) {
		ret = PTR_ERR(xdna->notifier_wq);
		goto iommu_fini;
	}

	mutex_lock(&xdna->dev_lock);
	ret = xdna->dev_info->ops->init(xdna);
	mutex_unlock(&xdna->dev_lock);
	if (ret) {
		XDNA_ERR(xdna, "Hardware init failed, ret %d", ret);
		goto iommu_fini;
	}

	ret = amdxdna_sysfs_init(xdna);
	if (ret) {
		XDNA_ERR(xdna, "Create amdxdna attrs failed: %d", ret);
		goto failed_dev_fini;
	}

	ret = drm_dev_register(ddev, 0);
	if (ret) {
		XDNA_ERR(xdna, "DRM register failed, ret %d", ret);
		goto failed_sysfs_fini;
	}

	amdxdna_debugfs_init(xdna);
	return 0;

failed_sysfs_fini:
	amdxdna_sysfs_fini(xdna);
failed_dev_fini:
	mutex_lock(&xdna->dev_lock);
	xdna->dev_info->ops->fini(xdna);
	mutex_unlock(&xdna->dev_lock);
iommu_fini:
	amdxdna_iommu_fini(xdna);
	return ret;
}

static void amdxdna_remove(struct pci_dev *pdev)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(pdev);
	struct amdxdna_client *client;

	drm_dev_unplug(&xdna->ddev);
	amdxdna_sysfs_fini(xdna);

	mutex_lock(&xdna->client_lock);
	mutex_lock(&xdna->dev_lock);
	list_for_each_entry(client, &xdna->client_list, node) {
		amdxdna_hwctx_remove_all(client);
		amdxdna_sva_fini(client);
	}

	xdna->dev_info->ops->fini(xdna);
	mutex_unlock(&xdna->dev_lock);
	mutex_unlock(&xdna->client_lock);

	amdxdna_iommu_fini(xdna);
}

static const struct dev_pm_ops amdxdna_pm_ops = {
	SYSTEM_SLEEP_PM_OPS(amdxdna_pm_suspend, amdxdna_pm_resume)
	RUNTIME_PM_OPS(amdxdna_pm_suspend, amdxdna_pm_resume, NULL)
};

static int amdxdna_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	struct amdxdna_dev *xdna = pci_get_drvdata(pdev);

	guard(mutex)(&xdna->dev_lock);
	if (xdna->dev_info->ops->sriov_configure)
		return xdna->dev_info->ops->sriov_configure(xdna, num_vfs);

	return -ENOENT;
}

static struct pci_driver amdxdna_pci_driver = {
	.name = KBUILD_MODNAME,
	.id_table = pci_ids,
	.probe = amdxdna_probe,
	.remove = amdxdna_remove,
	.driver.pm = &amdxdna_pm_ops,
	.sriov_configure = amdxdna_sriov_configure,
};

module_pci_driver(amdxdna_pci_driver);

MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("AMD_PMF");
MODULE_AUTHOR("XRT Team <runtimeca39d@amd.com>");
MODULE_DESCRIPTION("amdxdna driver");
