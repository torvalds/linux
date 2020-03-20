// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019 Linaro Limited, All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include <linux/coresight.h>

#include "coresight-cti.h"

/* basic attributes */
static ssize_t enable_show(struct device *dev,
			   struct device_attribute *attr,
			   char *buf)
{
	int enable_req;
	bool enabled, powered;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	enable_req = atomic_read(&drvdata->config.enable_req_count);
	spin_lock(&drvdata->spinlock);
	powered = drvdata->config.hw_powered;
	enabled = drvdata->config.hw_enabled;
	spin_unlock(&drvdata->spinlock);

	if (powered)
		return sprintf(buf, "%d\n", enabled);
	else
		return sprintf(buf, "%d\n", !!enable_req);
}

static ssize_t enable_store(struct device *dev,
			    struct device_attribute *attr,
			    const char *buf, size_t size)
{
	int ret = 0;
	unsigned long val;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	ret = kstrtoul(buf, 0, &val);
	if (ret)
		return ret;

	if (val)
		ret = cti_enable(drvdata->csdev);
	else
		ret = cti_disable(drvdata->csdev);
	if (ret)
		return ret;
	return size;
}
static DEVICE_ATTR_RW(enable);

static ssize_t powered_show(struct device *dev,
			    struct device_attribute *attr,
			    char *buf)
{
	bool powered;
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);

	spin_lock(&drvdata->spinlock);
	powered = drvdata->config.hw_powered;
	spin_unlock(&drvdata->spinlock);

	return sprintf(buf, "%d\n", powered);
}
static DEVICE_ATTR_RO(powered);

/* attribute and group sysfs tables. */
static struct attribute *coresight_cti_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_powered.attr,
	NULL,
};

static const struct attribute_group coresight_cti_group = {
	.attrs = coresight_cti_attrs,
};

const struct attribute_group *coresight_cti_groups[] = {
	&coresight_cti_group,
	NULL,
};
