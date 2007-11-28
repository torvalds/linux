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
#include <linux/kallsyms.h>
#include <linux/mutex.h>
#include <linux/pm.h>
#include <linux/resume-trace.h>

#include "../base.h"
#include "power.h"

LIST_HEAD(dpm_active);
static LIST_HEAD(dpm_off);
static LIST_HEAD(dpm_off_irq);

static DEFINE_MUTEX(dpm_mtx);
static DEFINE_MUTEX(dpm_list_mtx);

int (*platform_enable_wakeup)(struct device *dev, int is_on);


void device_pm_add(struct device *dev)
{
	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	list_add_tail(&dev->power.entry, &dpm_active);
	mutex_unlock(&dpm_list_mtx);
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


/*------------------------- Resume routines -------------------------*/

/**
 *	resume_device - Restore state for one device.
 *	@dev:	Device.
 *
 */

static int resume_device(struct device * dev)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	down(&dev->sem);

	if (dev->bus && dev->bus->resume) {
		dev_dbg(dev,"resuming\n");
		error = dev->bus->resume(dev);
	}

	if (!error && dev->type && dev->type->resume) {
		dev_dbg(dev,"resuming\n");
		error = dev->type->resume(dev);
	}

	if (!error && dev->class && dev->class->resume) {
		dev_dbg(dev,"class resume\n");
		error = dev->class->resume(dev);
	}

	up(&dev->sem);

	TRACE_RESUME(error);
	return error;
}


static int resume_device_early(struct device * dev)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);
	if (dev->bus && dev->bus->resume_early) {
		dev_dbg(dev,"EARLY resume\n");
		error = dev->bus->resume_early(dev);
	}
	TRACE_RESUME(error);
	return error;
}

/*
 * Resume the devices that have either not gone through
 * the late suspend, or that did go through it but also
 * went through the early resume
 */
static void dpm_resume(void)
{
	mutex_lock(&dpm_list_mtx);
	while(!list_empty(&dpm_off)) {
		struct list_head * entry = dpm_off.next;
		struct device * dev = to_device(entry);

		get_device(dev);
		list_move_tail(entry, &dpm_active);

		mutex_unlock(&dpm_list_mtx);
		resume_device(dev);
		mutex_lock(&dpm_list_mtx);
		put_device(dev);
	}
	mutex_unlock(&dpm_list_mtx);
}


/**
 *	device_resume - Restore state of each device in system.
 *
 *	Walk the dpm_off list, remove each entry, resume the device,
 *	then add it to the dpm_active list.
 */

void device_resume(void)
{
	might_sleep();
	mutex_lock(&dpm_mtx);
	dpm_resume();
	mutex_unlock(&dpm_mtx);
}

EXPORT_SYMBOL_GPL(device_resume);


/**
 *	dpm_power_up - Power on some devices.
 *
 *	Walk the dpm_off_irq list and power each device up. This
 *	is used for devices that required they be powered down with
 *	interrupts disabled. As devices are powered on, they are moved
 *	to the dpm_active list.
 *
 *	Interrupts must be disabled when calling this.
 */

static void dpm_power_up(void)
{
	while(!list_empty(&dpm_off_irq)) {
		struct list_head * entry = dpm_off_irq.next;
		struct device * dev = to_device(entry);

		list_move_tail(entry, &dpm_off);
		resume_device_early(dev);
	}
}


/**
 *	device_power_up - Turn on all devices that need special attention.
 *
 *	Power on system devices then devices that required we shut them down
 *	with interrupts disabled.
 *	Called with interrupts disabled.
 */

void device_power_up(void)
{
	sysdev_resume();
	dpm_power_up();
}

EXPORT_SYMBOL_GPL(device_power_up);


/*------------------------- Suspend routines -------------------------*/

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


static void
suspend_device_dbg(struct device *dev, pm_message_t state, char *info)
{
	dev_dbg(dev, "%s%s%s\n", info, suspend_verb(state.event),
		((state.event == PM_EVENT_SUSPEND) && device_may_wakeup(dev)) ?
		", may wakeup" : "");
}

/**
 *	suspend_device - Save state of one device.
 *	@dev:	Device.
 *	@state:	Power state device is entering.
 */

static int suspend_device(struct device * dev, pm_message_t state)
{
	int error = 0;

	down(&dev->sem);
	if (dev->power.power_state.event) {
		dev_dbg(dev, "PM: suspend %d-->%d\n",
			dev->power.power_state.event, state.event);
	}

	if (dev->class && dev->class->suspend) {
		suspend_device_dbg(dev, state, "class ");
		error = dev->class->suspend(dev, state);
		suspend_report_result(dev->class->suspend, error);
	}

	if (!error && dev->type && dev->type->suspend) {
		suspend_device_dbg(dev, state, "type ");
		error = dev->type->suspend(dev, state);
		suspend_report_result(dev->type->suspend, error);
	}

	if (!error && dev->bus && dev->bus->suspend) {
		suspend_device_dbg(dev, state, "");
		error = dev->bus->suspend(dev, state);
		suspend_report_result(dev->bus->suspend, error);
	}
	up(&dev->sem);
	return error;
}


/*
 * This is called with interrupts off, only a single CPU
 * running. We can't acquire a mutex or semaphore (and we don't
 * need the protection)
 */
static int suspend_device_late(struct device *dev, pm_message_t state)
{
	int error = 0;

	if (dev->bus && dev->bus->suspend_late) {
		suspend_device_dbg(dev, state, "LATE ");
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
	mutex_lock(&dpm_mtx);
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_active) && error == 0) {
		struct list_head * entry = dpm_active.prev;
		struct device * dev = to_device(entry);

		get_device(dev);
		mutex_unlock(&dpm_list_mtx);

		error = suspend_device(dev, state);

		mutex_lock(&dpm_list_mtx);

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
	mutex_unlock(&dpm_list_mtx);
	if (error)
		dpm_resume();

	mutex_unlock(&dpm_mtx);
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
