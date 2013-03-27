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
#include <linux/pci.h>
#include <linux/mei_cl_bus.h>

#include "mei_dev.h"
#include "hw-me.h"
#include "client.h"

#define to_mei_cl_driver(d) container_of(d, struct mei_cl_driver, driver)
#define to_mei_cl_device(d) container_of(d, struct mei_cl_device, dev)

static int mei_cl_device_match(struct device *dev, struct device_driver *drv)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver = to_mei_cl_driver(drv);
	const struct mei_cl_device_id *id;

	if (!device)
		return 0;

	if (!driver || !driver->id_table)
		return 0;

	id = driver->id_table;

	while (id->name[0]) {
		if (!strcmp(dev_name(dev), id->name))
			return 1;

		id++;
	}

	return 0;
}

static int mei_cl_device_probe(struct device *dev)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver;
	struct mei_cl_device_id id;

	if (!device)
		return 0;

	driver = to_mei_cl_driver(dev->driver);
	if (!driver || !driver->probe)
		return -ENODEV;

	dev_dbg(dev, "Device probe\n");

	strncpy(id.name, dev_name(dev), MEI_CL_NAME_SIZE);

	return driver->probe(device, &id);
}

static int mei_cl_device_remove(struct device *dev)
{
	struct mei_cl_device *device = to_mei_cl_device(dev);
	struct mei_cl_driver *driver;

	if (!device || !dev->driver)
		return 0;

	if (device->event_cb) {
		device->event_cb = NULL;
		cancel_work_sync(&device->event_work);
	}

	driver = to_mei_cl_driver(dev->driver);
	if (!driver->remove) {
		dev->driver = NULL;

		return 0;
	}

	return driver->remove(device);
}

static ssize_t modalias_show(struct device *dev, struct device_attribute *a,
			     char *buf)
{
	int len;

	len = snprintf(buf, PAGE_SIZE, "mei:%s\n", dev_name(dev));

	return (len >= PAGE_SIZE) ? (PAGE_SIZE - 1) : len;
}

static struct device_attribute mei_cl_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

static int mei_cl_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	if (add_uevent_var(env, "MODALIAS=mei:%s", dev_name(dev)))
		return -ENOMEM;

	return 0;
}

static struct bus_type mei_cl_bus_type = {
	.name		= "mei",
	.dev_attrs	= mei_cl_dev_attrs,
	.match		= mei_cl_device_match,
	.probe		= mei_cl_device_probe,
	.remove		= mei_cl_device_remove,
	.uevent		= mei_cl_uevent,
};

static void mei_cl_dev_release(struct device *dev)
{
	kfree(to_mei_cl_device(dev));
}

static struct device_type mei_cl_device_type = {
	.release	= mei_cl_dev_release,
};

static struct mei_cl *mei_bus_find_mei_cl_by_uuid(struct mei_device *dev,
						uuid_le uuid)
{
	struct mei_cl *cl, *next;

	list_for_each_entry_safe(cl, next, &dev->device_list, device_link) {
		if (!uuid_le_cmp(uuid, cl->device_uuid))
			return cl;
	}

	return NULL;
}
struct mei_cl_device *mei_cl_add_device(struct mei_device *dev,
				  uuid_le uuid, char *name)
{
	struct mei_cl_device *device;
	struct mei_cl *cl;
	int status;

	cl = mei_bus_find_mei_cl_by_uuid(dev, uuid);
	if (cl == NULL)
		return NULL;

	device = kzalloc(sizeof(struct mei_cl_device), GFP_KERNEL);
	if (!device)
		return NULL;

	device->cl = cl;

	device->dev.parent = &dev->pdev->dev;
	device->dev.bus = &mei_cl_bus_type;
	device->dev.type = &mei_cl_device_type;

	dev_set_name(&device->dev, "%s", name);

	status = device_register(&device->dev);
	if (status) {
		dev_err(&dev->pdev->dev, "Failed to register MEI device\n");
		kfree(device);
		return NULL;
	}

	cl->device = device;

	dev_dbg(&device->dev, "client %s registered\n", name);

	return device;
}
EXPORT_SYMBOL_GPL(mei_cl_add_device);

void mei_cl_remove_device(struct mei_cl_device *device)
{
	device_unregister(&device->dev);
}
EXPORT_SYMBOL_GPL(mei_cl_remove_device);

int __mei_cl_driver_register(struct mei_cl_driver *driver, struct module *owner)
{
	int err;

	driver->driver.name = driver->name;
	driver->driver.owner = owner;
	driver->driver.bus = &mei_cl_bus_type;

	err = driver_register(&driver->driver);
	if (err)
		return err;

	pr_debug("mei: driver [%s] registered\n", driver->driver.name);

	return 0;
}
EXPORT_SYMBOL_GPL(__mei_cl_driver_register);

void mei_cl_driver_unregister(struct mei_cl_driver *driver)
{
	driver_unregister(&driver->driver);

	pr_debug("mei: driver [%s] unregistered\n", driver->driver.name);
}
EXPORT_SYMBOL_GPL(mei_cl_driver_unregister);

