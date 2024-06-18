// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2018-2021 Intel Corporation

#include <linux/bug.h>
#include <linux/device.h>
#include <linux/export.h>
#include <linux/idr.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/peci.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/slab.h>

#include "internal.h"

static DEFINE_IDA(peci_controller_ida);

static void peci_controller_dev_release(struct device *dev)
{
	struct peci_controller *controller = to_peci_controller(dev);

	mutex_destroy(&controller->bus_lock);
	ida_free(&peci_controller_ida, controller->id);
	kfree(controller);
}

const struct device_type peci_controller_type = {
	.release	= peci_controller_dev_release,
};

int peci_controller_scan_devices(struct peci_controller *controller)
{
	int ret;
	u8 addr;

	for (addr = PECI_BASE_ADDR; addr < PECI_BASE_ADDR + PECI_DEVICE_NUM_MAX; addr++) {
		ret = peci_device_create(controller, addr);
		if (ret)
			return ret;
	}

	return 0;
}

static struct peci_controller *peci_controller_alloc(struct device *dev,
						     const struct peci_controller_ops *ops)
{
	struct peci_controller *controller;
	int ret;

	if (!ops->xfer)
		return ERR_PTR(-EINVAL);

	controller = kzalloc(sizeof(*controller), GFP_KERNEL);
	if (!controller)
		return ERR_PTR(-ENOMEM);

	ret = ida_alloc_max(&peci_controller_ida, U8_MAX, GFP_KERNEL);
	if (ret < 0)
		goto err;
	controller->id = ret;

	controller->ops = ops;

	controller->dev.parent = dev;
	controller->dev.bus = &peci_bus_type;
	controller->dev.type = &peci_controller_type;

	device_initialize(&controller->dev);

	mutex_init(&controller->bus_lock);

	return controller;

err:
	kfree(controller);
	return ERR_PTR(ret);
}

static int unregister_child(struct device *dev, void *dummy)
{
	peci_device_destroy(to_peci_device(dev));

	return 0;
}

static void unregister_controller(void *_controller)
{
	struct peci_controller *controller = _controller;

	/*
	 * Detach any active PECI devices. This can't fail, thus we do not
	 * check the returned value.
	 */
	device_for_each_child_reverse(&controller->dev, NULL, unregister_child);

	device_unregister(&controller->dev);

	fwnode_handle_put(controller->dev.fwnode);

	pm_runtime_disable(&controller->dev);
}

/**
 * devm_peci_controller_add() - add PECI controller
 * @dev: device for devm operations
 * @ops: pointer to controller specific methods
 *
 * In final stage of its probe(), peci_controller driver calls
 * devm_peci_controller_add() to register itself with the PECI bus.
 *
 * Return: Pointer to the newly allocated controller or ERR_PTR() in case of failure.
 */
struct peci_controller *devm_peci_controller_add(struct device *dev,
						 const struct peci_controller_ops *ops)
{
	struct peci_controller *controller;
	int ret;

	controller = peci_controller_alloc(dev, ops);
	if (IS_ERR(controller))
		return controller;

	ret = dev_set_name(&controller->dev, "peci-%d", controller->id);
	if (ret)
		goto err_put;

	pm_runtime_no_callbacks(&controller->dev);
	pm_suspend_ignore_children(&controller->dev, true);
	pm_runtime_enable(&controller->dev);

	device_set_node(&controller->dev, fwnode_handle_get(dev_fwnode(dev)));

	ret = device_add(&controller->dev);
	if (ret)
		goto err_fwnode;

	ret = devm_add_action_or_reset(dev, unregister_controller, controller);
	if (ret)
		return ERR_PTR(ret);

	/*
	 * Ignoring retval since failures during scan are non-critical for
	 * controller itself.
	 */
	peci_controller_scan_devices(controller);

	return controller;

err_fwnode:
	fwnode_handle_put(controller->dev.fwnode);

	pm_runtime_disable(&controller->dev);

err_put:
	put_device(&controller->dev);

	return ERR_PTR(ret);
}
EXPORT_SYMBOL_NS_GPL(devm_peci_controller_add, PECI);

static const struct peci_device_id *
peci_bus_match_device_id(const struct peci_device_id *id, struct peci_device *device)
{
	while (id->family != 0) {
		if (id->family == device->info.family &&
		    id->model == device->info.model)
			return id;
		id++;
	}

	return NULL;
}

static int peci_bus_device_match(struct device *dev, struct device_driver *drv)
{
	struct peci_device *device = to_peci_device(dev);
	struct peci_driver *peci_drv = to_peci_driver(drv);

	if (dev->type != &peci_device_type)
		return 0;

	return !!peci_bus_match_device_id(peci_drv->id_table, device);
}

static int peci_bus_device_probe(struct device *dev)
{
	struct peci_device *device = to_peci_device(dev);
	struct peci_driver *driver = to_peci_driver(dev->driver);

	return driver->probe(device, peci_bus_match_device_id(driver->id_table, device));
}

static void peci_bus_device_remove(struct device *dev)
{
	struct peci_device *device = to_peci_device(dev);
	struct peci_driver *driver = to_peci_driver(dev->driver);

	if (driver->remove)
		driver->remove(device);
}

const struct bus_type peci_bus_type = {
	.name		= "peci",
	.match		= peci_bus_device_match,
	.probe		= peci_bus_device_probe,
	.remove		= peci_bus_device_remove,
	.bus_groups	= peci_bus_groups,
};

static int __init peci_init(void)
{
	int ret;

	ret = bus_register(&peci_bus_type);
	if (ret < 0) {
		pr_err("peci: failed to register PECI bus type!\n");
		return ret;
	}

	return 0;
}
module_init(peci_init);

static void __exit peci_exit(void)
{
	bus_unregister(&peci_bus_type);
}
module_exit(peci_exit);

MODULE_AUTHOR("Jason M Bills <jason.m.bills@linux.intel.com>");
MODULE_AUTHOR("Jae Hyun Yoo <jae.hyun.yoo@linux.intel.com>");
MODULE_AUTHOR("Iwona Winiarska <iwona.winiarska@intel.com>");
MODULE_DESCRIPTION("PECI bus core module");
MODULE_LICENSE("GPL");
