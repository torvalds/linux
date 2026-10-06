// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AMD SFH sysfs attributes
 *
 * Copyright (c) 2026, Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Author: Basavaraj Natikar <Basavaraj.Natikar@amd.com>
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/string_choices.h>
#include <linux/sysfs.h>

#include "amd_sfh_common.h"
#include "amd_sfh_sysfs.h"
#include "sfh1_1/amd_sfh_init.h"

static ssize_t hpd_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%s\n", str_enabled_disabled(mp2->dev_en.is_hpd_enabled));
}

static ssize_t hpd_store(struct device *dev,
			 struct device_attribute *attr,
			 const char *buf, size_t count)
{
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);
	bool enabled;
	int ret;

	ret = kstrtobool(buf, &enabled);
	if (ret)
		return ret;

	mp2->sfh1_1_ops->toggle_hpd(mp2, enabled);

	return count;
}
static DEVICE_ATTR_RW(hpd);

static umode_t sfh_attr_is_visible(struct kobject *kobj, struct attribute *attr, int idx)
{
	struct device *dev = kobj_to_dev(kobj);
	struct amd_mp2_dev *mp2 = dev_get_drvdata(dev);

	if (!mp2->sfh1_1_ops || !mp2->dev_en.is_hpd_present)
		return 0;

	return attr->mode;
}

static struct attribute *sfh_attrs[] = {
	&dev_attr_hpd.attr,
	NULL,
};

static struct attribute_group sfh_attr_group = {
	.attrs = sfh_attrs,
	.is_visible = sfh_attr_is_visible,
};

const struct attribute_group *amd_sfh_groups[] = {
	&sfh_attr_group,
	NULL,
};

int amd_sfh_update_sysfs(struct amd_mp2_dev *mp2)
{
	return sysfs_update_group(&mp2->pdev->dev.kobj, &sfh_attr_group);
}