int __mei_cl_send(struct mei_cl *cl, u8 *buf, size_t length)
{
	struct mei_device *dev;
	struct mei_msg_hdr mei_hdr;
	struct mei_cl_cb *cb;
	int me_cl_id, err;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	if (cl->state != MEI_FILE_CONNECTED)
		return -ENODEV;

	cb = mei_io_cb_init(cl, NULL);
	if (!cb)
		return -ENOMEM;

	err = mei_io_cb_alloc_req_buf(cb, length);
	if (err < 0) {
		mei_io_cb_free(cb);
		return err;
	}

	memcpy(cb->request_buffer.data, buf, length);
	cb->fop_type = MEI_FOP_WRITE;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	/* Check if we have an ME client device */
	me_cl_id = mei_me_cl_by_id(dev, cl->me_client_id);
	if (me_cl_id == dev->me_clients_num) {
		err = -ENODEV;
		goto out_err;
	}

	if (length > dev->me_clients[me_cl_id].props.max_msg_length) {
		err = -EINVAL;
		goto out_err;
	}

	err = mei_cl_flow_ctrl_creds(cl);
	if (err < 0)
		goto out_err;

	/* Host buffer is not ready, we queue the request */
	if (err == 0 || !dev->hbuf_is_ready) {
		cb->buf_idx = 0;
		mei_hdr.msg_complete = 0;
		cl->writing_state = MEI_WRITING;
		list_add_tail(&cb->list, &dev->write_list.list);

		mutex_unlock(&dev->device_lock);

		return length;
	}

	dev->hbuf_is_ready = false;

	/* Check for a maximum length */
	if (length > mei_hbuf_max_len(dev)) {
		mei_hdr.length = mei_hbuf_max_len(dev);
		mei_hdr.msg_complete = 0;
	} else {
		mei_hdr.length = length;
		mei_hdr.msg_complete = 1;
	}

	mei_hdr.host_addr = cl->host_client_id;
	mei_hdr.me_addr = cl->me_client_id;
	mei_hdr.reserved = 0;

	if (mei_write_message(dev, &mei_hdr, buf)) {
		err = -EIO;
		goto out_err;
	}

	cl->writing_state = MEI_WRITING;
	cb->buf_idx = mei_hdr.length;

	if (!mei_hdr.msg_complete) {
		list_add_tail(&cb->list, &dev->write_list.list);
	} else {
		if (mei_cl_flow_ctrl_reduce(cl)) {
			err = -EIO;
			goto out_err;
		}

		list_add_tail(&cb->list, &dev->write_waiting_list.list);
	}

	mutex_unlock(&dev->device_lock);

	return mei_hdr.length;

out_err:
	mutex_unlock(&dev->device_lock);
	mei_io_cb_free(cb);

	return err;
}

int __mei_cl_recv(struct mei_cl *cl, u8 *buf, size_t length)
{
	struct mei_device *dev;
	struct mei_cl_cb *cb;
	size_t r_length;
	int err;

	if (WARN_ON(!cl || !cl->dev))
		return -ENODEV;

	dev = cl->dev;

	mutex_lock(&dev->device_lock);

	if (!cl->read_cb) {
		err = mei_cl_read_start(cl);
		if (err < 0) {
			mutex_unlock(&dev->device_lock);
			return err;
		}
	}

	if (cl->reading_state != MEI_READ_COMPLETE &&
	    !waitqueue_active(&cl->rx_wait)) {
		mutex_unlock(&dev->device_lock);

		if (wait_event_interruptible(cl->rx_wait,
				(MEI_READ_COMPLETE == cl->reading_state))) {
			if (signal_pending(current))
				return -EINTR;
			return -ERESTARTSYS;
		}

		mutex_lock(&dev->device_lock);
	}

	cb = cl->read_cb;

	if (cl->reading_state != MEI_READ_COMPLETE) {
		r_length = 0;
		goto out;
	}

	r_length = min_t(size_t, length, cb->buf_idx);

	memcpy(buf, cb->response_buffer.data, r_length);

	mei_io_cb_free(cb);
	cl->reading_state = MEI_IDLE;
	cl->read_cb = NULL;

out:
	mutex_unlock(&dev->device_lock);

	return r_length;
}

int mei_cl_send(struct mei_cl_device *device, u8 *buf, size_t length)
{
	struct mei_cl *cl = device->cl;

	if (cl == NULL)
		return -ENODEV;

	if (device->ops && device->ops->send)
		return device->ops->send(device, buf, length);

	return __mei_cl_send(cl, buf, length);
}
EXPORT_SYMBOL_GPL(mei_cl_send);

int mei_cl_recv(struct mei_cl_device *device, u8 *buf, size_t length)
{
	struct mei_cl *cl =  device->cl;

	if (cl == NULL)
		return -ENODEV;

	if (device->ops && device->ops->recv)
		return device->ops->recv(device, buf, length);

	return __mei_cl_recv(cl, buf, length);
}
EXPORT_SYMBOL_GPL(mei_cl_recv);

static void mei_bus_event_work(struct work_struct *work)
{
	struct mei_cl_device *device;

	device = container_of(work, struct mei_cl_device, event_work);

	if (device->event_cb)
		device->event_cb(device, device->events, device->event_context);

	device->events = 0;

	/* Prepare for the next read */
	mei_cl_read_start(device->cl);
}

int mei_cl_register_event_cb(struct mei_cl_device *device,
			  mei_cl_event_cb_t event_cb, void *context)
{
	if (device->event_cb)
		return -EALREADY;

	device->events = 0;
	device->event_cb = event_cb;
	device->event_context = context;
	INIT_WORK(&device->event_work, mei_bus_event_work);

	mei_cl_read_start(device->cl);

	return 0;
}
EXPORT_SYMBOL_GPL(mei_cl_register_event_cb);
