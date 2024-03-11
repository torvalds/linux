// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#include <linux/kobject.h>
#include <linux/pci.h>
#include <linux/sysfs.h>

#include <drm/drm_managed.h>

#include "xe_device.h"
#include "xe_device_sysfs.h"
#include "xe_pm.h"

/**
 * DOC: Xe device sysfs
 * Xe driver requires exposing certain tunable knobs controlled by user space for
 * each graphics device. Considering this, we need to add sysfs attributes at device
 * level granularity.
 * These sysfs attributes will be available under pci device kobj directory.
 *
 * vram_d3cold_threshold - Report/change vram used threshold(in MB) below
 * which vram save/restore is permissible during runtime D3cold entry/exit.
 */

static ssize_t
vram_d3cold_threshold_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	int ret;

	if (!xe)
		return -EINVAL;

	ret = sysfs_emit(buf, "%d\n", xe->d3cold.vram_threshold);

	return ret;
}

static ssize_t
vram_d3cold_threshold_store(struct device *dev, struct device_attribute *attr,
			    const char *buff, size_t count)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct xe_device *xe = pdev_to_xe_device(pdev);
	u32 vram_d3cold_threshold;
	int ret;

	if (!xe)
		return -EINVAL;

	ret = kstrtou32(buff, 0, &vram_d3cold_threshold);
	if (ret)
		return ret;

	drm_dbg(&xe->drm, "vram_d3cold_threshold: %u\n", vram_d3cold_threshold);

	ret = xe_pm_set_vram_threshold(xe, vram_d3cold_threshold);

	return ret ?: count;
}

static DEVICE_ATTR_RW(vram_d3cold_threshold);

static void xe_device_sysfs_fini(struct drm_device *drm, void *arg)
{
	struct xe_device *xe = arg;

	sysfs_remove_file(&xe->drm.dev->kobj, &dev_attr_vram_d3cold_threshold.attr);
}

void xe_device_sysfs_init(struct xe_device *xe)
{
	struct device *dev = xe->drm.dev;
	int ret;

	ret = sysfs_create_file(&dev->kobj, &dev_attr_vram_d3cold_threshold.attr);
	if (ret) {
		drm_warn(&xe->drm, "Failed to create sysfs file\n");
		return;
	}

	ret = drmm_add_action_or_reset(&xe->drm, xe_device_sysfs_fini, xe);
	if (ret)
		drm_warn(&xe->drm, "Failed to add sysfs fini drm action\n");
}
