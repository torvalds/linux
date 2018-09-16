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
#include <linux/sched/signal.h>
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
 * @mode: sending mode
 *
 * Return: written size bytes or < 0 on error
 */
ssize_t __mei_cl_send(struct mei_cl *cl, u8 *buf, size_t length,
		      unsigned int mode)
{
	struct mei_device *bus;
	struct mei_cl_cb *cb;
	ssize_t rets;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	bus = cl->dev;

	mutex_lock(&bus->device_lock);
	if (bus->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

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

	while (cl->tx_cb_queued >= bus->tx_queue_limit) {
		mutex_unlock(&bus->device_lock);
		rets = wait_event_interruptible(cl->tx_wait,
				cl->writing_state == MEI_WRITE_COMPLETE ||
				(!mei_cl_is_connected(cl)));
		mutex_lock(&bus->device_lock);
		if (rets) {
			if (signal_pending(current))
				rets = -EINTR;
			goto out;
		}
		if (!mei_cl_is_connected(cl)) {
			rets = -ENODEV;
			goto out;
		}
	}

	cb = mei_cl_alloc_cb(cl, length, MEI_FOP_WRITE, NULL);
	if (!cb) {
		rets = -ENOMEM;
		goto out;
	}

	cb->internal = !!(mode & MEI_CL_IO_TX_INTERNAL);
	cb->blocking = !!(mode & MEI_CL_IO_TX_BLOCKING);
	memcpy(cb->buf.data, buf, length);

	rets = mei_cl_write(cl, cb);

out:
	mutex_unlock(&bus->device_lock);

	return rets;
}

/**
 * __mei_cl_recv - internal client receive (read)
 *
 * @cl: host client
 * @buf: buffer to receive
 * @length: buffer length
 * @mode: io mode
 * @timeout: recv timeout, 0 for infinite timeout
 *
 * Return: read size in bytes of < 0 on error
 */
ssize_t __mei_cl_recv(struct mei_cl *cl, u8 *buf, size_t length,
		      unsigned int mode, unsigned long timeout)
{
	struct mei_device *bus;
	struct mei_cl_cb *cb;
	size_t r_length;
	ssize_t rets;
	bool nonblock = !!(mode & MEI_CL_IO_RX_NONBLOCK);

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	bus = cl->dev;

	mutex_lock(&bus->device_lock);
	if (bus->dev_state != MEI_DEV_ENABLED) {
		rets = -ENODEV;
		goto out;
	}

	cb = mei_cl_read_cb(cl, NULL);
	if (cb)
		goto copy;

	rets = mei_cl_read_start(cl, length, NULL);
	if (rets && rets != -EBUSY)
		goto out;

	if (nonblock) {
		rets = -EAGAIN;
		goto out;
	}

	/* wait on event only if there is no other waiter */
	/* synchronized under device mutex */
	if (!waitqueue_active(&cl->rx_wait)) {

		mutex_unlock(&bus->device_lock);

		if (timeout) {
			rets = wait_event_interruptible_timeout
					(cl->rx_wait,
					(!list_empty(&cl->rd_completed)) ||
					(!mei_cl_is_connected(cl)),
					msecs_to_jiffies(timeout));
			if (rets == 0)
				return -ETIME;
			if (rets < 0) {
				if (signal_pending(current))
					return -EINTR;
				return -ERESTARTSYS;
			}
		} else {
			if (wait_event_interruptible
					(cl->rx_wait,
					(!list_empty(&cl->rd_completed)) ||
					(!mei_cl_is_connected(cl)))) {
				if (signal_pending(current))
					return -EINTR;
				return -ERESTARTSYS;
			}
		}

		mutex_lock(&bus->device_lock);

		if (!mei_cl_is_connected(cl)) {
			rets = -ENODEV;
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
 * mei_cldev_send - me device send  (write)
 *
 * @cldev: me client device
 * @buf: buffer to send
 * @length: buffer length
 *
 * Return: written size in bytes or < 0 on error
 */
ssize_t mei_cldev_send(struct mei_cl_device *cldev, u8 *buf, size_t length)
{
	struct mei_cl *cl = cldev->cl;

	return __mei_cl_send(cl, buf, length, MEI_CL_IO_TX_BLOCKING);
}
EXPORT_SYMBOL_GPL(mei_cldev_send);

/**
 * mei_cldev_recv_nonblock - non block client receive (read)
 *
 * @cldev: me client device
 * @buf: buffer to receive
 * @length: buffer length
 *
 * Return: read size in bytes of < 0 on error
 *         -EAGAIN if function will block.
 */
ssize_t mei_cldev_recv_nonblock(struct mei_cl_device *cldev, u8 *buf,
				size_t length)
{
	struct mei_cl *cl = cldev->cl;

	return __mei_cl_recv(cl, buf, length, MEI_CL_IO_RX_NONBLOCK, 0);
}
EXPORT_SYMBOL_GPL(mei_cldev_recv_nonblock);

/**
 * mei_cldev_recv - client receive (read)
 *
 * @cldev: me client device
 * @buf: buffer to receive
 * @length: buffer length
 *
 * Return: read size in bytes of < 0 on error
 */
ssize_t mei_cldev_recv(struct mei_cl_device *cldev, u8 *buf, size_t length)
{
	struct mei_cl *cl = cldev->cl;

	return __mei_cl_recv(cl, buf, length, 0, 0);
}
EXPORT_SYMBOL_GPL(mei_cldev_recv);

/**
 * mei_cl_bus_rx_work - dispatch rx event for a bus device
 *
 * @work: work
 */
static void mei_cl_bus_rx_work(struct work_struct *work)
{
	struct mei_cl_device *cldev;
	struct mei_device *bus;

	cldev = container_of(work, struct mei_cl_device, rx_work);

	bus = cldev->bus;

	if (cldev->rx_cb)
		cldev->rx_cb(cldev);

	mutex_lock(&bus->device_lock);
	mei_cl_read_start(cldev->cl, mei_cl_mtu(cldev->cl), NULL);
	mutex_unlock(&bus->device_lock);
}

/**
 * mei_cl_bus_notif_work - dispatch FW notif event for a bus device
 *
 * @work: work
 */
static void mei_cl_bus_notif_work(struct work_struct *work)
{
	struct mei_cl_device *cldev;

	cldev = container_of(work, struct mei_cl_device, notif_work);

	if (cldev->notif_cb)
		cldev->notif_cb(cldev);
}

/**
 * mei_cl_bus_notify_event - schedule notify cb on bus client
 *
 * @cl: host client
 *
 * Return: true if event was scheduled
 *         false if the client is not waiting for event
 */
bool mei_cl_bus_notify_event(struct mei_cl *cl)
{
	struct mei_cl_device *cldev = cl->cldev;

	if (!cldev || !cldev->notif_cb)
		return false;

	if (!cl->notify_ev)
		return false;

	schedule_work(&cldev->notif_work);

	cl->notify_ev = false;

	return true;
}

/**
 * mei_cl_bus_rx_event - schedule rx event
 *
 * @cl: host client
 *
 * Return: true if event was scheduled
 *         false if the client is not waiting for event
 */
bool mei_cl_bus_rx_event(struct mei_cl *cl)
{
	struct mei_cl_device *cldev = cl->cldev;

	if (!cldev || !cldev->rx_cb)
		return false;

	schedule_work(&cldev->rx_work);

	return true;
}

/**
 * mei_cldev_register_rx_cb - register Rx event callback
 *
 * @cldev: me client devices
 * @rx_cb: callback function
 *
 * Return: 0 on success
 *         -EALREADY if an callback is already registered
 *         <0 on other errors
 */
int mei_cldev_register_rx_cb(struct mei_cl_device *cldev, mei_cldev_cb_t rx_cb)
{
	struct mei_device *bus = cldev->bus;
	int ret;

	if (!rx_cb)
		return -EINVAL;
	if (cldev->rx_cb)
		return -EALREADY;

	cldev->rx_cb = rx_cb;
	INIT_WORK(&cldev->rx_work, mei_cl_bus_rx_work);

	mutex_lock(&bus->device_lock);
	ret = mei_cl_read_start(cldev->cl, mei_cl_mtu(cldev->cl), NULL);
	mutex_unlock(&bus->device_lock);
	if (ret && ret != -EBUSY)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(mei_cldev_register_rx_cb);

/**
 * mei_cldev_register_notif_cb - register FW notification event callback
 *
 * @cldev: me client devices
 * @notif_cb: callback function
 *
 * Return: 0 on success
 *         -EALREADY if an callback is already registered
 *         <0 on other errors
 */
int mei_cldev_register_notif_cb(struct mei_cl_device *cldev,
				mei_cldev_cb_t notif_cb)
{
	struct mei_device *bus = cldev->bus;
	int ret;

	if (!notif_cb)
		return -EINVAL;

	if (cldev->notif_cb)
		return -EALREADY;

	cldev->notif_cb = notif_cb;
	INIT_WORK(&cldev->notif_work, mei_cl_bus_notif_work);

	mutex_lock(&bus->device_lock);
	ret = mei_cl_notify_request(cldev->cl, NULL, 1);
	mutex_unlock(&bus->device_lock);
	if (ret)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(mei_cldev_register_notif_cb);

/**
 * mei_cldev_get_drvdata - driver data getter
 *
 * @cldev: mei client device
 *
 * Return: driver private data
 */
void *mei_cldev_get_drvdata(const struct mei_cl_device *cldev)
{
	return dev_get_drvdata(&cldev->dev);
}
EXPORT_SYMBOL_GPL(mei_cldev_get_drvdata);

/**
 * mei_cldev_set_drvdata - driver data setter
 *
 * @cldev: mei client device
 * @data: data to store
 */
void mei_cldev_set_drvdata(struct mei_cl_device *cldev, void *data)
{
	dev_set_drvdata(&cldev->dev, data);
}
EXPORT_SYMBOL_GPL(mei_cldev_set_drvdata);

/**
 * mei_cldev_uuid - return uuid of the underlying me client
 *
 * @cldev: mei client device
 *
 * Return: me client uuid
 */
const uuid_le *mei_cldev_uuid(const struct mei_cl_device *cldev)
{
	return mei_me_cl_uuid(cldev->me_cl);
}
EXPORT_SYMBOL_GPL(mei_cldev_uuid);

/**
 * mei_cldev_ver - return protocol version of the underlying me client
 *
 * @cldev: mei client device
 *
 * Return: me client protocol version
 */
u8 mei_cldev_ver(const struct mei_cl_device *cldev)
{
	return mei_me_cl_ver(cldev->me_cl);
}
EXPORT_SYMBOL_GPL(mei_cldev_ver);

/**
 * mei_cldev_enabled - check whether the device is enabled
 *
 * @cldev: mei client device
 *
 * Return: true if me client is initialized and connected
 */
bool mei_cldev_enabled(struct mei_cl_device *cldev)
{
	return mei_cl_is_connected(cldev->cl);
}
EXPORT_SYMBOL_GPL(mei_cldev_enabled);

/**
 * mei_cl_bus_module_get - acquire module of the underlying
 *    hw driver.
 *
 * @cldev: mei client device
 *
 * Return: true on success; false if the module was removed.
 */
static bool mei_cl_bus_module_get(struct mei_cl_device *cldev)
{
	return try_module_get(cldev->bus->dev->driver->owner);
}

/**
 * mei_cl_bus_module_put -  release the underlying hw module.
 *
 * @cldev: mei client device
 */
static void mei_cl_bus_module_put(struct mei_cl_device *cldev)
{
	module_put(cldev->bus->dev->driver->owner);
}

/**
 * mei_cldev_enable - enable me client device
 *     create connection with me client
 *
 * @cldev: me client device
 *
 * Return: 0 on success and < 0 on error
 */
int mei_cldev_enable(struct mei_cl_device *cldev)
{
	struct mei_device *bus = cldev->bus;
	struct mei_cl *cl;
	int ret;

	cl = cldev->cl;

	mutex_lock(&bus->device_lock);
	if (cl->state == MEI_FILE_UNINITIALIZED) {
		ret = mei_cl_link(cl);
		if (ret)
			goto out;
		/* update pointers */
		cl->cldev = cldev;
	}

	if (mei_cl_is_connected(cl)) {
		ret = 0;
		goto out;
	}

	if (!mei_me_cl_is_active(cldev->me_cl)) {
		dev_err(&cldev->dev, "me client is not active\n");
		ret = -ENOTTY;
		goto out;
	}

	if (!mei_cl_bus_module_get(cldev)) {
		dev_err(&cldev->dev, "get hw module failed");
		ret = -ENODEV;
		goto out;
	}

	ret = mei_cl_connect(cl, cldev->me_cl, NULL);
	if (ret < 0) {
		dev_err(&cldev->dev, "cannot connect\n");
		mei_cl_bus_module_put(cldev);
	}

out:
	mutex_unlock(&bus->device_lock);

	return ret;
}
EXPORT_SYMBOL_GPL(mei_cldev_enable);

/**
 * mei_cldev_unregister_callbacks - internal wrapper for unregistering
 *  callbacks.
 *
 * @cldev: client device
 */
static void mei_cldev_unregister_callbacks(struct mei_cl_device *cldev)
{
	if (cldev->rx_cb) {
		cancel_work_sync(&cldev->rx_work);
		cldev->rx_cb = NULL;
	}

	if (cldev->notif_cb) {
		cancel_work_sync(&cldev->notif_work);
		cldev->notif_cb = NULL;
	}
}

/**
 * mei_cldev_disable - disable me client device
 *     disconnect form the me client
 *
 * @cldev: me client device
 *
 * Return: 0 on success and < 0 on error
 */
int mei_cldev_disable(struct mei_cl_device *cldev)
{
	struct mei_device *bus;
	struct mei_cl *cl;
	int err;

	if (!cldev)
		return -ENODEV;

	cl = cldev->cl;

	bus = cldev->bus;

	mei_cldev_unregister_callbacks(cldev);

	mutex_lock(&bus->device_lock);

	if (!mei_cl_is_connected(cl)) {
		dev_dbg(bus->dev, "Already disconnected\n");
		err = 0;
		goto out;
	}

	err = mei_cl_disconnect(cl);
	if (err < 0)
		dev_err(bus->dev, "Could not disconnect from the ME client\n");

	mei_cl_bus_module_put(cldev);
out:
	/* Flush queues and remove any pending read */
	mei_cl_flush_queues(cl, NULL);
	mei_cl_unlink(cl);

	mutex_unlock(&bus->device_lock);
	return err;
}
EXPORT_SYMBOL_GPL(mei_cldev_disable);

/**
 * mei_cl_device_find - find matching entry in the driver id table
 *
 * @cldev: me client device
 * @cldrv: me client driver
 *
 * Return: id on success; NULL if no id is matching
 */
static const
struct mei_cl_device_id *mei_cl_device_find(struct mei_cl_device *cldev,
					    struct mei_cl_driver *cldrv)
{
	const struct mei_cl_device_id *id;
	const uuid_le *uuid;
	u8 version;
	bool match;

	uuid = mei_me_cl_uuid(cldev->me_cl);
	version = mei_me_cl_ver(cldev->me_cl);

	id = cldrv->id_table;
	while (uuid_le_cmp(NULL_UUID_LE, id->uuid)) {
		if (!uuid_le_cmp(*uuid, id->uuid)) {
			match = true;

			if (cldev->name[0])
				if (strncmp(cldev->name, id->name,
					    sizeof(id->name)))
					match = false;

			if (id->version != MEI_CL_VERSION_ANY)
				if (id->version != version)
					match = false;
			if (match)
				return id;
		}

		id++;
	}

	return NULL;
}

/**
 * mei_cl_device_match  - device match function
 *
 * @dev: device
 * @drv: driver
 *
 * Return:  1 if matching device was found 0 otherwise
 */
static int mei_cl_device_match(struct device *dev, struct device_driver *drv)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct mei_cl_driver *cldrv = to_mei_cl_driver(drv);
	const struct mei_cl_device_id *found_id;

	if (!cldev)
		return 0;

	if (!cldev->do_match)
		return 0;

	if (!cldrv || !cldrv->id_table)
		return 0;

	found_id = mei_cl_device_find(cldev, cldrv);
	if (found_id)
		return 1;

	return 0;
}

/**
 * mei_cl_device_probe - bus probe function
 *
 * @dev: device
 *
 * Return:  0 on success; < 0 otherwise
 */
static int mei_cl_device_probe(struct device *dev)
{
	struct mei_cl_device *cldev;
	struct mei_cl_driver *cldrv;
	const struct mei_cl_device_id *id;
	int ret;

	cldev = to_mei_cl_device(dev);
	cldrv = to_mei_cl_driver(dev->driver);

	if (!cldev)
		return 0;

	if (!cldrv || !cldrv->probe)
		return -ENODEV;

	id = mei_cl_device_find(cldev, cldrv);
	if (!id)
		return -ENODEV;

	ret = cldrv->probe(cldev, id);
	if (ret)
		return ret;

	__module_get(THIS_MODULE);
	return 0;
}

/**
 * mei_cl_device_remove - remove device from the bus
 *
 * @dev: device
 *
 * Return:  0 on success; < 0 otherwise
 */
static int mei_cl_device_remove(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	struct mei_cl_driver *cldrv;
	int ret = 0;

	if (!cldev || !dev->driver)
		return 0;

	cldrv = to_mei_cl_driver(dev->driver);
	if (cldrv->remove)
		ret = cldrv->remove(cldev);

	mei_cldev_unregister_callbacks(cldev);

	module_put(THIS_MODULE);
	dev->driver = NULL;
	return ret;

}

static ssize_t name_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);

	return scnprintf(buf, PAGE_SIZE, "%s", cldev->name);
}
static DEVICE_ATTR_RO(name);

static ssize_t uuid_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);

	return scnprintf(buf, PAGE_SIZE, "%pUl", uuid);
}
static DEVICE_ATTR_RO(uuid);

static ssize_t version_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	u8 version = mei_me_cl_ver(cldev->me_cl);

	return scnprintf(buf, PAGE_SIZE, "%02X", version);
}
static DEVICE_ATTR_RO(version);

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);
	const uuid_le *uuid = mei_me_cl_uuid(cldev->me_cl);
	u8 version = mei_me_cl_ver(cldev->me_cl);

	return scnprintf(buf, PAGE_SIZE, "mei:%s:%pUl:%02X:",
			 cldev->name, uuid, version);
}
static DEVICE_ATTR_RO(modalias);

