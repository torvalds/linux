// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO ZPCI devices support
 *
 * Copyright (C) IBM Corp. 2020.  All rights reserved.
 *	Author(s): Pierre Morel <pmorel@linux.ibm.com>
 *                 Matthew Rosato <mjrosato@linux.ibm.com>
 */
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/uaccess.h>
#include <linux/vfio.h>
#include <linux/vfio_zdev.h>
#include <linux/kvm_host.h>
#include <asm/pci_clp.h>
#include <asm/pci_io.h>

#include "vfio_pci_priv.h"

/*
 * Add the Base PCI Function information to the device info region.
 */
static int zpci_base_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_base cap = {
		.header.id = VFIO_DEVICE_INFO_CAP_ZPCI_BASE,
		.header.version = 3,
		.start_dma = zdev->start_dma,
		.end_dma = zdev->end_dma,
		.pchid = zdev->pchid,
		.vfn = zdev->vfn,
		.fmb_length = zdev->fmb_length,
		.pft = zdev->pft,
		.gid = zdev->pfgid,
		.fh = zdev->fh,
		.ccdf_err_length = sizeof(struct zpci_ccdf_err)
	};

	return vfio_info_add_capability(caps, &cap.header, sizeof(cap));
}

/*
 * Add the Base PCI Function Group information to the device info region.
 */
static int zpci_group_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_group cap = {
		.header.id = VFIO_DEVICE_INFO_CAP_ZPCI_GROUP,
		.header.version = 2,
		.dasm = zdev->dma_mask,
		.msi_addr = zdev->msi_addr,
		.flags = VFIO_DEVICE_INFO_ZPCI_FLAG_REFRESH,
		.mui = zdev->fmb_update,
		.noi = zdev->max_msi,
		.maxstbl = ZPCI_MAX_WRITE_SIZE,
		.version = zdev->version,
		.reserved = 0,
		.imaxstbl = zdev->maxstbl
	};

	return vfio_info_add_capability(caps, &cap.header, sizeof(cap));
}

/*
 * Add the device utility string to the device info region.
 */
static int zpci_util_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_util *cap;
	int cap_size = sizeof(*cap) + CLP_UTIL_STR_LEN;
	int ret;

	cap = kmalloc(cap_size, GFP_KERNEL);
	if (!cap)
		return -ENOMEM;

	cap->header.id = VFIO_DEVICE_INFO_CAP_ZPCI_UTIL;
	cap->header.version = 1;
	cap->size = CLP_UTIL_STR_LEN;
	memcpy(cap->util_str, zdev->util_str, cap->size);

	ret = vfio_info_add_capability(caps, &cap->header, cap_size);

	kfree(cap);

	return ret;
}

/*
 * Add the function path string to the device info region.
 */
static int zpci_pfip_cap(struct zpci_dev *zdev, struct vfio_info_cap *caps)
{
	struct vfio_device_info_cap_zpci_pfip *cap;
	int cap_size = sizeof(*cap) + CLP_PFIP_NR_SEGMENTS;
	int ret;

	cap = kmalloc(cap_size, GFP_KERNEL);
	if (!cap)
		return -ENOMEM;

	cap->header.id = VFIO_DEVICE_INFO_CAP_ZPCI_PFIP;
	cap->header.version = 1;
	cap->size = CLP_PFIP_NR_SEGMENTS;
	memcpy(cap->pfip, zdev->pfip, cap->size);

	ret = vfio_info_add_capability(caps, &cap->header, cap_size);

	kfree(cap);

	return ret;
}

/*
 * Add all supported capabilities to the VFIO_DEVICE_GET_INFO capability chain.
 */
int vfio_pci_info_zdev_add_caps(struct vfio_pci_core_device *vdev,
				struct vfio_info_cap *caps)
{
	struct zpci_dev *zdev = to_zpci(vdev->pdev);
	int ret;

	if (!zdev)
		return -ENODEV;

	ret = zpci_base_cap(zdev, caps);
	if (ret)
		return ret;

	ret = zpci_group_cap(zdev, caps);
	if (ret)
		return ret;

	if (zdev->util_str_avail) {
		ret = zpci_util_cap(zdev, caps);
		if (ret)
			return ret;
	}

	ret = zpci_pfip_cap(zdev, caps);

	return ret;
}

int vfio_pci_zdev_feature_err(struct vfio_device *device, u32 flags,
			      void __user *arg, size_t argsz)
{
	struct vfio_device_feature_zpci_err err;
	struct vfio_pci_core_device *vdev;
	struct zpci_ccdf_err ccdf = {};
	struct zpci_dev *zdev;
	int ret;

	vdev = container_of(device, struct vfio_pci_core_device, vdev);
	zdev = to_zpci(vdev->pdev);
	if (!zdev)
		return -ENODEV;

	ret = vfio_check_feature(flags, argsz, VFIO_DEVICE_FEATURE_GET,
				 sizeof(err));
	if (ret != 1)
		return ret;

	if (copy_from_user(&err, arg, sizeof(err)))
		return -EFAULT;

	if (!err.data)
		return -EINVAL;

	ret = zpci_get_pending_error(zdev, &ccdf);
	if (ret)
		return ret;

	if (copy_to_user(u64_to_user_ptr(err.data), &ccdf, sizeof(ccdf))) {
		dev_warn_ratelimited(device->dev,
				     "Failed to handle PCI error event for PCI function 0x%x",
				     zdev->fid);
		return -EFAULT;
	}

	return 0;
}

int vfio_pci_zdev_open_device(struct vfio_pci_core_device *vdev)
{
	struct zpci_dev *zdev = to_zpci(vdev->pdev);
	int ret;

	if (!zdev)
		return -ENODEV;

	zpci_start_mediated_recovery(zdev);

	if (!vdev->vdev.kvm)
		return 0;

	ret = -ENOENT;
	if (zpci_kvm_hook.kvm_register)
		ret = zpci_kvm_hook.kvm_register(zdev, vdev->vdev.kvm);

	if (ret)
		zpci_stop_mediated_recovery(zdev);

	return ret;
}

void vfio_pci_zdev_close_device(struct vfio_pci_core_device *vdev)
{
	struct zpci_dev *zdev = to_zpci(vdev->pdev);

	if (!zdev)
		return;

	zpci_stop_mediated_recovery(zdev);

	if (!vdev->vdev.kvm)
		return;

	if (zpci_kvm_hook.kvm_unregister)
		zpci_kvm_hook.kvm_unregister(zdev);
}
