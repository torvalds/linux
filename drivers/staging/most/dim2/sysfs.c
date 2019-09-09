// SPDX-License-Identifier: GPL-2.0
/*
 * sysfs.c - MediaLB sysfs information
 *
 * Copyright (C) 2015, Microchip Technology Germany II GmbH & Co. KG
 */

/* Author: Andrey Shvetsov <andrey.shvetsov@k2l.de> */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include "sysfs.h"
#include <linux/device.h>

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	bool state = dim2_sysfs_get_state_cb();

	return sprintf(buf, "%s\n", state ? "locked" : "");
}

static DEVICE_ATTR_RO(state);

static struct attribute *dev_attrs[] = {
	&dev_attr_state.attr,
	NULL,
};

static struct attribute_group dev_attr_group = {
	.attrs = dev_attrs,
};

static const struct attribute_group *dev_attr_groups[] = {
	&dev_attr_group,
	NULL,
};

int dim2_sysfs_probe(struct device *dev)
{
	dev->groups = dev_attr_groups;
	return device_register(dev);
}

void dim2_sysfs_destroy(struct device *dev)
{
	device_unregister(dev);
}
