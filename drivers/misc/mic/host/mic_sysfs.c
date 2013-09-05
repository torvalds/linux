/*
 * Intel MIC Platform Software Stack (MPSS)
 *
 * Copyright(c) 2013 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Intel MIC Host driver.
 *
 */
#include <linux/pci.h>

#include "../common/mic_device.h"
#include "mic_device.h"

static ssize_t
mic_show_family(struct device *dev, struct device_attribute *attr, char *buf)
{
	static const char x100[] = "x100";
	static const char unknown[] = "Unknown";
	const char *card = NULL;
	struct mic_device *mdev = dev_get_drvdata(dev->parent);

	if (!mdev)
		return -EINVAL;

	switch (mdev->family) {
	case MIC_FAMILY_X100:
		card = x100;
		break;
	default:
		card = unknown;
		break;
	}
	return scnprintf(buf, PAGE_SIZE, "%s\n", card);
}
static DEVICE_ATTR(family, S_IRUGO, mic_show_family, NULL);

static ssize_t
mic_show_stepping(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct mic_device *mdev = dev_get_drvdata(dev->parent);
	char *string = "??";

	if (!mdev)
		return -EINVAL;

	switch (mdev->stepping) {
	case MIC_A0_STEP:
		string = "A0";
		break;
	case MIC_B0_STEP:
		string = "B0";
		break;
	case MIC_B1_STEP:
		string = "B1";
		break;
	case MIC_C0_STEP:
		string = "C0";
		break;
	default:
		break;
	}
	return scnprintf(buf, PAGE_SIZE, "%s\n", string);
}
static DEVICE_ATTR(stepping, S_IRUGO, mic_show_stepping, NULL);

static struct attribute *mic_default_attrs[] = {
	&dev_attr_family.attr,
	&dev_attr_stepping.attr,

	NULL
};

static struct attribute_group mic_attr_group = {
	.attrs = mic_default_attrs,
};

static const struct attribute_group *__mic_attr_group[] = {
	&mic_attr_group,
	NULL
};

void mic_sysfs_init(struct mic_device *mdev)
{
	mdev->attr_group = __mic_attr_group;
}
