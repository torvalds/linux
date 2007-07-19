/*
 * drivers/base/power/main.c - Where the driver meets power management.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 * This file is released under the GPLv2
 *
 *
 * The driver model core calls device_pm_add() when a device is registered.
 * This will intialize the embedded device_pm_info object in the device
 * and add it to the list of power-controlled devices. sysfs entries for
 * controlling device power management will also be added.
 *
 * A different set of lists than the global subsystem list are used to
 * keep track of power info because we use different lists to hold
 * devices based on what stage of the power management process they
 * are in. The power domain dependencies may also differ from the
 * ancestral dependencies that the subsystem list maintains.
 */

#include <linux/device.h>
#include <linux/mutex.h>

#include "power.h"

LIST_HEAD(dpm_active);
LIST_HEAD(dpm_off);
LIST_HEAD(dpm_off_irq);

DEFINE_MUTEX(dpm_mtx);
DEFINE_MUTEX(dpm_list_mtx);

int (*platform_enable_wakeup)(struct device *dev, int is_on);

int device_pm_add(struct device *dev)
{
	int error;

	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	list_add_tail(&dev->power.entry, &dpm_active);
	error = dpm_sysfs_add(dev);
	if (error)
		list_del(&dev->power.entry);
	mutex_unlock(&dpm_list_mtx);
	return error;
}

void device_pm_remove(struct device *dev)
{
	pr_debug("PM: Removing info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	dpm_sysfs_remove(dev);
	list_del_init(&dev->power.entry);
	mutex_unlock(&dpm_list_mtx);
}


