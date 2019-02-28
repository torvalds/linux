// SPDX-License-Identifier: GPL-2.0
/*
 * System Control and Management Interface (SCMI) Message Protocol bus layer
 *
 * Copyright (C) 2018 ARM Ltd.
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/types.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/device.h>

#include "common.h"

static DEFINE_IDA(scmi_bus_id);
static DEFINE_IDR(scmi_protocols);
static DEFINE_SPINLOCK(protocol_lock);

static const struct scmi_device_id *
scmi_dev_match_id(struct scmi_device *scmi_dev, struct scmi_driver *scmi_drv)
{
	const struct scmi_device_id *id = scmi_drv->id_table;

	if (!id)
		return NULL;

	for (; id->protocol_id; id++)
		if (id->protocol_id == scmi_dev->protocol_id)
			return id;

	return NULL;
}

static int scmi_dev_match(struct device *dev, struct device_driver *drv)
{
	struct scmi_driver *scmi_drv = to_scmi_driver(drv);
	struct scmi_device *scmi_dev = to_scmi_dev(dev);
	const struct scmi_device_id *id;

	id = scmi_dev_match_id(scmi_dev, scmi_drv);
	if (id)
		return 1;

	return 0;
}

static int scmi_protocol_init(int protocol_id, struct scmi_handle *handle)
{
	scmi_prot_init_fn_t fn = idr_find(&scmi_protocols, protocol_id);

	if (unlikely(!fn))
		return -EINVAL;
	return fn(handle);
}

static int scmi_dev_probe(struct device *dev)
{
	struct scmi_driver *scmi_drv = to_scmi_driver(dev->driver);
	struct scmi_device *scmi_dev = to_scmi_dev(dev);
	const struct scmi_device_id *id;
	int ret;

	id = scmi_dev_match_id(scmi_dev, scmi_drv);
	if (!id)
		return -ENODEV;

	if (!scmi_dev->handle)
		return -EPROBE_DEFER;

	ret = scmi_protocol_init(scmi_dev->protocol_id, scmi_dev->handle);
	if (ret)
		return ret;

	return scmi_drv->probe(scmi_dev);
}

static int scmi_dev_remove(struct device *dev)
{
	struct scmi_driver *scmi_drv = to_scmi_driver(dev->driver);
	struct scmi_device *scmi_dev = to_scmi_dev(dev);

	if (scmi_drv->remove)
		scmi_drv->remove(scmi_dev);

	return 0;
}

static struct bus_type scmi_bus_type = {
	.name =	"scmi_protocol",
	.match = scmi_dev_match,
	.probe = scmi_dev_probe,
	.remove = scmi_dev_remove,
};

int scmi_driver_register(struct scmi_driver *driver, struct module *owner,
			 const char *mod_name)
{
	int retval;

	driver->driver.bus = &scmi_bus_type;
	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.mod_name = mod_name;

	retval = driver_register(&driver->driver);
	if (!retval)
		pr_debug("registered new scmi driver %s\n", driver->name);

	return retval;
}
EXPORT_SYMBOL_GPL(scmi_driver_register);

void scmi_driver_unregister(struct scmi_driver *driver)
{
	driver_unregister(&driver->driver);
}
EXPORT_SYMBOL_GPL(scmi_driver_unregister);

static void scmi_device_release(struct device *dev)
{
	kfree(to_scmi_dev(dev));
}

struct scmi_device *
scmi_device_create(struct device_node *np, struct device *parent, int protocol)
{
	int id, retval;
	struct scmi_device *scmi_dev;

	scmi_dev = kzalloc(sizeof(*scmi_dev), GFP_KERNEL);
	if (!scmi_dev)
		return NULL;

	id = ida_simple_get(&scmi_bus_id, 1, 0, GFP_KERNEL);
	if (id < 0)
		goto free_mem;

	scmi_dev->id = id;
	scmi_dev->protocol_id = protocol;
	scmi_dev->dev.parent = parent;
	scmi_dev->dev.of_node = np;
	scmi_dev->dev.bus = &scmi_bus_type;
	scmi_dev->dev.release = scmi_device_release;
	dev_set_name(&scmi_dev->dev, "scmi_dev.%d", id);

	retval = device_register(&scmi_dev->dev);
	if (retval)
		goto put_dev;

	return scmi_dev;
put_dev:
	put_device(&scmi_dev->dev);
	ida_simple_remove(&scmi_bus_id, id);
free_mem:
	kfree(scmi_dev);
	return NULL;
}

void scmi_device_destroy(struct scmi_device *scmi_dev)
{
	scmi_handle_put(scmi_dev->handle);
	ida_simple_remove(&scmi_bus_id, scmi_dev->id);
	device_unregister(&scmi_dev->dev);
}

void scmi_set_handle(struct scmi_device *scmi_dev)
{
	scmi_dev->handle = scmi_handle_get(&scmi_dev->dev);
}

int scmi_protocol_register(int protocol_id, scmi_prot_init_fn_t fn)
{
	int ret;

	spin_lock(&protocol_lock);
	ret = idr_alloc(&scmi_protocols, fn, protocol_id, protocol_id + 1,
			GFP_ATOMIC);
	spin_unlock(&protocol_lock);
	if (ret != protocol_id)
		pr_err("unable to allocate SCMI idr slot, err %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_GPL(scmi_protocol_register);

void scmi_protocol_unregister(int protocol_id)
{
	spin_lock(&protocol_lock);
	idr_remove(&scmi_protocols, protocol_id);
	spin_unlock(&protocol_lock);
}
EXPORT_SYMBOL_GPL(scmi_protocol_unregister);

static int __scmi_devices_unregister(struct device *dev, void *data)
{
	struct scmi_device *scmi_dev = to_scmi_dev(dev);

	scmi_device_destroy(scmi_dev);
	return 0;
}

static void scmi_devices_unregister(void)
{
	bus_for_each_dev(&scmi_bus_type, NULL, NULL, __scmi_devices_unregister);
}

static int __init scmi_bus_init(void)
{
	int retval;

	retval = bus_register(&scmi_bus_type);
	if (retval)
		pr_err("scmi protocol bus register failed (%d)\n", retval);

	return retval;
}
subsys_initcall(scmi_bus_init);

static void __exit scmi_bus_exit(void)
{
	scmi_devices_unregister();
	bus_unregister(&scmi_bus_type);
	ida_destroy(&scmi_bus_id);
}
module_exit(scmi_bus_exit);
