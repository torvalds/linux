// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021-2022 Digiteq Automotive
 *     author: Martin Tuma <martin.tuma@digiteqautomotive.com>
 *
 * This module handles all the sysfs info/configuration that is related to the
 * PCI card device.
 */

#include <linux/device.h>
#include "mgb4_core.h"
#include "mgb4_sysfs.h"

static ssize_t module_version_show(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct mgb4_dev *mgbdev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", mgbdev->module_version & 0x0F);
}

static ssize_t module_type_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct mgb4_dev *mgbdev = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", mgbdev->module_version >> 4);
}

static ssize_t fw_version_show(struct device *dev,
			       struct device_attribute *attr, char *buf)
{
	struct mgb4_dev *mgbdev = dev_get_drvdata(dev);
	u32 config = mgb4_read_reg(&mgbdev->video, 0xC4);

	return sprintf(buf, "%u\n", config & 0xFFFF);
}

static ssize_t fw_type_show(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	struct mgb4_dev *mgbdev = dev_get_drvdata(dev);
	u32 config = mgb4_read_reg(&mgbdev->video, 0xC4);

	return sprintf(buf, "%u\n", config >> 24);
}

static ssize_t serial_number_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct mgb4_dev *mgbdev = dev_get_drvdata(dev);
	u32 sn = mgbdev->serial_number;

	return sprintf(buf, "%03d-%03d-%03d-%03d\n", sn >> 24, (sn >> 16) & 0xFF,
	  (sn >> 8) & 0xFF, sn & 0xFF);
}

static DEVICE_ATTR_RO(module_version);
static DEVICE_ATTR_RO(module_type);
static DEVICE_ATTR_RO(fw_version);
static DEVICE_ATTR_RO(fw_type);
static DEVICE_ATTR_RO(serial_number);

struct attribute *mgb4_pci_attrs[] = {
	&dev_attr_module_type.attr,
	&dev_attr_module_version.attr,
	&dev_attr_fw_type.attr,
	&dev_attr_fw_version.attr,
	&dev_attr_serial_number.attr,
	NULL
};
