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
#include <linux/rwsem.h>

#include "../base.h"
#include "power.h"

/*
 * The entries in the dpm_active list are in a depth first order, simply
 * because children are guaranteed to be discovered after parents, and
 * are inserted at the back of the list on discovery.
 *
 * All the other lists are kept in the same order, for consistency.
 * However the lists aren't always traversed in the same order.
 * Semaphores must be acquired from the top (i.e., front) down
 * and released in the opposite order.  Devices must be suspended
 * from the bottom (i.e., end) up and resumed in the opposite order.
 * That way no parent will be suspended while it still has an active
 * child.
 *
 * Since device_pm_add() may be called with a device semaphore held,
 * we must never try to acquire a device semaphore while holding
 * dpm_list_mutex.
 */

LIST_HEAD(dpm_active);
static LIST_HEAD(dpm_locked);
static LIST_HEAD(dpm_off);
static LIST_HEAD(dpm_off_irq);
static LIST_HEAD(dpm_destroy);

static DEFINE_MUTEX(dpm_list_mtx);

static DECLARE_RWSEM(pm_sleep_rwsem);

int (*platform_enable_wakeup)(struct device *dev, int is_on);

/**
 *	device_pm_add - add a device to the list of active devices
 *	@dev:	Device to be added to the list
 */
