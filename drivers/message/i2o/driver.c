/*
 *	Functions to handle I2O drivers (OSMs) and I2O bus type for sysfs
 *
 *	Copyright (C) 2004	Markus Lidel <Markus.Lidel@shadowconnect.com>
 *
 *	This program is free software; you can redistribute it and/or modify it
 *	under the terms of the GNU General Public License as published by the
 *	Free Software Foundation; either version 2 of the License, or (at your
 *	option) any later version.
 *
 *	Fixes/additions:
 *		Markus Lidel <Markus.Lidel@shadowconnect.com>
 *			initial version.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/rwsem.h>
#include <linux/i2o.h>
#include <linux/workqueue.h>
#include <linux/string.h>
#include <linux/slab.h>
#include "core.h"

#define OSM_NAME	"i2o"

/* max_drivers - Maximum I2O drivers (OSMs) which could be registered */
static unsigned int i2o_max_drivers = I2O_MAX_DRIVERS;
module_param_named(max_drivers, i2o_max_drivers, uint, 0);
MODULE_PARM_DESC(max_drivers, "maximum number of OSM's to support");

/* I2O drivers lock and array */
static spinlock_t i2o_drivers_lock;
static struct i2o_driver **i2o_drivers;

/**
 *	i2o_bus_match - Tell if I2O device class id matches the class ids of the I2O driver (OSM)
 *	@dev: device which should be verified
 *	@drv: the driver to match against
 *
 *	Used by the bus to check if the driver wants to handle the device.
 *
 *	Returns 1 if the class ids of the driver match the class id of the
 *	device, otherwise 0.
 */
static int i2o_bus_match(struct device *dev, struct device_driver *drv)
{
	struct i2o_device *i2o_dev = to_i2o_device(dev);
	struct i2o_driver *i2o_drv = to_i2o_driver(drv);
	struct i2o_class_id *ids = i2o_drv->classes;

	if (ids)
		while (ids->class_id != I2O_CLASS_END) {
			if (ids->class_id == i2o_dev->lct_data.class_id)
				return 1;
			ids++;
		}
	return 0;
};

/* I2O bus type */
struct bus_type i2o_bus_type = {
	.name = "i2o",
	.match = i2o_bus_match,
	.dev_attrs = i2o_device_attrs
};

/**
 *	i2o_driver_register - Register a I2O driver (OSM) in the I2O core
 *	@drv: I2O driver which should be registered
 *
 *	Registers the OSM drv in the I2O core and creates an event queues if
 *	necessary.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int i2o_driver_register(struct i2o_driver *drv)
{
	struct i2o_controller *c;
	int i;
	int rc = 0;
	unsigned long flags;

	osm_debug("Register driver %s\n", drv->name);

	if (drv->event) {
		drv->event_queue = create_workqueue(drv->name);
		if (!drv->event_queue) {
			osm_err("Could not initialize event queue for driver "
				"%s\n", drv->name);
			return -EFAULT;
		}
		osm_debug("Event queue initialized for driver %s\n", drv->name);
	} else
		drv->event_queue = NULL;

	drv->driver.name = drv->name;
	drv->driver.bus = &i2o_bus_type;

	spin_lock_irqsave(&i2o_drivers_lock, flags);

	for (i = 0; i2o_drivers[i]; i++)
		if (i >= i2o_max_drivers) {
			osm_err("too many drivers registered, increase "
				"max_drivers\n");
			spin_unlock_irqrestore(&i2o_drivers_lock, flags);
			return -EFAULT;
		}

	drv->context = i;
	i2o_drivers[i] = drv;

	spin_unlock_irqrestore(&i2o_drivers_lock, flags);

	osm_debug("driver %s gets context id %d\n", drv->name, drv->context);

	list_for_each_entry(c, &i2o_controllers, list) {
		struct i2o_device *i2o_dev;

		i2o_driver_notify_controller_add(drv, c);
		list_for_each_entry(i2o_dev, &c->devices, list)
		    i2o_driver_notify_device_add(drv, i2o_dev);
	}

	rc = driver_register(&drv->driver);
	if (rc) {
		if (drv->event) {
			destroy_workqueue(drv->event_queue);
			drv->event_queue = NULL;
		}
	}

	return rc;
};

/**
 *	i2o_driver_unregister - Unregister a I2O driver (OSM) from the I2O core
 *	@drv: I2O driver which should be unregistered
 *
 *	Unregisters the OSM drv from the I2O core and cleanup event queues if
 *	necessary.
 */
