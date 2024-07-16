// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PPS sysfs support
 *
 * Copyright (C) 2007-2009   Rodolfo Giometti <giometti@linux.it>
 */


#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/pps_kernel.h>

/*
 * Attribute functions
 */

static ssize_t assert_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	if (!(pps->info.mode & PPS_CAPTUREASSERT))
		return 0;

	return sprintf(buf, "%lld.%09d#%d\n",
			(long long) pps->assert_tu.sec, pps->assert_tu.nsec,
			pps->assert_sequence);
}
static DEVICE_ATTR_RO(assert);

static ssize_t clear_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	if (!(pps->info.mode & PPS_CAPTURECLEAR))
		return 0;

	return sprintf(buf, "%lld.%09d#%d\n",
			(long long) pps->clear_tu.sec, pps->clear_tu.nsec,
			pps->clear_sequence);
}
static DEVICE_ATTR_RO(clear);

static ssize_t mode_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	return sprintf(buf, "%4x\n", pps->info.mode);
}
static DEVICE_ATTR_RO(mode);

static ssize_t echo_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", !!pps->info.echo);
}
static DEVICE_ATTR_RO(echo);

static ssize_t name_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", pps->info.name);
}
static DEVICE_ATTR_RO(name);

static ssize_t path_show(struct device *dev, struct device_attribute *attr,
			 char *buf)
{
	struct pps_device *pps = dev_get_drvdata(dev);

	return sprintf(buf, "%s\n", pps->info.path);
}
static DEVICE_ATTR_RO(path);

static struct attribute *pps_attrs[] = {
	&dev_attr_assert.attr,
	&dev_attr_clear.attr,
	&dev_attr_mode.attr,
	&dev_attr_echo.attr,
	&dev_attr_name.attr,
	&dev_attr_path.attr,
	NULL,
};

static const struct attribute_group pps_group = {
	.attrs = pps_attrs,
};

const struct attribute_group *pps_groups[] = {
	&pps_group,
	NULL,
};