static struct attribute *mei_cldev_attrs[] = {
	&dev_attr_name.attr,
	&dev_attr_uuid.attr,
	&dev_attr_version.attr,
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(mei_cldev);

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
	u8 version = mei_me_cl_ver(cldev->me_cl);

	if (add_uevent_var(env, "MEI_CL_VERSION=%d", version))
		return -ENOMEM;

	if (add_uevent_var(env, "MEI_CL_UUID=%pUl", uuid))
		return -ENOMEM;

	if (add_uevent_var(env, "MEI_CL_NAME=%s", cldev->name))
		return -ENOMEM;

	if (add_uevent_var(env, "MODALIAS=mei:%s:%pUl:%02X:",
			   cldev->name, uuid, version))
		return -ENOMEM;

	return 0;
}

static struct bus_type mei_cl_bus_type = {
	.name		= "mei",
	.dev_groups	= mei_cldev_groups,
	.match		= mei_cl_device_match,
	.probe		= mei_cl_device_probe,
	.remove		= mei_cl_device_remove,
	.uevent		= mei_cl_device_uevent,
};

static struct mei_device *mei_dev_bus_get(struct mei_device *bus)
{
	if (bus)
		get_device(bus->dev);

	return bus;
}

static void mei_dev_bus_put(struct mei_device *bus)
{
	if (bus)
		put_device(bus->dev);
}