void device_pm_add(struct device *dev)
{
	pr_debug("PM: Adding info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	list_add_tail(&dev->power.entry, &dpm_active);
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_pm_remove - remove a device from the list of active devices
 *	@dev:	Device to be removed from the list
 *
 *	This function also removes the device's PM-related sysfs attributes.
 */
void device_pm_remove(struct device *dev)
{
	/*
	 * If this function is called during a suspend, it will be blocked,
	 * because we're holding the device's semaphore at that time, which may
	 * lead to a deadlock.  In that case we want to print a warning.
	 * However, it may also be called by unregister_dropped_devices() with
	 * the device's semaphore released, in which case the warning should
	 * not be printed.
	 */
	if (down_trylock(&dev->sem)) {
		if (down_read_trylock(&pm_sleep_rwsem)) {
			/* No suspend in progress, wait on dev->sem */
			down(&dev->sem);
			up_read(&pm_sleep_rwsem);
		} else {
			/* Suspend in progress, we may deadlock */
			dev_warn(dev, "Suspicious %s during suspend\n",
				__FUNCTION__);
			dump_stack();
			/* The user has been warned ... */
			down(&dev->sem);
		}
	}
	pr_debug("PM: Removing info for %s:%s\n",
		 dev->bus ? dev->bus->name : "No Bus",
		 kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	dpm_sysfs_remove(dev);
	list_del_init(&dev->power.entry);
	mutex_unlock(&dpm_list_mtx);
	up(&dev->sem);
}

/**
 *	device_pm_schedule_removal - schedule the removal of a suspended device
 *	@dev:	Device to destroy
 *
 *	Moves the device to the dpm_destroy list for further processing by
 *	unregister_dropped_devices().
 */
void device_pm_schedule_removal(struct device *dev)
{
	pr_debug("PM: Preparing for removal: %s:%s\n",
		dev->bus ? dev->bus->name : "No Bus",
		kobject_name(&dev->kobj));
	mutex_lock(&dpm_list_mtx);
	list_move_tail(&dev->power.entry, &dpm_destroy);
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	pm_sleep_lock - mutual exclusion for registration and suspend
 *
 *	Returns 0 if no suspend is underway and device registration
 *	may proceed, otherwise -EBUSY.
 */
int pm_sleep_lock(void)
{
	if (down_read_trylock(&pm_sleep_rwsem))
		return 0;

	return -EBUSY;
}

/**
 *	pm_sleep_unlock - mutual exclusion for registration and suspend
 *
 *	This routine undoes the effect of device_pm_add_lock
 *	when a device's registration is complete.
 */
void pm_sleep_unlock(void)
{
	up_read(&pm_sleep_rwsem);
}


/*------------------------- Resume routines -------------------------*/

/**
 *	resume_device_early - Power on one device (early resume).
 *	@dev:	Device.
 *
 *	Must be called with interrupts disabled.
 */
static int resume_device_early(struct device *dev)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

	if (dev->bus && dev->bus->resume_early) {
		dev_dbg(dev, "EARLY resume\n");
		error = dev->bus->resume_early(dev);
	}

	TRACE_RESUME(error);
	return error;
}

/**
 *	dpm_power_up - Power on all regular (non-sysdev) devices.
 *
 *	Walk the dpm_off_irq list and power each device up. This
 *	is used for devices that required they be powered down with
 *	interrupts disabled. As devices are powered on, they are moved
 *	to the dpm_off list.
 *
 *	Must be called with interrupts disabled and only one CPU running.
 */
static void dpm_power_up(void)
{

	while (!list_empty(&dpm_off_irq)) {
		struct list_head *entry = dpm_off_irq.next;
		struct device *dev = to_device(entry);

		list_move_tail(entry, &dpm_off);
		resume_device_early(dev);
	}
}

/**
 *	device_power_up - Turn on all devices that need special attention.
 *
 *	Power on system devices, then devices that required we shut them down
 *	with interrupts disabled.
 *
 *	Must be called with interrupts disabled.
 */
void device_power_up(void)
{
	sysdev_resume();
	dpm_power_up();
}
EXPORT_SYMBOL_GPL(device_power_up);

/**
 *	resume_device - Restore state for one device.
 *	@dev:	Device.
 *
 */
static int resume_device(struct device *dev)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);

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

	TRACE_RESUME(error);
	return error;
}

/**
 *	dpm_resume - Resume every device.
 *
 *	Resume the devices that have either not gone through
 *	the late suspend, or that did go through it but also
 *	went through the early resume.
 *
 *	Take devices from the dpm_off_list, resume them,
 *	and put them on the dpm_locked list.
 */
static void dpm_resume(void)
{
	mutex_lock(&dpm_list_mtx);
	while(!list_empty(&dpm_off)) {
		struct list_head *entry = dpm_off.next;
		struct device *dev = to_device(entry);

		list_move_tail(entry, &dpm_locked);
		mutex_unlock(&dpm_list_mtx);
		resume_device(dev);
		mutex_lock(&dpm_list_mtx);
	}
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	unlock_all_devices - Release each device's semaphore
 *
 *	Go through the dpm_off list.  Put each device on the dpm_active
 *	list and unlock it.
 */
static void unlock_all_devices(void)
{
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_locked)) {
		struct list_head *entry = dpm_locked.prev;
		struct device *dev = to_device(entry);

		list_move(entry, &dpm_active);
		up(&dev->sem);
	}
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	unregister_dropped_devices - Unregister devices scheduled for removal
 *
 *	Unregister all devices on the dpm_destroy list.
 */
static void unregister_dropped_devices(void)
{
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_destroy)) {
		struct list_head *entry = dpm_destroy.next;
		struct device *dev = to_device(entry);

		up(&dev->sem);
		mutex_unlock(&dpm_list_mtx);
		/* This also removes the device from the list */
		device_unregister(dev);
		mutex_lock(&dpm_list_mtx);
	}
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_resume - Restore state of each device in system.
 *
 *	Resume all the devices, unlock them all, and allow new
 *	devices to be registered once again.
 */
void device_resume(void)
{
	might_sleep();
	dpm_resume();
	unlock_all_devices();
	unregister_dropped_devices();
	up_write(&pm_sleep_rwsem);
}
EXPORT_SYMBOL_GPL(device_resume);


/*------------------------- Suspend routines -------------------------*/

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
 *	suspend_device_late - Shut down one device (late suspend).
 *	@dev:	Device.
 *	@state:	Power state device is entering.
 *
 *	This is called with interrupts off and only a single CPU running.
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
 *	device_power_down - Shut down special devices.
 *	@state:		Power state to enter.
 *
 *	Power down devices that require interrupts to be disabled
 *	and move them from the dpm_off list to the dpm_off_irq list.
 *	Then power down system devices.
 *
 *	Must be called with interrupts disabled and only one CPU running.
 */
