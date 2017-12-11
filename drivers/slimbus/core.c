// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2011-2017, The Linux Foundation
 */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/slimbus.h>

static const struct slim_device_id *slim_match(const struct slim_device_id *id,
					       const struct slim_device *sbdev)
{
	while (id->manf_id != 0 || id->prod_code != 0) {
		if (id->manf_id == sbdev->e_addr.manf_id &&
		    id->prod_code == sbdev->e_addr.prod_code)
			return id;
		id++;
	}
	return NULL;
}

static int slim_device_match(struct device *dev, struct device_driver *drv)
{
	struct slim_device *sbdev = to_slim_device(dev);
	struct slim_driver *sbdrv = to_slim_driver(drv);

	return !!slim_match(sbdrv->id_table, sbdev);
}

static int slim_device_probe(struct device *dev)
{
	struct slim_device	*sbdev = to_slim_device(dev);
	struct slim_driver	*sbdrv = to_slim_driver(dev->driver);

	return sbdrv->probe(sbdev);
}

static int slim_device_remove(struct device *dev)
{
	struct slim_device *sbdev = to_slim_device(dev);
	struct slim_driver *sbdrv;

	if (dev->driver) {
		sbdrv = to_slim_driver(dev->driver);
		if (sbdrv->remove)
			sbdrv->remove(sbdev);
	}

	return 0;
}

struct bus_type slimbus_bus = {
	.name		= "slimbus",
	.match		= slim_device_match,
	.probe		= slim_device_probe,
	.remove		= slim_device_remove,
};
EXPORT_SYMBOL_GPL(slimbus_bus);

/*
 * __slim_driver_register() - Client driver registration with SLIMbus
 *
 * @drv:Client driver to be associated with client-device.
 * @owner: owning module/driver
 *
 * This API will register the client driver with the SLIMbus
 * It is called from the driver's module-init function.
 */
int __slim_driver_register(struct slim_driver *drv, struct module *owner)
{
	/* ID table and probe are mandatory */
	if (!drv->id_table || !drv->probe)
		return -EINVAL;

	drv->driver.bus = &slimbus_bus;
	drv->driver.owner = owner;

	return driver_register(&drv->driver);
}
EXPORT_SYMBOL_GPL(__slim_driver_register);

/*
 * slim_driver_unregister() - Undo effect of slim_driver_register
 *
 * @drv: Client driver to be unregistered
 */
void slim_driver_unregister(struct slim_driver *drv)
{
	driver_unregister(&drv->driver);
}
EXPORT_SYMBOL_GPL(slim_driver_unregister);

static void __exit slimbus_exit(void)
{
	bus_unregister(&slimbus_bus);
}
module_exit(slimbus_exit);

static int __init slimbus_init(void)
{
	return bus_register(&slimbus_bus);
}
postcore_initcall(slimbus_init);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("SLIMbus core");