static void mei_cl_bus_dev_release(struct device *dev)
{
	struct mei_cl_device *cldev = to_mei_cl_device(dev);

	if (!cldev)
		return;

	mei_me_cl_put(cldev->me_cl);
	mei_dev_bus_put(cldev->bus);
	mei_cl_unlink(cldev->cl);
	kfree(cldev->cl);
	kfree(cldev);
}

static const struct device_type mei_cl_device_type = {
	.release = mei_cl_bus_dev_release,
};

/**
 * mei_cl_bus_set_name - set device name for me client device
 *
 * @cldev: me client device
 */
static inline void mei_cl_bus_set_name(struct mei_cl_device *cldev)
{
	dev_set_name(&cldev->dev, "mei:%s:%pUl:%02X",
		     cldev->name,
		     mei_me_cl_uuid(cldev->me_cl),
		     mei_me_cl_ver(cldev->me_cl));
}

/**
 * mei_cl_bus_dev_alloc - initialize and allocate mei client device
 *
 * @bus: mei device
 * @me_cl: me client
 *
 * Return: allocated device structur or NULL on allocation failure
 */
static struct mei_cl_device *mei_cl_bus_dev_alloc(struct mei_device *bus,
						  struct mei_me_client *me_cl)
{
	struct mei_cl_device *cldev;
	struct mei_cl *cl;