void i2o_driver_unregister(struct i2o_driver *drv)
{
	struct i2o_controller *c;
	unsigned long flags;

	osm_debug("unregister driver %s\n", drv->name);

	driver_unregister(&drv->driver);

	list_for_each_entry(c, &i2o_controllers, list) {
		struct i2o_device *i2o_dev;

		list_for_each_entry(i2o_dev, &c->devices, list)
		    i2o_driver_notify_device_remove(drv, i2o_dev);

		i2o_driver_notify_controller_remove(drv, c);
	}

	spin_lock_irqsave(&i2o_drivers_lock, flags);
	i2o_drivers[drv->context] = NULL;
	spin_unlock_irqrestore(&i2o_drivers_lock, flags);

	if (drv->event_queue) {
		destroy_workqueue(drv->event_queue);
		drv->event_queue = NULL;
		osm_debug("event queue removed for %s\n", drv->name);
	}
};

/**
 *	i2o_driver_dispatch - dispatch an I2O reply message
 *	@c: I2O controller of the message
 *	@m: I2O message number
 *
 *	The reply is delivered to the driver from which the original message
 *	was. This function is only called from interrupt context.
 *
 *	Returns 0 on success and the message should not be flushed. Returns > 0
 *	on success and if the message should be flushed afterwords. Returns
 *	negative error code on failure (the message will be flushed too).
 */
int i2o_driver_dispatch(struct i2o_controller *c, u32 m)
{
	struct i2o_driver *drv;
	struct i2o_message *msg = i2o_msg_out_to_virt(c, m);
	u32 context = le32_to_cpu(msg->u.s.icntxt);
	unsigned long flags;

	if (unlikely(context >= i2o_max_drivers)) {
		osm_warn("%s: Spurious reply to unknown driver %d\n", c->name,
			 context);
		return -EIO;
	}

	spin_lock_irqsave(&i2o_drivers_lock, flags);
	drv = i2o_drivers[context];
	spin_unlock_irqrestore(&i2o_drivers_lock, flags);

	if (unlikely(!drv)) {
		osm_warn("%s: Spurious reply to unknown driver %d\n", c->name,
			 context);
		return -EIO;
	}

	if ((le32_to_cpu(msg->u.head[1]) >> 24) == I2O_CMD_UTIL_EVT_REGISTER) {
		struct i2o_device *dev, *tmp;
		struct i2o_event *evt;
		u16 size;
		u16 tid = le32_to_cpu(msg->u.head[1]) & 0xfff;

		osm_debug("event received from device %d\n", tid);

		if (!drv->event)
			return -EIO;

		/* cut of header from message size (in 32-bit words) */
		size = (le32_to_cpu(msg->u.head[0]) >> 16) - 5;

		evt = kzalloc(size * 4 + sizeof(*evt), GFP_ATOMIC);
		if (!evt)
			return -ENOMEM;

		evt->size = size;
		evt->tcntxt = le32_to_cpu(msg->u.s.tcntxt);
		evt->event_indicator = le32_to_cpu(msg->body[0]);
		memcpy(&evt->data, &msg->body[1], size * 4);

		list_for_each_entry_safe(dev, tmp, &c->devices, list)
		    if (dev->lct_data.tid == tid) {
			evt->i2o_dev = dev;
			break;
		}

		INIT_WORK(&evt->work, drv->event);
		queue_work(drv->event_queue, &evt->work);
		return 1;
	}

	if (unlikely(!drv->reply)) {
		osm_debug("%s: Reply to driver %s, but no reply function"
			  " defined!\n", c->name, drv->name);
		return -EIO;
	}

	return drv->reply(c, m, msg);
}

