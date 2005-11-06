/*
 * suspend.c - Functions for putting devices to sleep.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include "power.h"

extern int sysdev_suspend(pm_message_t state);

/*
 * The entries in the dpm_active list are in a depth first order, simply
 * because children are guaranteed to be discovered after parents, and
 * are inserted at the back of the list on discovery.
 *
 * All list on the suspend path are done in reverse order, so we operate
 * on the leaves of the device tree (or forests, depending on how you want
 * to look at it ;) first. As nodes are removed from the back of the list,
 * they are inserted into the front of their destintation lists.
 *
 * Things are the reverse on the resume path - iterations are done in
 * forward order, and nodes are inserted at the back of their destination
 * lists. This way, the ancestors will be accessed before their descendents.
 */


/**
 *	suspend_device - Save state of one device.
 *	@dev:	Device.
 *	@state:	Power state device is entering.
 */

int suspend_device(struct device * dev, pm_message_t state)
{
	int error = 0;

	down(&dev->sem);
	if (dev->power.power_state.event) {
		dev_dbg(dev, "PM: suspend %d-->%d\n",
			dev->power.power_state.event, state.event);
	}
	if (dev->power.pm_parent
			&& dev->power.pm_parent->power.power_state.event) {
		dev_err(dev,
			"PM: suspend %d->%d, parent %s already %d\n",
			dev->power.power_state.event, state.event,
			dev->power.pm_parent->bus_id,
			dev->power.pm_parent->power.power_state.event);
	}

	dev->power.prev_state = dev->power.power_state;

	if (dev->bus && dev->bus->suspend && !dev->power.power_state.event) {
		dev_dbg(dev, "suspending\n");
		error = dev->bus->suspend(dev, state);
	}
	up(&dev->sem);
	return error;
}


/**
 *	device_suspend - Save state and stop all devices in system.
 *	@state:		Power state to put each device in.
 *
 *	Walk the dpm_active list, call ->suspend() for each device, and move
 *	it to dpm_off.
 *	Check the return value for each. If it returns 0, then we move the
 *	the device to the dpm_off list. If it returns -EAGAIN, we move it to
 *	the dpm_off_irq list. If we get a different error, try and back out.
 *
 *	If we hit a failure with any of the devices, call device_resume()
 *	above to bring the suspended devices back to life.
 *
 */

int device_suspend(pm_message_t state)
{
	int error = 0;

	down(&dpm_sem);
	down(&dpm_list_sem);
	while (!list_empty(&dpm_active) && error == 0) {
		struct list_head * entry = dpm_active.prev;
		struct device * dev = to_device(entry);

		get_device(dev);
		up(&dpm_list_sem);

		error = suspend_device(dev, state);

		down(&dpm_list_sem);

		/* Check if the device got removed */
		if (!list_empty(&dev->power.entry)) {
			/* Move it to the dpm_off or dpm_off_irq list */
			if (!error) {
				list_del(&dev->power.entry);
				list_add(&dev->power.entry, &dpm_off);
			} else if (error == -EAGAIN) {
				list_del(&dev->power.entry);
				list_add(&dev->power.entry, &dpm_off_irq);
				error = 0;
			}
		}
		if (error)
			printk(KERN_ERR "Could not suspend device %s: "
				"error %d\n", kobject_name(&dev->kobj), error);
		put_device(dev);
	}
	up(&dpm_list_sem);
	if (error) {
		/* we failed... before resuming, bring back devices from
		 * dpm_off_irq list back to main dpm_off list, we do want
		 * to call resume() on them, in case they partially suspended
		 * despite returning -EAGAIN
		 */
		while (!list_empty(&dpm_off_irq)) {
			struct list_head * entry = dpm_off_irq.next;
			list_del(entry);
			list_add(entry, &dpm_off);
		}
		dpm_resume();
	}
	up(&dpm_sem);
	return error;
}

EXPORT_SYMBOL_GPL(device_suspend);


/**
 *	device_power_down - Shut down special devices.
 *	@state:		Power state to enter.
 *
 *	Walk the dpm_off_irq list, calling ->power_down() for each device that
 *	couldn't power down the device with interrupts enabled. When we're
 *	done, power down system devices.
 */

int device_power_down(pm_message_t state)
{
	int error = 0;
	struct device * dev;

	list_for_each_entry_reverse(dev, &dpm_off_irq, power.entry) {
		if ((error = suspend_device(dev, state)))
			break;
	}
	if (error)
		goto Error;
	if ((error = sysdev_suspend(state)))
		goto Error;
 Done:
	return error;
 Error:
	printk(KERN_ERR "Could not power down device %s: "
		"error %d\n", kobject_name(&dev->kobj), error);
	dpm_power_up();
	goto Done;
}

EXPORT_SYMBOL_GPL(device_power_down);

