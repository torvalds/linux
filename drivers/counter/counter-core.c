// SPDX-License-Identifier: GPL-2.0
/*
 * Generic Counter interface
 * Copyright (C) 2020 William Breathitt Gray
 */
#include <linux/cdev.h>
#include <linux/counter.h>
#include <linux/device.h>
#include <linux/device/bus.h>
#include <linux/export.h>
#include <linux/fs.h>
#include <linux/gfp.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/kdev_t.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/types.h>
#include <linux/wait.h>

#include "counter-chrdev.h"
#include "counter-sysfs.h"

/* Provides a unique ID for each counter device */
static DEFINE_IDA(counter_ida);

static void counter_device_release(struct device *dev)
{
	struct counter_device *const counter = dev_get_drvdata(dev);

	counter_chrdev_remove(counter);
	ida_free(&counter_ida, dev->id);
}

static struct device_type counter_device_type = {
	.name = "counter_device",
	.release = counter_device_release,
};

static struct bus_type counter_bus_type = {
	.name = "counter",
	.dev_name = "counter",
};

static dev_t counter_devt;

/**
 * counter_register - register Counter to the system
 * @counter:	pointer to Counter to register
 *
 * This function registers a Counter to the system. A sysfs "counter" directory
 * will be created and populated with sysfs attributes correlating with the
 * Counter Signals, Synapses, and Counts respectively.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int counter_register(struct counter_device *const counter)
{
	struct device *const dev = &counter->dev;
	int id;
	int err;

	/* Acquire unique ID */
	id = ida_alloc(&counter_ida, GFP_KERNEL);
	if (id < 0)
		return id;

	mutex_init(&counter->ops_exist_lock);

	/* Configure device structure for Counter */
	dev->id = id;
	dev->type = &counter_device_type;
	dev->bus = &counter_bus_type;
	dev->devt = MKDEV(MAJOR(counter_devt), id);
	if (counter->parent) {
		dev->parent = counter->parent;
		dev->of_node = counter->parent->of_node;
	}
	device_initialize(dev);
	dev_set_drvdata(dev, counter);

	err = counter_sysfs_add(counter);
	if (err < 0)
		goto err_free_id;

	err = counter_chrdev_add(counter);
	if (err < 0)
		goto err_free_id;

	err = cdev_device_add(&counter->chrdev, dev);
	if (err < 0)
		goto err_remove_chrdev;

	return 0;

err_remove_chrdev:
	counter_chrdev_remove(counter);
err_free_id:
	put_device(dev);
	return err;
}
EXPORT_SYMBOL_GPL(counter_register);

/**
 * counter_unregister - unregister Counter from the system
 * @counter:	pointer to Counter to unregister
 *
 * The Counter is unregistered from the system.
 */
void counter_unregister(struct counter_device *const counter)
{
	if (!counter)
		return;

	cdev_device_del(&counter->chrdev, &counter->dev);

	mutex_lock(&counter->ops_exist_lock);

	counter->ops = NULL;
	wake_up(&counter->events_wait);

	mutex_unlock(&counter->ops_exist_lock);

	put_device(&counter->dev);
}
EXPORT_SYMBOL_GPL(counter_unregister);

static void devm_counter_release(void *counter)
{
	counter_unregister(counter);
}

/**
 * devm_counter_register - Resource-managed counter_register
 * @dev:	device to allocate counter_device for
 * @counter:	pointer to Counter to register
 *
 * Managed counter_register. The Counter registered with this function is
 * automatically unregistered on driver detach. This function calls
 * counter_register internally. Refer to that function for more information.
 *
 * RETURNS:
 * 0 on success, negative error number on failure.
 */
int devm_counter_register(struct device *dev,
			  struct counter_device *const counter)
{
	int err;

	err = counter_register(counter);
	if (err < 0)
		return err;

	return devm_add_action_or_reset(dev, devm_counter_release, counter);
}
EXPORT_SYMBOL_GPL(devm_counter_register);

#define COUNTER_DEV_MAX 256

static int __init counter_init(void)
{
	int err;

	err = bus_register(&counter_bus_type);
	if (err < 0)
		return err;

	err = alloc_chrdev_region(&counter_devt, 0, COUNTER_DEV_MAX, "counter");
	if (err < 0)
		goto err_unregister_bus;

	return 0;

err_unregister_bus:
	bus_unregister(&counter_bus_type);
	return err;
}

static void __exit counter_exit(void)
{
	unregister_chrdev_region(counter_devt, COUNTER_DEV_MAX);
	bus_unregister(&counter_bus_type);
}

subsys_initcall(counter_init);
module_exit(counter_exit);

MODULE_AUTHOR("William Breathitt Gray <vilhelm.gray@gmail.com>");
MODULE_DESCRIPTION("Generic Counter interface");
MODULE_LICENSE("GPL v2");
