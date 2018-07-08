/*
 * Copyright (C) 2016-2017 Linaro Ltd., Rob Herring <robh@kernel.org>
 *
 * Based on drivers/spmi/spmi.c:
 * Copyright (c) 2012-2015, The Linux Foundation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 and
 * only version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/errno.h>
#include <linux/idr.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/serdev.h>
#include <linux/slab.h>

static bool is_registered;
static DEFINE_IDA(ctrl_ida);

static void serdev_device_release(struct device *dev)
{
	struct serdev_device *serdev = to_serdev_device(dev);
	kfree(serdev);
}

static const struct device_type serdev_device_type = {
	.release	= serdev_device_release,
};

static void serdev_ctrl_release(struct device *dev)
{
	struct serdev_controller *ctrl = to_serdev_controller(dev);
	ida_simple_remove(&ctrl_ida, ctrl->nr);
	kfree(ctrl);
}

static const struct device_type serdev_ctrl_type = {
	.release	= serdev_ctrl_release,
};

static int serdev_device_match(struct device *dev, struct device_driver *drv)
{
	/* TODO: ACPI and platform matching */
	return of_driver_match_device(dev, drv);
}

static int serdev_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	/* TODO: ACPI and platform modalias */
	return of_device_uevent_modalias(dev, env);
}

/**
 * serdev_device_add() - add a device previously constructed via serdev_device_alloc()
 * @serdev:	serdev_device to be added
 */
int serdev_device_add(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;
	struct device *parent = serdev->dev.parent;
	int err;

	dev_set_name(&serdev->dev, "%s-%d", dev_name(parent), serdev->nr);

	/* Only a single slave device is currently supported. */
	if (ctrl->serdev) {
		dev_err(&serdev->dev, "controller busy\n");
		return -EBUSY;
	}
	ctrl->serdev = serdev;

	err = device_add(&serdev->dev);
	if (err < 0) {
		dev_err(&serdev->dev, "Can't add %s, status %d\n",
			dev_name(&serdev->dev), err);
		goto err_clear_serdev;
	}

	dev_dbg(&serdev->dev, "device %s registered\n", dev_name(&serdev->dev));

	return 0;

err_clear_serdev:
	ctrl->serdev = NULL;
	return err;
}
EXPORT_SYMBOL_GPL(serdev_device_add);

/**
 * serdev_device_remove(): remove an serdev device
 * @serdev:	serdev_device to be removed
 */
void serdev_device_remove(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	device_unregister(&serdev->dev);
	ctrl->serdev = NULL;
}
EXPORT_SYMBOL_GPL(serdev_device_remove);

int serdev_device_open(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->open)
		return -EINVAL;

	return ctrl->ops->open(ctrl);
}
EXPORT_SYMBOL_GPL(serdev_device_open);

void serdev_device_close(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->close)
		return;

	ctrl->ops->close(ctrl);
}
EXPORT_SYMBOL_GPL(serdev_device_close);

void serdev_device_write_wakeup(struct serdev_device *serdev)
{
	complete(&serdev->write_comp);
}
EXPORT_SYMBOL_GPL(serdev_device_write_wakeup);

int serdev_device_write_buf(struct serdev_device *serdev,
			    const unsigned char *buf, size_t count)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->write_buf)
		return -EINVAL;

	return ctrl->ops->write_buf(ctrl, buf, count);
}
EXPORT_SYMBOL_GPL(serdev_device_write_buf);

int serdev_device_write(struct serdev_device *serdev,
			const unsigned char *buf, size_t count,
			unsigned long timeout)
{
	struct serdev_controller *ctrl = serdev->ctrl;
	int ret;

	if (!ctrl || !ctrl->ops->write_buf ||
	    (timeout && !serdev->ops->write_wakeup))
		return -EINVAL;

	mutex_lock(&serdev->write_lock);
	do {
		reinit_completion(&serdev->write_comp);

		ret = ctrl->ops->write_buf(ctrl, buf, count);
		if (ret < 0)
			break;

		buf += ret;
		count -= ret;

	} while (count &&
		 (timeout = wait_for_completion_timeout(&serdev->write_comp,
							timeout)));
	mutex_unlock(&serdev->write_lock);
	return ret < 0 ? ret : (count ? -ETIMEDOUT : 0);
}
EXPORT_SYMBOL_GPL(serdev_device_write);

void serdev_device_write_flush(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->write_flush)
		return;

	ctrl->ops->write_flush(ctrl);
}
EXPORT_SYMBOL_GPL(serdev_device_write_flush);

