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

/* register based attributes */

/* macro to access RO registers with power check only (no enable check). */
#define coresight_cti_reg(name, offset)			\
static ssize_t name##_show(struct device *dev,				\
			   struct device_attribute *attr, char *buf)	\
{									\
	struct cti_drvdata *drvdata = dev_get_drvdata(dev->parent);	\
	u32 val = 0;							\
	pm_runtime_get_sync(dev->parent);				\
	spin_lock(&drvdata->spinlock);					\
	if (drvdata->config.hw_powered)					\
		val = readl_relaxed(drvdata->base + offset);		\
	spin_unlock(&drvdata->spinlock);				\
	pm_runtime_put_sync(dev->parent);				\
	return sprintf(buf, "0x%x\n", val);				\
}									\
static DEVICE_ATTR_RO(name)

/* coresight management registers */
coresight_cti_reg(devaff0, CTIDEVAFF0);
coresight_cti_reg(devaff1, CTIDEVAFF1);
coresight_cti_reg(authstatus, CORESIGHT_AUTHSTATUS);
coresight_cti_reg(devarch, CORESIGHT_DEVARCH);
coresight_cti_reg(devid, CORESIGHT_DEVID);
coresight_cti_reg(devtype, CORESIGHT_DEVTYPE);
coresight_cti_reg(pidr0, CORESIGHT_PERIPHIDR0);
coresight_cti_reg(pidr1, CORESIGHT_PERIPHIDR1);
coresight_cti_reg(pidr2, CORESIGHT_PERIPHIDR2);
coresight_cti_reg(pidr3, CORESIGHT_PERIPHIDR3);
coresight_cti_reg(pidr4, CORESIGHT_PERIPHIDR4);

static struct attribute *coresight_cti_mgmt_attrs[] = {
	&dev_attr_devaff0.attr,
	&dev_attr_devaff1.attr,
	&dev_attr_authstatus.attr,
	&dev_attr_devarch.attr,
	&dev_attr_devid.attr,
	&dev_attr_devtype.attr,
	&dev_attr_pidr0.attr,
	&dev_attr_pidr1.attr,
	&dev_attr_pidr2.attr,
	&dev_attr_pidr3.attr,
	&dev_attr_pidr4.attr,
	NULL,
};

static const struct attribute_group coresight_cti_group = {
	.attrs = coresight_cti_attrs,
};

static const struct attribute_group coresight_cti_mgmt_group = {
	.attrs = coresight_cti_mgmt_attrs,
	.name = "mgmt",
};

const struct attribute_group *coresight_cti_groups[] = {
	&coresight_cti_group,
	&coresight_cti_mgmt_group,
	NULL,
};
