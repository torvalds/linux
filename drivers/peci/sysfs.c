// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2021 Intel Corporation

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/peci.h>

#include "internal.h"

static int rescan_controller(struct device *dev, void *data)
{
	if (dev->type != &peci_controller_type)
		return 0;

	return peci_controller_scan_devices(to_peci_controller(dev));
}

static ssize_t rescan_store(const struct bus_type *bus, const char *buf, size_t count)
{
	bool res;
	int ret;

	ret = kstrtobool(buf, &res);
	if (ret)
		return ret;

	if (!res)
		return count;

	ret = bus_for_each_dev(&peci_bus_type, NULL, NULL, rescan_controller);
	if (ret)
		return ret;

	return count;
}
static BUS_ATTR_WO(rescan);

static struct attribute *peci_bus_attrs[] = {
	&bus_attr_rescan.attr,
	NULL
};

static const struct attribute_group peci_bus_group = {
	.attrs = peci_bus_attrs,
};

const struct attribute_group *peci_bus_groups[] = {
	&peci_bus_group,
	NULL
};

static ssize_t remove_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t count)
{
	struct peci_device *device = to_peci_device(dev);
	bool res;
	int ret;

	ret = kstrtobool(buf, &res);
	if (ret)
		return ret;

	if (res && device_remove_file_self(dev, attr))
		peci_device_destroy(device);

	return count;
}
static DEVICE_ATTR_IGNORE_LOCKDEP(remove, 0200, NULL, remove_store);

static struct attribute *peci_device_attrs[] = {
	&dev_attr_remove.attr,
	NULL
};

static const struct attribute_group peci_device_group = {
	.attrs = peci_device_attrs,
};

const struct attribute_group *peci_device_groups[] = {
	&peci_device_group,
	NULL
};
