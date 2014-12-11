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
#define gb_module_attr(field, type)					\
static ssize_t module_##field##_show(struct device *dev,		\
				     struct device_attribute *attr,	\
				     char *buf)				\
{									\
	struct gb_interface_block *gb_ib = to_gb_interface_block(dev);	\
	return sprintf(buf, "%"#type"\n", gb_ib->field);		\
}									\
static DEVICE_ATTR_RO(module_##field)

gb_module_attr(vendor, x);
gb_module_attr(product, x);
gb_module_attr(unique_id, llX);
gb_module_attr(vendor_string, s);
gb_module_attr(product_string, s);

static struct attribute *module_attrs[] = {
	&dev_attr_module_vendor.attr,
	&dev_attr_module_product.attr,
	&dev_attr_module_unique_id.attr,
	&dev_attr_module_vendor_string.attr,
	&dev_attr_module_product_string.attr,
	NULL,
};

static struct attribute_group module_attr_grp = {
	.attrs =	module_attrs,
};

const struct attribute_group *greybus_module_groups[] = {
	&module_attr_grp,
	NULL,
};