int serdev_device_write_room(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->write_room)
		return 0;

	return serdev->ctrl->ops->write_room(ctrl);
}
EXPORT_SYMBOL_GPL(serdev_device_write_room);

unsigned int serdev_device_set_baudrate(struct serdev_device *serdev, unsigned int speed)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->set_baudrate)
		return 0;

	return ctrl->ops->set_baudrate(ctrl, speed);

}
EXPORT_SYMBOL_GPL(serdev_device_set_baudrate);

void serdev_device_set_flow_control(struct serdev_device *serdev, bool enable)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->set_flow_control)
		return;

	ctrl->ops->set_flow_control(ctrl, enable);
}
EXPORT_SYMBOL_GPL(serdev_device_set_flow_control);

void serdev_device_wait_until_sent(struct serdev_device *serdev, long timeout)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->wait_until_sent)
		return;

	ctrl->ops->wait_until_sent(ctrl, timeout);
}
EXPORT_SYMBOL_GPL(serdev_device_wait_until_sent);

int serdev_device_get_tiocm(struct serdev_device *serdev)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->get_tiocm)
		return -ENOTSUPP;

	return ctrl->ops->get_tiocm(ctrl);
}
EXPORT_SYMBOL_GPL(serdev_device_get_tiocm);

int serdev_device_set_tiocm(struct serdev_device *serdev, int set, int clear)
{
	struct serdev_controller *ctrl = serdev->ctrl;

	if (!ctrl || !ctrl->ops->set_tiocm)
		return -ENOTSUPP;

	return ctrl->ops->set_tiocm(ctrl, set, clear);
}
EXPORT_SYMBOL_GPL(serdev_device_set_tiocm);

static int serdev_drv_probe(struct device *dev)
{
	const struct serdev_device_driver *sdrv = to_serdev_device_driver(dev->driver);

	return sdrv->probe(to_serdev_device(dev));
}

static int serdev_drv_remove(struct device *dev)
{
	const struct serdev_device_driver *sdrv = to_serdev_device_driver(dev->driver);

	sdrv->remove(to_serdev_device(dev));
	return 0;
}

static ssize_t modalias_show(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	return of_device_modalias(dev, buf, PAGE_SIZE);
}
DEVICE_ATTR_RO(modalias);

static struct attribute *serdev_device_attrs[] = {
	&dev_attr_modalias.attr,
	NULL,
};
ATTRIBUTE_GROUPS(serdev_device);

static struct bus_type serdev_bus_type = {
	.name		= "serial",
	.match		= serdev_device_match,
	.probe		= serdev_drv_probe,
	.remove		= serdev_drv_remove,
	.uevent		= serdev_uevent,
	.dev_groups	= serdev_device_groups,
};

/**
 * serdev_controller_alloc() - Allocate a new serdev device
 * @ctrl:	associated controller
 *
 * Caller is responsible for either calling serdev_device_add() to add the
 * newly allocated controller, or calling serdev_device_put() to discard it.
 */
struct serdev_device *serdev_device_alloc(struct serdev_controller *ctrl)
{
	struct serdev_device *serdev;

	serdev = kzalloc(sizeof(*serdev), GFP_KERNEL);
	if (!serdev)
		return NULL;

	serdev->ctrl = ctrl;
	device_initialize(&serdev->dev);
	serdev->dev.parent = &ctrl->dev;
	serdev->dev.bus = &serdev_bus_type;
	serdev->dev.type = &serdev_device_type;
	init_completion(&serdev->write_comp);
	mutex_init(&serdev->write_lock);
	return serdev;
}
EXPORT_SYMBOL_GPL(serdev_device_alloc);

/**
 * serdev_controller_alloc() - Allocate a new serdev controller
 * @parent:	parent device
 * @size:	size of private data
 *
 * Caller is responsible for either calling serdev_controller_add() to add the
 * newly allocated controller, or calling serdev_controller_put() to discard it.
 * The allocated private data region may be accessed via
 * serdev_controller_get_drvdata()
 */
