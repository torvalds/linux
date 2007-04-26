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
#include "power.h"

LIST_HEAD(dpm_active);
LIST_HEAD(dpm_off);
LIST_HEAD(dpm_off_irq);

DECLARE_MUTEX(dpm_sem);
DECLARE_MUTEX(dpm_list_sem);

/**
 *	device_pm_set_parent - Specify power dependency.
 *	@dev:		Device who needs power.
 *	@parent:	Device that supplies power.
 *
 *	This function is used to manually describe a power-dependency
 *	relationship. It may be used to specify a transversal relationship
 *	(where the power supplier is not the physical (or electrical)
 *	ancestor of a specific device.
 *	The effect of this is that the supplier will not be powered down
 *	before the power dependent.
 */

void device_pm_set_parent(struct device * dev, struct device * parent)
{
	put_device(dev->power.pm_parent);
	dev->power.pm_parent = get_device(parent);
}
EXPORT_SYMBOL_GPL(device_pm_set_parent);

int device_pm_add(struct device * dev)
{
	int error;

	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	down(&dpm_list_sem);
	list_add_tail(&dev->power.entry, &dpm_active);
	device_pm_set_parent(dev, dev->parent);
	if ((error = dpm_sysfs_add(dev)))
		list_del(&dev->power.entry);
	up(&dpm_list_sem);
	return error;
}

void device_pm_remove(struct device * dev)
{
	pr_debug("PM: Removing info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	down(&dpm_list_sem);
	dpm_sysfs_remove(dev);
	put_device(dev->power.pm_parent);
	list_del_init(&dev->power.entry);
	up(&dpm_list_sem);
}