	cldev = kzalloc(sizeof(struct mei_cl_device), GFP_KERNEL);
	if (!cldev)
		return NULL;

	cl = mei_cl_allocate(bus);
	if (!cl) {
		kfree(cldev);
		return NULL;
	}

	device_initialize(&cldev->dev);
	cldev->dev.parent = bus->dev;
	cldev->dev.bus    = &mei_cl_bus_type;
	cldev->dev.type   = &mei_cl_device_type;
	cldev->bus        = mei_dev_bus_get(bus);
	cldev->me_cl      = mei_me_cl_get(me_cl);
	cldev->cl         = cl;
	mei_cl_bus_set_name(cldev);
	cldev->is_added   = 0;
	INIT_LIST_HEAD(&cldev->bus_list);

	return cldev;
}

/**
 * mei_cl_dev_setup - setup me client device
 *    run fix up routines and set the device name
 *
 * @bus: mei device
 * @cldev: me client device
 *
 * Return: true if the device is eligible for enumeration
 */
static bool mei_cl_bus_dev_setup(struct mei_device *bus,
				 struct mei_cl_device *cldev)
{
	cldev->do_match = 1;
	mei_cl_bus_dev_fixup(cldev);

	/* the device name can change during fix up */
	if (cldev->do_match)
		mei_cl_bus_set_name(cldev);