struct serdev_controller *serdev_controller_alloc(struct device *parent,
					      size_t size)
{
	struct serdev_controller *ctrl;
	int id;

	if (WARN_ON(!parent))
		return NULL;

	ctrl = kzalloc(sizeof(*ctrl) + size, GFP_KERNEL);
	if (!ctrl)
		return NULL;

	device_initialize(&ctrl->dev);
	ctrl->dev.type = &serdev_ctrl_type;
	ctrl->dev.bus = &serdev_bus_type;
	ctrl->dev.parent = parent;
	ctrl->dev.of_node = parent->of_node;
	serdev_controller_set_drvdata(ctrl, &ctrl[1]);

	id = ida_simple_get(&ctrl_ida, 0, 0, GFP_KERNEL);
	if (id < 0) {
		dev_err(parent,
			"unable to allocate serdev controller identifier.\n");
		serdev_controller_put(ctrl);
		return NULL;
	}

	ctrl->nr = id;
	dev_set_name(&ctrl->dev, "serial%d", id);

	dev_dbg(&ctrl->dev, "allocated controller 0x%p id %d\n", ctrl, id);
	return ctrl;
}
EXPORT_SYMBOL_GPL(serdev_controller_alloc);

static int of_serdev_register_devices(struct serdev_controller *ctrl)
{
	struct device_node *node;
	struct serdev_device *serdev = NULL;
	int err;
	bool found = false;

	for_each_available_child_of_node(ctrl->dev.of_node, node) {
		if (!of_get_property(node, "compatible", NULL))
			continue;

		dev_dbg(&ctrl->dev, "adding child %pOF\n", node);

		serdev = serdev_device_alloc(ctrl);
		if (!serdev)
			continue;

		serdev->dev.of_node = node;

		err = serdev_device_add(serdev);
		if (err) {
			dev_err(&serdev->dev,
				"failure adding device. status %d\n", err);
			serdev_device_put(serdev);
		} else
			found = true;
	}
	if (!found)
		return -ENODEV;

	return 0;
}

/**
 * serdev_controller_add() - Add an serdev controller
 * @ctrl:	controller to be registered.
 *
 * Register a controller previously allocated via serdev_controller_alloc() with
 * the serdev core.
 */
int serdev_controller_add(struct serdev_controller *ctrl)
{
	int ret;

	/* Can't register until after driver model init */
	if (WARN_ON(!is_registered))
		return -EAGAIN;

	ret = device_add(&ctrl->dev);
	if (ret)
		return ret;

	ret = of_serdev_register_devices(ctrl);
	if (ret)
		goto out_dev_del;

	dev_dbg(&ctrl->dev, "serdev%d registered: dev:%p\n",
		ctrl->nr, &ctrl->dev);
	return 0;

out_dev_del:
	device_del(&ctrl->dev);
	return ret;
};
EXPORT_SYMBOL_GPL(serdev_controller_add);

/* Remove a device associated with a controller */
static int serdev_remove_device(struct device *dev, void *data)
{
	struct serdev_device *serdev = to_serdev_device(dev);
	if (dev->type == &serdev_device_type)
		serdev_device_remove(serdev);
	return 0;
}

/**
 * serdev_controller_remove(): remove an serdev controller
 * @ctrl:	controller to remove
 *
 * Remove a serdev controller.  Caller is responsible for calling
 * serdev_controller_put() to discard the allocated controller.
 */
void serdev_controller_remove(struct serdev_controller *ctrl)
{
	int dummy;

	if (!ctrl)
		return;

	dummy = device_for_each_child(&ctrl->dev, NULL,
				      serdev_remove_device);
	device_del(&ctrl->dev);
}
EXPORT_SYMBOL_GPL(serdev_controller_remove);

/**
 * serdev_driver_register() - Register client driver with serdev core
 * @sdrv:	client driver to be associated with client-device.
 *
 * This API will register the client driver with the serdev framework.
 * It is typically called from the driver's module-init function.
 */
int __serdev_device_driver_register(struct serdev_device_driver *sdrv, struct module *owner)
{
	sdrv->driver.bus = &serdev_bus_type;
	sdrv->driver.owner = owner;

	/* force drivers to async probe so I/O is possible in probe */
        sdrv->driver.probe_type = PROBE_PREFER_ASYNCHRONOUS;

	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(__serdev_device_driver_register);

static void __exit serdev_exit(void)
{
	bus_unregister(&serdev_bus_type);
	ida_destroy(&ctrl_ida);
}
module_exit(serdev_exit);

static int __init serdev_init(void)
{
	int ret;

	ret = bus_register(&serdev_bus_type);
	if (ret)
		return ret;

	is_registered = true;
	return 0;
}
/* Must be before serial drivers register */
postcore_initcall(serdev_init);

MODULE_AUTHOR("Rob Herring <robh@kernel.org>");
MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Serial attached device bus");
