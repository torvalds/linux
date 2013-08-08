/*
 * Copyright (C) 2013-2015 Freescale Semiconductor, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <linux/module.h>
#include <linux/thermal.h>
#include <linux/err.h>
#include <linux/slab.h>

struct devfreq_cooling_device {
	int id;
	struct thermal_cooling_device *cool_dev;
	unsigned int devfreq_state;
};

static DEFINE_IDR(devfreq_idr);
static DEFINE_MUTEX(devfreq_cooling_lock);

#define	MAX_STATE	1

static BLOCKING_NOTIFIER_HEAD(devfreq_cooling_chain_head);

int register_devfreq_cooling_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(
		&devfreq_cooling_chain_head, nb);
}
EXPORT_SYMBOL_GPL(register_devfreq_cooling_notifier);

int unregister_devfreq_cooling_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_unregister(
		&devfreq_cooling_chain_head, nb);
}
EXPORT_SYMBOL_GPL(unregister_devfreq_cooling_notifier);

static int devfreq_cooling_notifier_call_chain(unsigned long val)
{
	return (blocking_notifier_call_chain(
		&devfreq_cooling_chain_head, val, NULL)
		== NOTIFY_BAD) ? -EINVAL : 0;
}

static int devfreq_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct devfreq_cooling_device *devfreq_device = cdev->devdata;
	int ret;

	ret = devfreq_cooling_notifier_call_chain(state);
	if (ret)
		return -EINVAL;
	devfreq_device->devfreq_state = state;

	return 0;
}

static int devfreq_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = MAX_STATE;

	return 0;
}

static int devfreq_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct devfreq_cooling_device *devfreq_device = cdev->devdata;

	*state = devfreq_device->devfreq_state;

	return 0;
}

static struct thermal_cooling_device_ops const devfreq_cooling_ops = {
	.get_max_state = devfreq_get_max_state,
	.get_cur_state = devfreq_get_cur_state,
	.set_cur_state = devfreq_set_cur_state,
};

static int get_idr(struct idr *idr, int *id)
{
	int ret;

	mutex_lock(&devfreq_cooling_lock);
	ret = idr_alloc(idr, NULL, 0, 0, GFP_KERNEL);
	mutex_unlock(&devfreq_cooling_lock);
	if (unlikely(ret < 0))
		return ret;
	*id = ret;

	return 0;
}

static void release_idr(struct idr *idr, int id)
{
	mutex_lock(&devfreq_cooling_lock);
	idr_remove(idr, id);
	mutex_unlock(&devfreq_cooling_lock);
}

struct thermal_cooling_device *devfreq_cooling_register(void)
{
	struct thermal_cooling_device *cool_dev;
	struct devfreq_cooling_device *devfreq_dev = NULL;
	char dev_name[THERMAL_NAME_LENGTH];
	int ret = 0;

	devfreq_dev = kzalloc(sizeof(struct devfreq_cooling_device),
			      GFP_KERNEL);
	if (!devfreq_dev)
		return ERR_PTR(-ENOMEM);

	ret = get_idr(&devfreq_idr, &devfreq_dev->id);
	if (ret) {
		kfree(devfreq_dev);
		return ERR_PTR(-EINVAL);
	}

	snprintf(dev_name, sizeof(dev_name), "thermal-devfreq-%d",
		 devfreq_dev->id);

	cool_dev = thermal_cooling_device_register(dev_name, devfreq_dev,
						   &devfreq_cooling_ops);
	if (!cool_dev) {
		release_idr(&devfreq_idr, devfreq_dev->id);
		kfree(devfreq_dev);
		return ERR_PTR(-EINVAL);
	}
	devfreq_dev->cool_dev = cool_dev;
	devfreq_dev->devfreq_state = 0;

	return cool_dev;
}
EXPORT_SYMBOL_GPL(devfreq_cooling_register);

void devfreq_cooling_unregister(struct thermal_cooling_device *cdev)
{
	struct devfreq_cooling_device *devfreq_dev = cdev->devdata;

	thermal_cooling_device_unregister(devfreq_dev->cool_dev);
	release_idr(&devfreq_idr, devfreq_dev->id);
	kfree(devfreq_dev);
}
EXPORT_SYMBOL_GPL(devfreq_cooling_unregister);