	return cldev->do_match == 1;
}

/**
 * mei_cl_bus_dev_add - add me client devices
 *
 * @cldev: me client device
 *
 * Return: 0 on success; < 0 on failre
 */
static int mei_cl_bus_dev_add(struct mei_cl_device *cldev)
{
	int ret;

	dev_dbg(cldev->bus->dev, "adding %pUL:%02X\n",
		mei_me_cl_uuid(cldev->me_cl),
		mei_me_cl_ver(cldev->me_cl));
	ret = device_add(&cldev->dev);
	if (!ret)
		cldev->is_added = 1;

	return ret;
}

/**
 * mei_cl_bus_dev_stop - stop the driver
 *
 * @cldev: me client device
 */
static void mei_cl_bus_dev_stop(struct mei_cl_device *cldev)
{
	if (cldev->is_added)
		device_release_driver(&cldev->dev);
}

/**
 * mei_cl_bus_dev_destroy - destroy me client devices object
 *
 * @cldev: me client device
 *
 * Locking: called under "dev->cl_bus_lock" lock
 */
static void mei_cl_bus_dev_destroy(struct mei_cl_device *cldev)
{

	WARN_ON(!mutex_is_locked(&cldev->bus->cl_bus_lock));

	if (!cldev->is_added)
		return;

	device_del(&cldev->dev);

	list_del_init(&cldev->bus_list);

	cldev->is_added = 0;
	put_device(&cldev->dev);
}

