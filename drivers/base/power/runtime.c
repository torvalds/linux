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
	if (!dev->power.power_state.event)
		return;
	if (!resume_device(dev))
		dev->power.power_state = PMSG_ON;
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
	mutex_lock(&dpm_mtx);
	runtime_resume(dev);
	mutex_unlock(&dpm_mtx);
}
EXPORT_SYMBOL(dpm_runtime_resume);


/**
 *	dpm_runtime_suspend - Put one device in low-power state.
 *	@dev:	Device.
 *	@state:	State to enter.
 */

int dpm_runtime_suspend(struct device * dev, pm_message_t state)
{
	int error = 0;

	mutex_lock(&dpm_mtx);
	if (dev->power.power_state.event == state.event)
		goto Done;

	if (dev->power.power_state.event)
		runtime_resume(dev);

	if (!(error = suspend_device(dev, state)))
		dev->power.power_state = state;
 Done:
	mutex_unlock(&dpm_mtx);
	return error;
}
EXPORT_SYMBOL(dpm_runtime_suspend);


#if 0
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
	mutex_lock(&dpm_mtx);
	dev->power.power_state = state;
	mutex_unlock(&dpm_mtx);
}
#endif  /*  0  */
