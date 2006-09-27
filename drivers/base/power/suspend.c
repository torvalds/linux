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
#include <linux/kallsyms.h>
#include <linux/pm.h>
#include "../base.h"
#include "power.h"

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

static inline char *suspend_verb(u32 event)
{
	switch (event) {
	case PM_EVENT_SUSPEND:	return "suspend";
	case PM_EVENT_FREEZE:	return "freeze";
	case PM_EVENT_PRETHAW:	return "prethaw";
	default:		return "(unknown suspend event)";
	}
}


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

	if (dev->class && dev->class->suspend && !dev->power.power_state.event) {
		dev_dbg(dev, "class %s%s\n",
			suspend_verb(state.event),
			((state.event == PM_EVENT_SUSPEND)
					&& device_may_wakeup(dev))
				? ", may wakeup"
				: ""
			);
		error = dev->class->suspend(dev, state);
		suspend_report_result(dev->class->suspend, error);
	}

	if (!error && dev->bus && dev->bus->suspend && !dev->power.power_state.event) {
		dev_dbg(dev, "%s%s\n",
			suspend_verb(state.event),
			((state.event == PM_EVENT_SUSPEND)
					&& device_may_wakeup(dev))
				? ", may wakeup"
				: ""
			);
		error = dev->bus->suspend(dev, state);
		suspend_report_result(dev->bus->suspend, error);
	}
	up(&dev->sem);
	return error;
}


/*
 * This is called with interrupts off, only a single CPU
 * running. We can't do down() on a semaphore (and we don't
 * need the protection)
 */
static int suspend_device_late(struct device *dev, pm_message_t state)
{
	int error = 0;

	if (dev->bus && dev->bus->suspend_late && !dev->power.power_state.event) {
		dev_dbg(dev, "LATE %s%s\n",
			suspend_verb(state.event),
			((state.event == PM_EVENT_SUSPEND)
					&& device_may_wakeup(dev))
				? ", may wakeup"
				: ""
			);
		error = dev->bus->suspend_late(dev, state);
		suspend_report_result(dev->bus->suspend_late, error);
	}
	return error;
}

/**
 *	device_suspend - Save state and stop all devices in system.
 *	@state:		Power state to put each device in.
 *
 *	Walk the dpm_active list, call ->suspend() for each device, and move
 *	it to the dpm_off list.
 *
 *	(For historical reasons, if it returns -EAGAIN, that used to mean
 *	that the device would be called again with interrupts disabled.
 *	These days, we use the "suspend_late()" callback for that, so we
 *	print a warning and consider it an error).
 *
 *	If we get a different error, try and back out.
 *
 *	If we hit a failure with any of the devices, call device_resume()
 *	above to bring the suspended devices back to life.
 *
 */

int device_suspend(pm_message_t state)
{
	int error = 0;

	might_sleep();
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
			/* Move it to the dpm_off list */
			if (!error)
				list_move(&dev->power.entry, &dpm_off);
		}
		if (error)
			printk(KERN_ERR "Could not suspend device %s: "
				"error %d%s\n",
				kobject_name(&dev->kobj), error,
				error == -EAGAIN ? " (please convert to suspend_late)" : "");
		put_device(dev);
	}
	up(&dpm_list_sem);
	if (error)
		dpm_resume();

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

	while (!list_empty(&dpm_off)) {
		struct list_head * entry = dpm_off.prev;

		dev = to_device(entry);
		error = suspend_device_late(dev, state);
		if (error)
			goto Error;
		list_move(&dev->power.entry, &dpm_off_irq);
	}

	error = sysdev_suspend(state);
 Done:
	return error;
 Error:
	printk(KERN_ERR "Could not power down device %s: "
		"error %d\n", kobject_name(&dev->kobj), error);
	dpm_power_up();
	goto Done;
}

EXPORT_SYMBOL_GPL(device_power_down);

void __suspend_report_result(const char *function, void *fn, int ret)
{
	if (ret) {
		printk(KERN_ERR "%s(): ", function);
		print_fn_descriptor_symbol("%s() returns ", (unsigned long)fn);
		printk("%d\n", ret);
	}
}
EXPORT_SYMBOL_GPL(__suspend_report_result);
