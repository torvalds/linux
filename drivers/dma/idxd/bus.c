// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2021 Intel Corporation. All rights rsvd. */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include "idxd.h"


int __idxd_driver_register(struct idxd_device_driver *idxd_drv, struct module *owner,
			   const char *mod_name)
{
	struct device_driver *drv = &idxd_drv->drv;

	if (!idxd_drv->type) {
		pr_debug("driver type not set (%ps)\n", __builtin_return_address(0));
		return -EINVAL;
	}

	drv->name = idxd_drv->name;
	drv->bus = &dsa_bus_type;
	drv->owner = owner;
	drv->mod_name = mod_name;

	return driver_register(drv);
}
EXPORT_SYMBOL_GPL(__idxd_driver_register);

void idxd_driver_unregister(struct idxd_device_driver *idxd_drv)
{
	driver_unregister(&idxd_drv->drv);
}
EXPORT_SYMBOL_GPL(idxd_driver_unregister);

static int idxd_config_bus_match(struct device *dev,
				 struct device_driver *drv)
{
	struct idxd_device_driver *idxd_drv =
		container_of(drv, struct idxd_device_driver, drv);
	struct idxd_dev *idxd_dev = confdev_to_idxd_dev(dev);
	int i = 0;

	while (idxd_drv->type[i] != IDXD_DEV_NONE) {
		if (idxd_dev->type == idxd_drv->type[i])
			return 1;
		i++;
	}

	return 0;
}

static int idxd_config_bus_probe(struct device *dev)
{
	struct idxd_device_driver *idxd_drv =
		container_of(dev->driver, struct idxd_device_driver, drv);
	struct idxd_dev *idxd_dev = confdev_to_idxd_dev(dev);

	return idxd_drv->probe(idxd_dev);
}

static void idxd_config_bus_remove(struct device *dev)
{
	struct idxd_device_driver *idxd_drv =
		container_of(dev->driver, struct idxd_device_driver, drv);
	struct idxd_dev *idxd_dev = confdev_to_idxd_dev(dev);

	idxd_drv->remove(idxd_dev);
}

static int idxd_bus_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	return add_uevent_var(env, "MODALIAS=" IDXD_DEVICES_MODALIAS_FMT, 0);
}

const struct bus_type dsa_bus_type = {
	.name = "dsa",
	.match = idxd_config_bus_match,
	.probe = idxd_config_bus_probe,
	.remove = idxd_config_bus_remove,
	.uevent = idxd_bus_uevent,
};
EXPORT_SYMBOL_GPL(dsa_bus_type);

static int __init dsa_bus_init(void)
{
	return bus_register(&dsa_bus_type);
}
module_init(dsa_bus_init);

static void __exit dsa_bus_exit(void)
{
	bus_unregister(&dsa_bus_type);
}
module_exit(dsa_bus_exit);

MODULE_DESCRIPTION("IDXD driver dsa_bus_type driver");
MODULE_LICENSE("GPL v2");