/**
 *	i2o_driver_notify_controller_add_all - Send notify of added controller
 *	@c: newly added controller
 *
 *	Send notifications to all registered drivers that a new controller was
 *	added.
 */
void i2o_driver_notify_controller_add_all(struct i2o_controller *c)
{
	int i;
	struct i2o_driver *drv;

	for (i = 0; i < i2o_max_drivers; i++) {
		drv = i2o_drivers[i];

		if (drv)
			i2o_driver_notify_controller_add(drv, c);
	}
}

/**
 *	i2o_driver_notify_controller_remove_all - Send notify of removed controller
 *	@c: controller that is being removed
 *
 *	Send notifications to all registered drivers that a controller was
 *	removed.
 */
void i2o_driver_notify_controller_remove_all(struct i2o_controller *c)
{
	int i;
	struct i2o_driver *drv;

	for (i = 0; i < i2o_max_drivers; i++) {
		drv = i2o_drivers[i];

		if (drv)
			i2o_driver_notify_controller_remove(drv, c);
	}
}

/**
 *	i2o_driver_notify_device_add_all - Send notify of added device
 *	@i2o_dev: newly added I2O device
 *
 *	Send notifications to all registered drivers that a device was added.
 */
void i2o_driver_notify_device_add_all(struct i2o_device *i2o_dev)
{
	int i;
	struct i2o_driver *drv;

	for (i = 0; i < i2o_max_drivers; i++) {
		drv = i2o_drivers[i];

		if (drv)
			i2o_driver_notify_device_add(drv, i2o_dev);
	}
}

/**
 *	i2o_driver_notify_device_remove_all - Send notify of removed device
 *	@i2o_dev: device that is being removed
 *
 *	Send notifications to all registered drivers that a device was removed.
 */
void i2o_driver_notify_device_remove_all(struct i2o_device *i2o_dev)
{
	int i;
	struct i2o_driver *drv;

	for (i = 0; i < i2o_max_drivers; i++) {
		drv = i2o_drivers[i];

		if (drv)
			i2o_driver_notify_device_remove(drv, i2o_dev);
	}
}

/**
 *	i2o_driver_init - initialize I2O drivers (OSMs)
 *
 *	Registers the I2O bus and allocate memory for the array of OSMs.
 *
 *	Returns 0 on success or negative error code on failure.
 */
int __init i2o_driver_init(void)
{
	int rc = 0;

	spin_lock_init(&i2o_drivers_lock);

	if ((i2o_max_drivers < 2) || (i2o_max_drivers > 64)) {
		osm_warn("max_drivers set to %d, but must be >=2 and <= 64\n",
			 i2o_max_drivers);
		i2o_max_drivers = I2O_MAX_DRIVERS;
	}
	osm_info("max drivers = %d\n", i2o_max_drivers);

	i2o_drivers =
	    kcalloc(i2o_max_drivers, sizeof(*i2o_drivers), GFP_KERNEL);
	if (!i2o_drivers)
		return -ENOMEM;

	rc = bus_register(&i2o_bus_type);

	if (rc < 0)
		kfree(i2o_drivers);

	return rc;
};

/**
 *	i2o_driver_exit - clean up I2O drivers (OSMs)
 *
 *	Unregisters the I2O bus and frees driver array.
 */
void i2o_driver_exit(void)
{
	bus_unregister(&i2o_bus_type);
	kfree(i2o_drivers);
};

EXPORT_SYMBOL(i2o_driver_register);
EXPORT_SYMBOL(i2o_driver_unregister);
EXPORT_SYMBOL(i2o_driver_notify_controller_add_all);
EXPORT_SYMBOL(i2o_driver_notify_controller_remove_all);
EXPORT_SYMBOL(i2o_driver_notify_device_add_all);
EXPORT_SYMBOL(i2o_driver_notify_device_remove_all);
