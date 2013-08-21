/*
 * Copyright 2012 Red Hat, Inc <mjg@redhat.com>
 * Copyright 2012 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/sysfs.h>
#include <linux/efi-bgrt.h>

static struct kobject *bgrt_kobj;

static ssize_t show_version(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", bgrt_tab->version);
}
static DEVICE_ATTR(version, S_IRUGO, show_version, NULL);

static ssize_t show_status(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", bgrt_tab->status);
}
static DEVICE_ATTR(status, S_IRUGO, show_status, NULL);

static ssize_t show_type(struct device *dev,
			 struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", bgrt_tab->image_type);
}
static DEVICE_ATTR(type, S_IRUGO, show_type, NULL);

static ssize_t show_xoffset(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", bgrt_tab->image_offset_x);
}
static DEVICE_ATTR(xoffset, S_IRUGO, show_xoffset, NULL);

static ssize_t show_yoffset(struct device *dev,
			    struct device_attribute *attr, char *buf)
{
	return snprintf(buf, PAGE_SIZE, "%d\n", bgrt_tab->image_offset_y);
}
static DEVICE_ATTR(yoffset, S_IRUGO, show_yoffset, NULL);

static ssize_t image_read(struct file *file, struct kobject *kobj,
	       struct bin_attribute *attr, char *buf, loff_t off, size_t count)
{
	memcpy(buf, attr->private + off, count);
	return count;
}

static BIN_ATTR_RO(image, 0);	/* size gets filled in later */

static struct attribute *bgrt_attributes[] = {
	&dev_attr_version.attr,
	&dev_attr_status.attr,
	&dev_attr_type.attr,
	&dev_attr_xoffset.attr,
	&dev_attr_yoffset.attr,
	NULL,
};

static struct bin_attribute *bgrt_bin_attributes[] = {
	&bin_attr_image,
	NULL,
};

static struct attribute_group bgrt_attribute_group = {
	.attrs = bgrt_attributes,
	.bin_attrs = bgrt_bin_attributes,
};

static int __init bgrt_init(void)
{
	int ret;

	if (!bgrt_image)
		return -ENODEV;

	sysfs_bin_attr_init(&image_attr);
	bin_attr_image.private = bgrt_image;
	bin_attr_image.size = bgrt_image_size;

	bgrt_kobj = kobject_create_and_add("bgrt", acpi_kobj);
	if (!bgrt_kobj)
		return -EINVAL;

	ret = sysfs_create_group(bgrt_kobj, &bgrt_attribute_group);
	if (ret)
		goto out_kobject;

	return 0;

out_kobject:
	kobject_put(bgrt_kobj);
	return ret;
}

module_init(bgrt_init);

MODULE_AUTHOR("Matthew Garrett, Josh Triplett <josh@joshtriplett.org>");
MODULE_DESCRIPTION("BGRT boot graphic support");
MODULE_LICENSE("GPL");
