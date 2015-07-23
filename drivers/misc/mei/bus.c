/*
 * Intel Management Engine Interface (Intel MEI) Linux driver
 * Copyright (c) 2012-2013, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 */

#include <linux/module.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/interrupt.h>
#include <linux/mei_cl_bus.h>

#include "mei_dev.h"
#include "client.h"

#define to_mei_cl_driver(d) container_of(d, struct mei_cl_driver, driver)
#define to_mei_cl_device(d) container_of(d, struct mei_cl_device, dev)

/**
 * __mei_cl_send - internal client send (write)
 *
 * @cl: host client
 * @buf: buffer to send
 * @length: buffer length
 * @blocking: wait for write completion
 *
 * Return: written size bytes or < 0 on error
 */
ssize_t __mei_cl_send(struct mei_cl *cl, u8 *buf, size_t length,
			bool blocking)
{
	struct mei_device *bus;
	struct mei_cl_cb *cb = NULL;
	ssize_t rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	bus = cl->dev;

	mutex_lock(&bus->device_lock);
	if (!mei_cl_is_connected(cl)) {
		rets = -ENODEV;
		goto out;
	}

	/* Check if we have an ME client device */
	if (!mei_me_cl_is_active(cl->me_cl)) {
		rets = -ENOTTY;
		goto out;
	}

	if (length > mei_cl_mtu(cl)) {
		rets = -EFBIG;
		goto out;
	}

	cb = mei_cl_alloc_cb(cl, length, MEI_FOP_WRITE, NULL);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	memcpy(cb->buf.data, buf, length);

	rets = mei_cl_write(cl, cb, blocking);

out:
	mutex_unlock(&bus->device_lock);
	if (rets < 0)
		mei_io_cb_free(cb);

	return rets;
}

/**
 * __mei_cl_recv - internal client receive (read)
 *
 * @cl: host client
 * @buf: buffer to send
 * @length: buffer length
 *
 * Return: read size in bytes of < 0 on error
 */
ssize_t __mei_cl_recv(struct mei_cl *cl, u8 *buf, size_t length)
{
	struct mei_device *bus;
	struct mei_cl_cb *cb;
	size_t r_length;
	ssize_t rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	bus = cl->dev;

	mutex_lock(&bus->device_lock);

	cb = mei_cl_read_cb(cl, NULL);
	if (cb)
		goto copy;

	rets = mei_cl_read_start(cl, length, NULL);
	if (rets && rets != -EBUSY)
		goto out;

	/* wait on event only if there is no other waiter */
	if (list_empty(&cl->rd_completed) && !waitqueue_active(&cl->rx_wait)) {

		mutex_unlock(&bus->device_lock);

		if (wait_event_interruptible(cl->rx_wait,
				(!list_empty(&cl->rd_completed)) ||
				(!mei_cl_is_connected(cl)))) {

			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}

		mutex_lock(&bus->device_lock);

		if (!mei_cl_is_connected(cl)) {
			rets = -EBUSY;
			goto out;
		}
	}

	cb = mei_cl_read_cb(cl, NULL);
	if (!cb) {
		rets = 0;
		goto out;
	}

copy:
	if (cb->status) {
		rets = cb->status;
		goto free;
	}

	r_length = min_t(size_t, length, cb->buf_idx);
	memcpy(buf, cb->buf.data, r_length);
	rets = r_length;

free:
	mei_io_cb_free(cb);
out:
	mutex_unlock(&bus->device_lock);

	return rets;
}

/**
 * mei_cl_send - me device send  (write)
 *
 * @cldev: me client device
 * @buf: buffer to send
 * @length: buffer length
 *
 * Return: written size in bytes or < 0 on error
 */
ssize_t mei_cl_send(struct mei_cl_device *cldev, u8 *buf, size_t length)
{
	struct mei_cl *cl = cldev->cl;

	if (cl == NULL)
		return -ENODEV;

	return __mei_cl_send(cl, buf, length, 1);
}
EXPORT_SYMBOL_GPL(mei_cl_send);

