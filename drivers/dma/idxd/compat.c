// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/device/bus.h>
#include "idxd.h"

extern void device_driver_detach(struct device *dev);

#define DRIVER_ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store)	\
	struct driver_attribute driver_attr_##_name =		\
	__ATTR_IGNORE_LOCKDEP(_name, _mode, _show, _store)

static ssize_t unbind_store(struct device_driver *drv, const char *buf, size_t count)
{
	const struct bus_type *bus = drv->bus;
	struct device *dev;
	int rc = -ENODEV;

	dev = bus_find_device_by_name(bus, NULL, buf);
	if (dev && dev->driver) {
		device_driver_detach(dev);
		rc = count;
	}

	return rc;
}
static DRIVER_ATTR_IGNORE_LOCKDEP(unbind, 0200, NULL, unbind_store);

static ssize_t bind_store(struct device_driver *drv, const char *buf, size_t count)
{
	const struct bus_type *bus = drv->bus;
	struct device *dev;
	struct device_driver *alt_drv = NULL;
	int rc = -ENODEV;
	struct idxd_dev *idxd_dev;

	dev = bus_find_device_by_name(bus, NULL, buf);
	if (!dev || dev->driver || drv != &dsa_drv.drv)
		return -ENODEV;

	idxd_dev = confdev_to_idxd_dev(dev);
	if (is_idxd_dev(idxd_dev)) {
		alt_drv = driver_find("idxd", bus);
	} else if (is_idxd_wq_dev(idxd_dev)) {
		struct idxd_wq *wq = confdev_to_wq(dev);

		if (is_idxd_wq_kernel(wq))
			alt_drv = driver_find("dmaengine", bus);
		else if (is_idxd_wq_user(wq))
			alt_drv = driver_find("user", bus);
	}
	if (!alt_drv)
		return -ENODEV;

	rc = device_driver_attach(alt_drv, dev);
	if (rc < 0)
		return rc;

	return count;
}
static DRIVER_ATTR_IGNORE_LOCKDEP(bind, 0200, NULL, bind_store);

static struct attribute *dsa_drv_compat_attrs[] = {
	&driver_attr_bind.attr,
	&driver_attr_unbind.attr,
	NULL,
};

static const struct attribute_group dsa_drv_compat_attr_group = {
	.attrs = dsa_drv_compat_attrs,
};

static const struct attribute_group *dsa_drv_compat_groups[] = {
	&dsa_drv_compat_attr_group,
	NULL,
};

static int idxd_dsa_drv_probe(struct idxd_dev *idxd_dev)
{
	return -ENODEV;
}

static void idxd_dsa_drv_remove(struct idxd_dev *idxd_dev)
{
}

static enum idxd_dev_type dev_types[] = {
	IDXD_DEV_NONE,
};

struct idxd_device_driver dsa_drv = {
	.name = "dsa",
	.probe = idxd_dsa_drv_probe,
	.remove = idxd_dsa_drv_remove,
	.type = dev_types,
	.drv = {
		.suppress_bind_attrs = true,
		.groups = dsa_drv_compat_groups,
	},
};

module_idxd_driver(dsa_drv);
MODULE_IMPORT_NS("IDXD");
