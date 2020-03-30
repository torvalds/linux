// SPDX-License-Identifier: GPL-2.0
/*
 * wakeup.c - support wakeup devices
 * Copyright (C) 2004 Li Shaohua <shaohua.li@intel.com>
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/kernel.h>
#include <linux/types.h>

#include "internal.h"
#include "sleep.h"

/*
 * We didn't lock acpi_device_lock in the file, because it invokes oops in
 * suspend/resume and isn't really required as this is called in S-state. At
 * that time, there is no device hotplug
 **/
#define _COMPONENT		ACPI_SYSTEM_COMPONENT
ACPI_MODULE_NAME("wakeup_devices")

/**
 * acpi_enable_wakeup_devices - Enable wake-up device GPEs.
 * @sleep_state: ACPI system sleep state.
 *
 * Enable wakeup device power of devices with the state.enable flag set and set
 * the wakeup enable mask bits in the GPE registers that correspond to wakeup
 * devices.
 */
void acpi_enable_wakeup_devices(u8 sleep_state)
{
	struct acpi_device *dev, *tmp;

	list_for_each_entry_safe(dev, tmp, &acpi_wakeup_device_list,
				 wakeup_list) {
		if (!dev->wakeup.flags.valid
		    || sleep_state > (u32) dev->wakeup.sleep_state
		    || !(device_may_wakeup(&dev->dev)
		        || dev->wakeup.prepare_count))
			continue;

		if (device_may_wakeup(&dev->dev))
			acpi_enable_wakeup_device_power(dev, sleep_state);

		/* The wake-up power should have been enabled already. */
		acpi_set_gpe_wake_mask(dev->wakeup.gpe_device, dev->wakeup.gpe_number,
				ACPI_GPE_ENABLE);
	}
}

/**
 * acpi_disable_wakeup_devices - Disable devices' wakeup capability.
 * @sleep_state: ACPI system sleep state.
 */
void acpi_disable_wakeup_devices(u8 sleep_state)
{
	struct acpi_device *dev, *tmp;

	list_for_each_entry_safe(dev, tmp, &acpi_wakeup_device_list,
				 wakeup_list) {
		if (!dev->wakeup.flags.valid
		    || sleep_state > (u32) dev->wakeup.sleep_state
		    || !(device_may_wakeup(&dev->dev)
		        || dev->wakeup.prepare_count))
			continue;

		acpi_set_gpe_wake_mask(dev->wakeup.gpe_device, dev->wakeup.gpe_number,
				ACPI_GPE_DISABLE);

		if (device_may_wakeup(&dev->dev))
			acpi_disable_wakeup_device_power(dev);
	}
}

int __init acpi_wakeup_device_init(void)
{
	struct acpi_device *dev, *tmp;

	mutex_lock(&acpi_device_lock);
	list_for_each_entry_safe(dev, tmp, &acpi_wakeup_device_list,
				 wakeup_list) {
		if (device_can_wakeup(&dev->dev)) {
			/* Button GPEs are supposed to be always enabled. */
			acpi_enable_gpe(dev->wakeup.gpe_device,
					dev->wakeup.gpe_number);
			device_set_wakeup_enable(&dev->dev, true);
		}
	}
	mutex_unlock(&acpi_device_lock);
	return 0;
}