/**
 * mei_cl_recv - client receive (read)
 *
 * @cldev: me client device
 * @buf: buffer to send
 * @length: buffer length
 *
 * Return: read size in bytes of < 0 on error
 */
ssize_t mei_cl_recv(struct mei_cl_device *cldev, u8 *buf, size_t length)
{
	struct mei_cl *cl = cldev->cl;

	if (cl == NULL)
		return -ENODEV;

	return __mei_cl_recv(cl, buf, length);
}
EXPORT_SYMBOL_GPL(mei_cl_recv);

/**
 * mei_bus_event_work  - dispatch rx event for a bus device
 *    and schedule new work
 *
 * @work: work
 */
static void mei_bus_event_work(struct work_struct *work)
{
	struct mei_cl_device *cldev;

	cldev = container_of(work, struct mei_cl_device, event_work);

	if (cldev->event_cb)
		cldev->event_cb(cldev, cldev->events, cldev->event_context);

	cldev->events = 0;

	/* Prepare for the next read */
	mei_cl_read_start(cldev->cl, 0, NULL);
}

/**
 * mei_cl_bus_rx_event  - schedule rx evenet
 *
 * @cl: host client
 */
void mei_cl_bus_rx_event(struct mei_cl *cl)
{
	struct mei_cl_device *cldev = cl->cldev;

	if (!cldev || !cldev->event_cb)
		return;

	set_bit(MEI_CL_EVENT_RX, &cldev->events);

	schedule_work(&cldev->event_work);
}

/**
 * mei_cl_register_event_cb - register event callback
 *
 * @cldev: me client devices
 * @event_cb: callback function
 * @context: driver context data
 *
 * Return: 0 on success
 *         -EALREADY if an callback is already registered
 *         <0 on other errors
 */
