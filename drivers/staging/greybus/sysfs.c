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

/* Function fields */
#define greybus_function_attr(field)					\
static ssize_t function_##field##_show(struct device *dev,		\
				       struct device_attribute *attr,	\
				       char *buf)			\
{									\
	struct greybus_module *gmod = to_greybus_module(dev);		\
	return sprintf(buf, "%d\n", gmod->function.field);		\
}									\
static DEVICE_ATTR_RO(function_##field)

greybus_function_attr(cport);
greybus_function_attr(function_type);

static struct attribute *function_attrs[] = {
	&dev_attr_function_cport.attr,
	&dev_attr_function_function_type.attr,
	NULL,
};

static umode_t function_attrs_are_visible(struct kobject *kobj,
					  struct attribute *a, int n)
{
	struct greybus_module *gmod = to_greybus_module(kobj_to_dev(kobj));

	// FIXME - make this a dynamic structure to "know" if it really is here
	// or not easier?
	if (gmod->function.cport ||
	    gmod->function.function_type)
		return a->mode;
	return 0;
}

static struct attribute_group function_attr_grp = {
	.attrs =	function_attrs,
	.is_visible =	function_attrs_are_visible,
};

/* Module fields */
#define greybus_module_attr(field)					\
static ssize_t module_##field##_show(struct device *dev,		\
				     struct device_attribute *attr,	\
				     char *buf)				\
{									\
	struct greybus_module *gmod = to_greybus_module(dev);		\
	return sprintf(buf, "%x\n", gmod->module.field);		\
}									\
static DEVICE_ATTR_RO(module_##field)

greybus_module_attr(vendor);
greybus_module_attr(product);
greybus_module_attr(version);

static ssize_t module_serial_number_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct greybus_module *gmod = to_greybus_module(dev);

	return sprintf(buf, "%llX\n",
		      (unsigned long long)le64_to_cpu(gmod->module.serial_number));
}
static DEVICE_ATTR_RO(module_serial_number);

static ssize_t module_vendor_string_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct greybus_module *gmod = to_greybus_module(dev);

	return sprintf(buf, "%s",
		       greybus_string(gmod, gmod->module.vendor_stringid));
}
static DEVICE_ATTR_RO(module_vendor_string);

static ssize_t module_product_string_show(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct greybus_module *gmod = to_greybus_module(dev);

	return sprintf(buf, "%s",
		       greybus_string(gmod, gmod->module.product_stringid));
}
static DEVICE_ATTR_RO(module_product_string);

static struct attribute *module_attrs[] = {
	&dev_attr_module_vendor.attr,
	&dev_attr_module_product.attr,
	&dev_attr_module_version.attr,
	&dev_attr_module_serial_number.attr,
	&dev_attr_module_vendor_string.attr,
	&dev_attr_module_product_string.attr,
	NULL,
};

static umode_t module_attrs_are_visible(struct kobject *kobj,
					struct attribute *a, int n)
{
	struct greybus_module *gmod = to_greybus_module(kobj_to_dev(kobj));

	if ((a == &dev_attr_module_vendor_string.attr) &&
	    (gmod->module.vendor_stringid))
		return a->mode;
	if ((a == &dev_attr_module_product_string.attr) &&
	    (gmod->module.product_stringid))
		return a->mode;

	// FIXME - make this a dynamic structure to "know" if it really is here
	// or not easier?
	if (gmod->module.vendor ||
	    gmod->module.product ||
	    gmod->module.version ||
	    gmod->module.serial_number)
		return a->mode;
	return 0;
}

static struct attribute_group module_attr_grp = {
	.attrs =	module_attrs,
	.is_visible =	module_attrs_are_visible,
};




const struct attribute_group *greybus_module_groups[] = {
	&function_attr_grp,
	&module_attr_grp,
	NULL,
};

