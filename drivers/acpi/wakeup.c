/*
 * wakeup.c - support wakeup devices
 * Copyright (C) 2004 Li Shaohua <shaohua.li@intel.com>
 */

#include <linux/init.h>
#include <linux/acpi.h>
#include <acpi/acpi_drivers.h>
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
	struct list_head *node, *next;

	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
			container_of(node, struct acpi_device, wakeup_list);

		if (!dev->wakeup.flags.valid
		    || !(dev->wakeup.state.enabled || dev->wakeup.prepare_count)
		    || sleep_state > (u32) dev->wakeup.sleep_state)
			continue;

		if (dev->wakeup.state.enabled)
			acpi_enable_wakeup_device_power(dev, sleep_state);

		/* The wake-up power should have been enabled already. */
		acpi_gpe_wakeup(dev->wakeup.gpe_device, dev->wakeup.gpe_number,
				ACPI_GPE_ENABLE);
	}
}

/**
 * acpi_disable_wakeup_devices - Disable devices' wakeup capability.
 * @sleep_state: ACPI system sleep state.
 */
void acpi_disable_wakeup_devices(u8 sleep_state)
{
	struct list_head *node, *next;

	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
			container_of(node, struct acpi_device, wakeup_list);

		if (!dev->wakeup.flags.valid
		    || !(dev->wakeup.state.enabled || dev->wakeup.prepare_count)
		    || (sleep_state > (u32) dev->wakeup.sleep_state))
			continue;

		acpi_gpe_wakeup(dev->wakeup.gpe_device, dev->wakeup.gpe_number,
				ACPI_GPE_DISABLE);

		if (dev->wakeup.state.enabled)
			acpi_disable_wakeup_device_power(dev);
	}
}

int __init acpi_wakeup_device_init(void)
{
	struct list_head *node, *next;

	mutex_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev = container_of(node,
						       struct acpi_device,
						       wakeup_list);
		if (dev->wakeup.flags.always_enabled)
			dev->wakeup.state.enabled = 1;
	}
	mutex_unlock(&acpi_device_lock);
	return 0;
}
