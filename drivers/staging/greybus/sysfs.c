/*
 * Greybus sysfs file functions
 *
 * Copyright 2014 Google Inc.
 *
 * Released under the GPLv2 only.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/kthread.h>
#include <linux/device.h>

#include "greybus.h"
#include "kernel_ver.h"

/* Module fields */
#define gb_ib_attr(field, type)					\
static ssize_t field##_show(struct device *dev,		\
				     struct device_attribute *attr,	\
				     char *buf)				\
{									\
	struct gb_interface_block *gb_ib = to_gb_interface_block(dev);	\
	return sprintf(buf, "%"#type"\n", gb_ib->field);		\
}									\
static DEVICE_ATTR_RO(field)

gb_ib_attr(vendor, x);
gb_ib_attr(product, x);
gb_ib_attr(unique_id, llX);
gb_ib_attr(vendor_string, s);
gb_ib_attr(product_string, s);

static struct attribute *interface_block_attrs[] = {
	&dev_attr_vendor.attr,
	&dev_attr_product.attr,
	&dev_attr_unique_id.attr,
	&dev_attr_vendor_string.attr,
	&dev_attr_product_string.attr,
	NULL,
};

static struct attribute_group interface_block_attr_grp = {
	.attrs =	interface_block_attrs,
};

const struct attribute_group *greybus_interface_block_groups[] = {
	&interface_block_attr_grp,
	NULL,
};

