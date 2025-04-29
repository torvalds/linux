// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * PPS generators sysfs support
 *
 * Copyright (C) 2024 Rodolfo Giometti <giometti@enneenne.com>
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/pps_gen_kernel.h>

/*
 * Attribute functions
 */

static ssize_t system_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct pps_gen_device *pps_gen = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%d\n", pps_gen->info->use_system_clock);
}
static DEVICE_ATTR_RO(system);

static ssize_t time_show(struct device *dev, struct device_attribute *attr,
				char *buf)
{
	struct pps_gen_device *pps_gen = dev_get_drvdata(dev);
	struct timespec64 time;
	int ret;

	ret = pps_gen->info->get_time(pps_gen, &time);
	if (ret)
		return ret;

	return sysfs_emit(buf, "%llu %09lu\n", time.tv_sec, time.tv_nsec);
}
static DEVICE_ATTR_RO(time);

static ssize_t enable_store(struct device *dev, struct device_attribute *attr,
				const char *buf, size_t count)
{
	struct pps_gen_device *pps_gen = dev_get_drvdata(dev);
	bool status;
	int ret;

	ret = kstrtobool(buf, &status);
	if (ret)
		return ret;

	ret = pps_gen->info->enable(pps_gen, status);
	if (ret)
		return ret;
	pps_gen->enabled = status;

	return count;
}
static DEVICE_ATTR_WO(enable);

static struct attribute *pps_gen_attrs[] = {
	&dev_attr_enable.attr,
	&dev_attr_time.attr,
	&dev_attr_system.attr,
	NULL,
};

static const struct attribute_group pps_gen_group = {
	.attrs = pps_gen_attrs,
};

const struct attribute_group *pps_gen_groups[] = {
	&pps_gen_group,
	NULL,
};
