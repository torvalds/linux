// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023-2024, Advanced Micro Devices, Inc.
 */

#include <drm/amdxdna_accel.h>
#include <drm/drm_device.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_print.h>
#include <drm/gpu_scheduler.h>
#include <linux/types.h>

#include "amdxdna_gem.h"
#include "amdxdna_pci_drv.h"

static ssize_t vbnv_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct amdxdna_dev *xdna = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", xdna->dev_info->vbnv);
}
static DEVICE_ATTR_RO(vbnv);

static ssize_t device_type_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct amdxdna_dev *xdna = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", xdna->dev_info->device_type);
}
static DEVICE_ATTR_RO(device_type);

static ssize_t fw_version_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct amdxdna_dev *xdna = dev_get_drvdata(dev);

	return sprintf(buf, "%d.%d.%d.%d\n", xdna->fw_ver.major,
		       xdna->fw_ver.minor, xdna->fw_ver.sub,
		       xdna->fw_ver.build);
}
static DEVICE_ATTR_RO(fw_version);

static struct attribute *amdxdna_attrs[] = {
	&dev_attr_device_type.attr,
	&dev_attr_vbnv.attr,
	&dev_attr_fw_version.attr,
	NULL,
};

static struct attribute_group amdxdna_attr_group = {
	.attrs = amdxdna_attrs,
};

int amdxdna_sysfs_init(struct amdxdna_dev *xdna)
{
	int ret;

	ret = sysfs_create_group(&xdna->ddev.dev->kobj, &amdxdna_attr_group);
	if (ret)
		XDNA_ERR(xdna, "Create attr group failed");

	return ret;
}

void amdxdna_sysfs_fini(struct amdxdna_dev *xdna)
{
	sysfs_remove_group(&xdna->ddev.dev->kobj, &amdxdna_attr_group);
}