int mei_cl_register_event_cb(struct mei_cl_device *cldev,
			  mei_cl_event_cb_t event_cb, void *context)
{
	if (cldev->event_cb)
		return -EALREADY;

	cldev->events = 0;
	cldev->event_cb = event_cb;
	cldev->event_context = context;
	INIT_WORK(&cldev->event_work, mei_bus_event_work);

	mei_cl_read_start(cldev->cl, 0, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(mei_cl_register_event_cb);

/**
 * mei_cl_get_drvdata - driver data getter
 *
 * @cldev: mei client device
 *
 * Return: driver private data
 */
void *mei_cl_get_drvdata(const struct mei_cl_device *cldev)
{
	return dev_get_drvdata(&cldev->dev);
}
EXPORT_SYMBOL_GPL(mei_cl_get_drvdata);

/**
 * mei_cl_set_drvdata - driver data setter
 *
 * @cldev: mei client device
 * @data: data to store
 */
void mei_cl_set_drvdata(struct mei_cl_device *cldev, void *data)
{
	dev_set_drvdata(&cldev->dev, data);
}
EXPORT_SYMBOL_GPL(mei_cl_set_drvdata);

/**
 * mei_cl_enable_device - enable me client device
 *     create connection with me client
 *
 * @cldev: me client device
 *
 * Return: 0 on success and < 0 on error
 */
int mei_cl_enable_device(struct mei_cl_device *cldev)
{
	int err;
	struct mei_device *bus;
	struct mei_cl *cl = cldev->cl;

	if (cl == NULL)
		return -ENODEV;

	bus = cl->dev;

	mutex_lock(&bus->device_lock);

	if (mei_cl_is_connected(cl)) {
		mutex_unlock(&bus->device_lock);
		dev_warn(bus->dev, "Already connected");
		return -EBUSY;
	}

	err = mei_cl_connect(cl, cldev->me_cl, NULL);
	if (err < 0) {
		mutex_unlock(&bus->device_lock);
		dev_err(bus->dev, "Could not connect to the ME client");

		return err;
	}

	mutex_unlock(&bus->device_lock);

	if (cldev->event_cb)
		mei_cl_read_start(cldev->cl, 0, NULL);

	return 0;
}
EXPORT_SYMBOL_GPL(mei_cl_enable_device);

/**
 * mei_cl_disable_device - disable me client device
 *     disconnect form the me client
 *
 * @cldev: me client device
 *
 * Return: 0 on success and < 0 on error
 */
int mei_cl_disable_device(struct mei_cl_device *cldev)
{
	int err;
	struct mei_device *bus;
	struct mei_cl *cl = cldev->cl;

	if (cl == NULL)
		return -ENODEV;

	bus = cl->dev;

	cldev->event_cb = NULL;

	mutex_lock(&bus->device_lock);

	if (!mei_cl_is_connected(cl)) {
		dev_err(bus->dev, "Already disconnected");
		err = 0;
		goto out;
	}

	err = mei_cl_disconnect(cl);
	if (err < 0) {
		dev_err(bus->dev, "Could not disconnect from the ME client");
		goto out;
	}

	/* Flush queues and remove any pending read */
	mei_cl_flush_queues(cl, NULL);

out:
	mutex_unlock(&bus->device_lock);
	return err;

}
EXPORT_SYMBOL_GPL(mei_cl_disable_device);

static int mei_cl_device_match(struct device *dev, struct device_driver *drv)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct mei_cl_driver *cldrv = to_mei_cl_driver(drv);
	const struct mei_cl_device_id *id;
	const uuid_le *uuid;
	const char *name;

	if (!cldev)
		return 0;

	uuid = mei_me_cl_uuid(cldev->me_cl);
	name = cldev->name;

	if (!cldrv || !cldrv->id_table)
		return 0;

	id = cldrv->id_table;

	while (uuid_le_cmp(NULL_UUID_LE, id->uuid)) {

		if (!uuid_le_cmp(*uuid, id->uuid)) {
			if (id->name[0]) {
				if (!strncmp(name, id->name, sizeof(id->name)))
					return 1;
			} else {
				return 1;
			}
		}

		id++;
	}

	return 0;
}

static int mei_cl_device_probe(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct mei_cl_driver *cldrv;
	struct mei_cl_device_id id;

	if (!cldev)
		return 0;

	cldrv = to_mei_cl_driver(dev->driver);
	if (!cldrv || !cldrv->probe)
		return -ENODEV;

	dev_dbg(dev, "Device probe\n");

	strlcpy(id.name, cldev->name, sizeof(id.name));

	return cldrv->probe(cldev, &id);
}

static int mei_cl_device_remove(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct mei_cl_driver *cldrv;

	if (!cldev || !dev->driver)
		return 0;

	if (cldev->event_cb) {
		cldev->event_cb = NULL;
		cancel_work_sync(&cldev->event_work);
	}

	cldrv = to_mei_cl_driver(dev->driver);
	if (!cldrv->remove) {
		dev->driver = NULL;

		return 0;
	}

	return cldrv->remove(cldev);
}

static ssize_t name_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	size_t len;

	len = snprintf(buf, PAGE_SIZE, "%s", cldev->name);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(name);

static ssize_t uuid_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);
	size_t len;

	len = snprintf(buf, PAGE_SIZE, "%pUl", uuid);

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(uuid);

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);
	size_t len;

	len = snprintf(buf, PAGE_SIZE, "mei:%s:" MEI_CL_UUID_FMT ":",
		cldev->name, MEI_CL_UUID_ARGS(uuid->b));

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *mei_cl_dev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_uuid.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mei_cl_dev);

/**
 * mei_cl_device_uevent - me client bus uevent handler
 *
 * @dev: device
 * @env: uevent kobject
 *
 * Return: 0 on success -ENOMEM on when add_uevent_var fails
 */