int device_power_down(pm_message_t state)
{
	int error = 0;

	while (!list_empty(&dpm_off)) {
		struct list_head *entry = dpm_off.prev;
		struct device *dev = to_device(entry);

		list_del_init(&dev->power.entry);
		error = suspend_device_late(dev, state);
		if (error) {
			printk(KERN_ERR "Could not power down device %s: "
					"error %d\n",
					kobject_name(&dev->kobj), error);
			if (list_empty(&dev->power.entry))
				list_add(&dev->power.entry, &dpm_off);
			break;
		}
		if (list_empty(&dev->power.entry))
			list_add(&dev->power.entry, &dpm_off_irq);
	}

	if (!error)
		error = sysdev_suspend(state);
	if (error)
		dpm_power_up();
	return error;
}
EXPORT_SYMBOL_GPL(device_power_down);

/**
 *	suspend_device - Save state of one device.
 *	@dev:	Device.
 *	@state:	Power state device is entering.
 */
int suspend_device(struct device *dev, pm_message_t state)
{
	int error = 0;

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
	return error;
}

/**
 *	dpm_suspend - Suspend every device.
 *	@state:	Power state to put each device in.
 *
 *	Walk the dpm_locked list.  Suspend each device and move it
 *	to the dpm_off list.
 *
 *	(For historical reasons, if it returns -EAGAIN, that used to mean
 *	that the device would be called again with interrupts disabled.
 *	These days, we use the "suspend_late()" callback for that, so we
 *	print a warning and consider it an error).
 */
static int dpm_suspend(pm_message_t state)
{
	int error = 0;

	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_locked)) {
		struct list_head *entry = dpm_locked.prev;
		struct device *dev = to_device(entry);

		list_del_init(&dev->power.entry);
		mutex_unlock(&dpm_list_mtx);
		error = suspend_device(dev, state);
		if (error) {
			printk(KERN_ERR "Could not suspend device %s: "
					"error %d%s\n",
					kobject_name(&dev->kobj),
					error,
					(error == -EAGAIN ?
					" (please convert to suspend_late)" :
					""));
			mutex_lock(&dpm_list_mtx);
			if (list_empty(&dev->power.entry))
				list_add(&dev->power.entry, &dpm_locked);
			mutex_unlock(&dpm_list_mtx);
			break;
		}
		mutex_lock(&dpm_list_mtx);
		if (list_empty(&dev->power.entry))
			list_add(&dev->power.entry, &dpm_off);
	}
	mutex_unlock(&dpm_list_mtx);

	return error;
}

/**
 *	lock_all_devices - Acquire every device's semaphore
 *
 *	Go through the dpm_active list. Carefully lock each device's
 *	semaphore and put it in on the dpm_locked list.
 */
static void lock_all_devices(void)
{
	mutex_lock(&dpm_list_mtx);
	while (!list_empty(&dpm_active)) {
		struct list_head *entry = dpm_active.next;
		struct device *dev = to_device(entry);

		/* Required locking order is dev->sem first,
		 * then dpm_list_mutex.  Hence this awkward code.
		 */
		get_device(dev);
		mutex_unlock(&dpm_list_mtx);
		down(&dev->sem);
		mutex_lock(&dpm_list_mtx);

		if (list_empty(entry))
			up(&dev->sem);		/* Device was removed */
		else
			list_move_tail(entry, &dpm_locked);
		put_device(dev);
	}
	mutex_unlock(&dpm_list_mtx);
}

/**
 *	device_suspend - Save state and stop all devices in system.
 *
 *	Prevent new devices from being registered, then lock all devices
 *	and suspend them.
 */
int device_suspend(pm_message_t state)
{
	int error;

	might_sleep();
	down_write(&pm_sleep_rwsem);
	lock_all_devices();
	error = dpm_suspend(state);
	if (error)
		device_resume();
	return error;
}
EXPORT_SYMBOL_GPL(device_suspend);

void __suspend_report_result(const char *function, void *fn, int ret)
{
	if (ret) {
		printk(KERN_ERR "%s(): ", function);
		print_fn_descriptor_symbol("%s() returns ", (unsigned long)fn);
		printk("%d\n", ret);
	}
}
EXPORT_SYMBOL_GPL(__suspend_report_result);