/**
 * mei_cl_bus_remove_device - remove a devices form the bus
 *
 * @cldev: me client device
 */
static void mei_cl_bus_remove_device(struct mei_cl_device *cldev)
{
	mei_cl_bus_dev_stop(cldev);
	mei_cl_bus_dev_destroy(cldev);
}

/**
 * mei_cl_bus_remove_devices - remove all devices form the bus
 *
 * @bus: mei device
 */
void mei_cl_bus_remove_devices(struct mei_device *bus)
{
	struct mei_cl_device *cldev, *next;

	mutex_lock(&bus->cl_bus_lock);
	list_for_each_entry_safe(cldev, next, &bus->device_list, bus_list)
		mei_cl_bus_remove_device(cldev);
	mutex_unlock(&bus->cl_bus_lock);
}


/**
 * mei_cl_bus_dev_init - allocate and initializes an mei client devices
 *     based on me client
 *
 * @bus: mei device
 * @me_cl: me client
 *
 * Locking: called under "dev->cl_bus_lock" lock
 */
static void mei_cl_bus_dev_init(struct mei_device *bus,
				struct mei_me_client *me_cl)
{
	struct mei_cl_device *cldev;

	WARN_ON(!mutex_is_locked(&bus->cl_bus_lock));

	dev_dbg(bus->dev, "initializing %pUl", mei_me_cl_uuid(me_cl));