static int mei_cl_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);

	if (add_uevent_var(env, "MEI_CL_UUID=%pUl", uuid))
		return -ENOMEM;

	if (add_uevent_var(env, "MEI_CL_NAME=%s", cldev->name))
		return -ENOMEM;

	if (add_uevent_var(env, "MODALIAS=mei:%s:" MEI_CL_UUID_FMT ":",
		cldev->name, MEI_CL_UUID_ARGS(uuid->b)))
		return -ENOMEM;

	return 0;
}

static struct bus_type mei_cl_bus_type = {
	.name		= "mei",
	.dev_groups	= mei_cl_dev_groups,
	.match		= mei_cl_device_match,
	.probe		= mei_cl_device_probe,
	.remove		= mei_cl_device_remove,
	.uevent		= mei_cl_device_uevent,
};

static void mei_cl_dev_release(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);

	if (!cldev)
		return;

	mei_me_cl_put(cldev->me_cl);
	kfree(cldev);
}

static struct device_type mei_cl_device_type = {
	.release	= mei_cl_dev_release,
};

struct mei_cl *mei_cl_bus_find_cl_by_uuid(struct mei_device *bus,
					 uuid_le uuid)
{
	struct mei_cl *cl;

	list_for_each_entry(cl, &bus->device_list, device_link) {
		if (cl->cldev && cl->cldev->me_cl &&
		    !uuid_le_cmp(uuid, *mei_me_cl_uuid(cl->cldev->me_cl)))
			return cl;
	}

	return NULL;
}

struct mei_cl_device *mei_cl_add_device(struct mei_device *bus,
					struct mei_me_client *me_cl,
					struct mei_cl *cl,
					char *name)
{
	struct mei_cl_device *cldev;
	int status;

	cldev = kzalloc(sizeof(struct mei_cl_device), GFP_KERNEL);
	if (!cldev)
		return NULL;

	cldev->me_cl = mei_me_cl_get(me_cl);
	if (!cldev->me_cl) {
		kfree(cldev);
		return NULL;
	}

	cldev->cl = cl;
	cldev->dev.parent = bus->dev;
	cldev->dev.bus = &mei_cl_bus_type;
	cldev->dev.type = &mei_cl_device_type;

	strlcpy(cldev->name, name, sizeof(cldev->name));

	dev_set_name(&cldev->dev, "mei:%s:%pUl", name, mei_me_cl_uuid(me_cl));

	status = device_register(&cldev->dev);
	if (status) {
		dev_err(bus->dev, "Failed to register MEI device\n");
		mei_me_cl_put(cldev->me_cl);
		kfree(cldev);
		return NULL;
	}

	cl->cldev = cldev;

	dev_dbg(&cldev->dev, "client %s registered\n", name);

	return cldev;
}
EXPORT_SYMBOL_GPL(mei_cl_add_device);

void mei_cl_remove_device(struct mei_cl_device *cldev)
{
	device_unregister(&cldev->dev);
}
EXPORT_SYMBOL_GPL(mei_cl_remove_device);

int __mei_cl_driver_register(struct mei_cl_driver *cldrv, struct module *owner)
{
	int err;

	cldrv->driver.name = cldrv->name;
	cldrv->driver.owner = owner;
	cldrv->driver.bus = &mei_cl_bus_type;

	err = driver_register(&cldrv->driver);
	if (err)
		return err;

	pr_debug("mei: driver [%s] registered\n", cldrv->driver.name);

	return 0;
}
EXPORT_SYMBOL_GPL(__mei_cl_driver_register);

void mei_cl_driver_unregister(struct mei_cl_driver *cldrv)
{
	driver_unregister(&cldrv->driver);

	pr_debug("mei: driver [%s] unregistered\n", cldrv->driver.name);
}
EXPORT_SYMBOL_GPL(mei_cl_driver_unregister);

int __init mei_cl_bus_init(void)
{
	return bus_register(&mei_cl_bus_type);
}

void __exit mei_cl_bus_exit(void)
{
	bus_unregister(&mei_cl_bus_type);
}
