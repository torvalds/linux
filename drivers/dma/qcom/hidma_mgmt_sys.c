/*
 * Qualcomm Technologies HIDMA Management SYS interface
 *
 * Copyright (c) 2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/sysfs.h>
#include <linux/platform_device.h>

#include "hidma_mgmt.h"

struct hidma_chan_attr {
	struct hidma_mgmt_dev *mdev;
	int index;
	struct kobj_attribute attr;
};

struct hidma_mgmt_fileinfo {
	char *name;
	int mode;
	int (*get)(struct hidma_mgmt_dev *mdev);
	int (*set)(struct hidma_mgmt_dev *mdev, u64 val);
};

#define IMPLEMENT_GETSET(name)					\
static int get_##name(struct hidma_mgmt_dev *mdev)		\
{								\
	return mdev->name;					\
}								\
static int set_##name(struct hidma_mgmt_dev *mdev, u64 val)	\
{								\
	u64 tmp;						\
	int rc;							\
								\
	tmp = mdev->name;					\
	mdev->name = val;					\
	rc = hidma_mgmt_setup(mdev);				\
	if (rc)							\
		mdev->name = tmp;				\
	return rc;						\
}

#define DECLARE_ATTRIBUTE(name, mode)				\
	{#name, mode, get_##name, set_##name}

IMPLEMENT_GETSET(hw_version_major)
IMPLEMENT_GETSET(hw_version_minor)
IMPLEMENT_GETSET(max_wr_xactions)
IMPLEMENT_GETSET(max_rd_xactions)
IMPLEMENT_GETSET(max_write_request)
IMPLEMENT_GETSET(max_read_request)
IMPLEMENT_GETSET(dma_channels)
IMPLEMENT_GETSET(chreset_timeout_cycles)

static int set_priority(struct hidma_mgmt_dev *mdev, unsigned int i, u64 val)
{
	u64 tmp;
	int rc;

	if (i >= mdev->dma_channels)
		return -EINVAL;

	tmp = mdev->priority[i];
	mdev->priority[i] = val;
	rc = hidma_mgmt_setup(mdev);
	if (rc)
		mdev->priority[i] = tmp;
	return rc;
}

static int set_weight(struct hidma_mgmt_dev *mdev, unsigned int i, u64 val)
{
	u64 tmp;
	int rc;

	if (i >= mdev->dma_channels)
		return -EINVAL;

	tmp = mdev->weight[i];
	mdev->weight[i] = val;
	rc = hidma_mgmt_setup(mdev);
	if (rc)
		mdev->weight[i] = tmp;
	return rc;
}

static struct hidma_mgmt_fileinfo hidma_mgmt_files[] = {
	DECLARE_ATTRIBUTE(hw_version_major, S_IRUGO),
	DECLARE_ATTRIBUTE(hw_version_minor, S_IRUGO),
	DECLARE_ATTRIBUTE(dma_channels, S_IRUGO),
	DECLARE_ATTRIBUTE(chreset_timeout_cycles, S_IRUGO),
	DECLARE_ATTRIBUTE(max_wr_xactions, S_IRUGO),
	DECLARE_ATTRIBUTE(max_rd_xactions, S_IRUGO),
	DECLARE_ATTRIBUTE(max_write_request, S_IRUGO),
	DECLARE_ATTRIBUTE(max_read_request, S_IRUGO),
};

static ssize_t show_values(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hidma_mgmt_dev *mdev = platform_get_drvdata(pdev);
	unsigned int i;

	buf[0] = 0;

	for (i = 0; i < ARRAY_SIZE(hidma_mgmt_files); i++) {
		if (strcmp(attr->attr.name, hidma_mgmt_files[i].name) == 0) {
			sprintf(buf, "%d\n", hidma_mgmt_files[i].get(mdev));
			break;
		}
	}
	return strlen(buf);
}

static ssize_t set_values(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t count)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct hidma_mgmt_dev *mdev = platform_get_drvdata(pdev);
	unsigned long tmp;
	unsigned int i;
	int rc;

	rc = kstrtoul(buf, 0, &tmp);
	if (rc)
		return rc;

	for (i = 0; i < ARRAY_SIZE(hidma_mgmt_files); i++) {
		if (strcmp(attr->attr.name, hidma_mgmt_files[i].name) == 0) {
			rc = hidma_mgmt_files[i].set(mdev, tmp);
			if (rc)
				return rc;

			break;
		}
	}
	return count;
}

static ssize_t show_values_channel(struct kobject *kobj,
				   struct kobj_attribute *attr, char *buf)
{
	struct hidma_chan_attr *chattr;
	struct hidma_mgmt_dev *mdev;

	buf[0] = 0;
	chattr = container_of(attr, struct hidma_chan_attr, attr);
	mdev = chattr->mdev;
	if (strcmp(attr->attr.name, "priority") == 0)
		sprintf(buf, "%d\n", mdev->priority[chattr->index]);
	else if (strcmp(attr->attr.name, "weight") == 0)
		sprintf(buf, "%d\n", mdev->weight[chattr->index]);

	return strlen(buf);
}

static ssize_t set_values_channel(struct kobject *kobj,
				  struct kobj_attribute *attr, const char *buf,
				  size_t count)
{
	struct hidma_chan_attr *chattr;
	struct hidma_mgmt_dev *mdev;
	unsigned long tmp;
	int rc;

	chattr = container_of(attr, struct hidma_chan_attr, attr);
	mdev = chattr->mdev;

	rc = kstrtoul(buf, 0, &tmp);
	if (rc)
		return rc;

	if (strcmp(attr->attr.name, "priority") == 0) {
		rc = set_priority(mdev, chattr->index, tmp);
		if (rc)
			return rc;
	} else if (strcmp(attr->attr.name, "weight") == 0) {
		rc = set_weight(mdev, chattr->index, tmp);
		if (rc)
			return rc;
	}
	return count;
}

static int create_sysfs_entry(struct hidma_mgmt_dev *dev, char *name, int mode)
{
	struct device_attribute *attrs;
	char *name_copy;

	attrs = devm_kmalloc(&dev->pdev->dev,
			     sizeof(struct device_attribute), GFP_KERNEL);
	if (!attrs)
		return -ENOMEM;

	name_copy = devm_kstrdup(&dev->pdev->dev, name, GFP_KERNEL);
	if (!name_copy)
		return -ENOMEM;

	attrs->attr.name = name_copy;
	attrs->attr.mode = mode;
	attrs->show = show_values;
	attrs->store = set_values;
	sysfs_attr_init(&attrs->attr);

	return device_create_file(&dev->pdev->dev, attrs);
}

static int create_sysfs_entry_channel(struct hidma_mgmt_dev *mdev, char *name,
				      int mode, int index,
				      struct kobject *parent)
{
	struct hidma_chan_attr *chattr;
	char *name_copy;

	chattr = devm_kmalloc(&mdev->pdev->dev, sizeof(*chattr), GFP_KERNEL);
	if (!chattr)
		return -ENOMEM;

	name_copy = devm_kstrdup(&mdev->pdev->dev, name, GFP_KERNEL);
	if (!name_copy)
		return -ENOMEM;

	chattr->mdev = mdev;
	chattr->index = index;
	chattr->attr.attr.name = name_copy;
	chattr->attr.attr.mode = mode;
	chattr->attr.show = show_values_channel;
	chattr->attr.store = set_values_channel;
	sysfs_attr_init(&chattr->attr.attr);

	return sysfs_create_file(parent, &chattr->attr.attr);
}

int hidma_mgmt_init_sys(struct hidma_mgmt_dev *mdev)
{
	unsigned int i;
	int rc;
	int required;
	struct kobject *chanops;

	required = sizeof(*mdev->chroots) * mdev->dma_channels;
	mdev->chroots = devm_kmalloc(&mdev->pdev->dev, required, GFP_KERNEL);
	if (!mdev->chroots)
		return -ENOMEM;

	chanops = kobject_create_and_add("chanops", &mdev->pdev->dev.kobj);
	if (!chanops)
		return -ENOMEM;

	/* create each channel directory here */
	for (i = 0; i < mdev->dma_channels; i++) {
		char name[20];

		snprintf(name, sizeof(name), "chan%d", i);
		mdev->chroots[i] = kobject_create_and_add(name, chanops);
		if (!mdev->chroots[i])
			return -ENOMEM;
	}

	/* populate common parameters */
	for (i = 0; i < ARRAY_SIZE(hidma_mgmt_files); i++) {
		rc = create_sysfs_entry(mdev, hidma_mgmt_files[i].name,
					hidma_mgmt_files[i].mode);
		if (rc)
			return rc;
	}

	/* populate parameters that are per channel */
	for (i = 0; i < mdev->dma_channels; i++) {
		rc = create_sysfs_entry_channel(mdev, "priority",
						(S_IRUGO | S_IWUGO), i,
						mdev->chroots[i]);
		if (rc)
			return rc;

		rc = create_sysfs_entry_channel(mdev, "weight",
						(S_IRUGO | S_IWUGO), i,
						mdev->chroots[i]);
		if (rc)
			return rc;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(hidma_mgmt_init_sys);