	if (me_cl->bus_added)
		return;

	cldev = mei_cl_bus_dev_alloc(bus, me_cl);
	if (!cldev)
		return;

	me_cl->bus_added = true;
	list_add_tail(&cldev->bus_list, &bus->device_list);

}

/**
 * mei_cl_bus_rescan - scan me clients list and add create
 *    devices for eligible clients
 *
 * @bus: mei device
 */
static void mei_cl_bus_rescan(struct mei_device *bus)
{
	struct mei_cl_device *cldev, *n;
	struct mei_me_client *me_cl;

	mutex_lock(&bus->cl_bus_lock);

	down_read(&bus->me_clients_rwsem);
	list_for_each_entry(me_cl, &bus->me_clients, list)
		mei_cl_bus_dev_init(bus, me_cl);
	up_read(&bus->me_clients_rwsem);

	list_for_each_entry_safe(cldev, n, &bus->device_list, bus_list) {

		if (!mei_me_cl_is_active(cldev->me_cl)) {
			mei_cl_bus_remove_device(cldev);
			continue;
		}

		if (cldev->is_added)
			continue;

		if (mei_cl_bus_dev_setup(bus, cldev))
			mei_cl_bus_dev_add(cldev);
		else {
			list_del_init(&cldev->bus_list);
			put_device(&cldev->dev);
		}
	}
	mutex_unlock(&bus->cl_bus_lock);

	dev_dbg(bus->dev, "rescan end");
}

void mei_cl_bus_rescan_work(struct work_struct *work)
{
	struct mei_device *bus =
		container_of(work, struct mei_device, bus_rescan_work);

	mei_cl_bus_rescan(bus);
}

int __mei_cldev_driver_register(struct mei_cl_driver *cldrv,
				struct module *owner)
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
EXPORT_SYMBOL_GPL(__mei_cldev_driver_register);

void mei_cldev_driver_unregister(struct mei_cl_driver *cldrv)
{
	driver_unregister(&cldrv->driver);

	pr_debug("mei: driver [%s] unregistered\n", cldrv->driver.name);
}
EXPORT_SYMBOL_GPL(mei_cldev_driver_unregister);


int __init mei_cl_bus_init(void)
{
	return bus_register(&mei_cl_bus_type);
}

void __exit mei_cl_bus_exit(void)
{
	bus_unregister(&mei_cl_bus_type);
}
