/*
 * drivers/base/power/runtime.c - Handling dynamic device power management.
 *
 * Copyright (c) 2003 Patrick Mochel
 * Copyright (c) 2003 Open Source Development Lab
 *
 */

#include <linux/device.h>
#include "power.h"


static void runtime_resume(struct device * dev)
{
	dev_dbg(dev, "resuming\n");
	if (!dev->power.power_state)
		return;
	if (!resume_device(dev))
		dev->power.power_state = 0;
}


/**
 *	dpm_runtime_resume - Power one device back on.
 *	@dev:	Device.
 *
 *	Bring one device back to the on state by first powering it
 *	on, then restoring state. We only operate on devices that aren't
 *	already on.
 *	FIXME: We need to handle devices that are in an unknown state.
 */

void dpm_runtime_resume(struct device * dev)
{
	down(&dpm_sem);
	runtime_resume(dev);
	up(&dpm_sem);
}


/**
 *	dpm_runtime_suspend - Put one device in low-power state.
 *	@dev:	Device.
 *	@state:	State to enter.
 */

int dpm_runtime_suspend(struct device * dev, pm_message_t state)
{
	int error = 0;

	down(&dpm_sem);
	if (dev->power.power_state == state)
		goto Done;

	if (dev->power.power_state)
		runtime_resume(dev);

	if (!(error = suspend_device(dev, state)))
		dev->power.power_state = state;
 Done:
	up(&dpm_sem);
	return error;
}


/**
 *	dpm_set_power_state - Update power_state field.
 *	@dev:	Device.
 *	@state:	Power state device is in.
 *
 *	This is an update mechanism for drivers to notify the core
 *	what power state a device is in. Device probing code may not
 *	always be able to tell, but we need accurate information to
 *	work reliably.
 */
void dpm_set_power_state(struct device * dev, pm_message_t state)
{
	down(&dpm_sem);
	dev->power.power_state = state;
	up(&dpm_sem);
}
