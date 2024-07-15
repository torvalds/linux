// SPDX-License-Identifier: GPL-2.0-only
// Copyright 2014 Cisco Systems, Inc.  All rights reserved.

#include <linux/string.h>
#include <linux/device.h>

#include "snic.h"

static ssize_t
snic_show_sym_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct snic *snic = shost_priv(class_to_shost(dev));

	return sysfs_emit(buf, "%s\n", snic->name);
}

static ssize_t
snic_show_state(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct snic *snic = shost_priv(class_to_shost(dev));

	return sysfs_emit(buf, "%s\n", snic_state_str[snic_get_state(snic)]);
}

static ssize_t
snic_show_drv_version(struct device *dev,
		      struct device_attribute *attr,
		      char *buf)
{
	return sysfs_emit(buf, "%s\n", SNIC_DRV_VERSION);
}

static ssize_t
snic_show_link_state(struct device *dev,
		     struct device_attribute *attr,
		     char *buf)
{
	struct snic *snic = shost_priv(class_to_shost(dev));

	if (snic->config.xpt_type == SNIC_DAS)
		snic->link_status = svnic_dev_link_status(snic->vdev);

	return sysfs_emit(buf, "%s\n",
			  (snic->link_status) ? "Link Up" : "Link Down");
}

static DEVICE_ATTR(snic_sym_name, S_IRUGO, snic_show_sym_name, NULL);
static DEVICE_ATTR(snic_state, S_IRUGO, snic_show_state, NULL);
static DEVICE_ATTR(drv_version, S_IRUGO, snic_show_drv_version, NULL);
static DEVICE_ATTR(link_state, S_IRUGO, snic_show_link_state, NULL);

static struct attribute *snic_host_attrs[] = {
	&dev_attr_snic_sym_name.attr,
	&dev_attr_snic_state.attr,
	&dev_attr_drv_version.attr,
	&dev_attr_link_state.attr,
	NULL,
};

static const struct attribute_group snic_host_attr_group = {
	.attrs = snic_host_attrs
};

const struct attribute_group *snic_host_groups[] = {
	&snic_host_attr_group,
	NULL
};
