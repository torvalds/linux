/*
 * resume.c - Functions for waking devices up.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Labs
 *
 * This file is released under the GPLv2
 *
 */

#include <linux/device.h>
#include <linux/resume-trace.h>
#include "../base.h"
#include "power.h"


/**
 *	resume_device - Restore state for one device.
 *	@dev:	Device.
 *
 */

int resume_device(struct device * dev)
{
	int error = 0;

	TRACE_DEVICE(dev);
	TRACE_RESUME(0);
	down(&dev->sem);
	if (dev->power.pm_parent
			&& dev->power.pm_parent->power.power_state.event) {
		dev_err(dev, "PM: resume from %d, parent %s still %d\n",
			dev->power.power_state.event,
			dev->power.pm_parent->bus_id,
			dev->power.pm_parent->power.power_state.event);
	}
	if (dev->bus && dev->bus->resume) {
		dev_dbg(dev,"resuming\n");
		error = dev->bus->resume(dev);
	}
	if (dev->class && dev->class->resume) {
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
void dpm_resume(void)
{
	down(&dpm_list_sem);
	while(!list_empty(&dpm_off)) {
		struct list_head * entry = dpm_off.next;
		struct device * dev = to_device(entry);

		get_device(dev);
		list_move_tail(entry, &dpm_active);

		up(&dpm_list_sem);
		if (!dev->power.prev_state.event)
			resume_device(dev);
		down(&dpm_list_sem);
		put_device(dev);
	}
	up(&dpm_list_sem);
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
	down(&dpm_sem);
	dpm_resume();
	up(&dpm_sem);
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

void dpm_power_up(void)
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


