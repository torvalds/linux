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
 * acpi_enable_wakeup_device_prep - Prepare wake-up devices.
 * @sleep_state: ACPI system sleep state.
 *
 * Enable all wake-up devices' power, unless the requested system sleep state is
 * too deep.
 */
void acpi_enable_wakeup_device_prep(u8 sleep_state)
{
	struct list_head *node, *next;

	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev = container_of(node,
						       struct acpi_device,
						       wakeup_list);

		if (!dev->wakeup.flags.valid || !dev->wakeup.state.enabled
		    || (sleep_state > (u32) dev->wakeup.sleep_state))
			continue;

		acpi_enable_wakeup_device_power(dev, sleep_state);
	}
}

/**
 * acpi_enable_wakeup_device - Enable wake-up device GPEs.
 * @sleep_state: ACPI system sleep state.
 *
 * Enable all wake-up devices' GPEs, with the assumption that
 * acpi_disable_all_gpes() was executed before, so we don't need to disable any
 * GPEs here.
 */
void acpi_enable_wakeup_device(u8 sleep_state)
{
	struct list_head *node, *next;

	/* 
	 * Caution: this routine must be invoked when interrupt is disabled 
	 * Refer ACPI2.0: P212
	 */
	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
			container_of(node, struct acpi_device, wakeup_list);

		if (!dev->wakeup.flags.valid || !dev->wakeup.state.enabled
		    || sleep_state > (u32) dev->wakeup.sleep_state)
			continue;

		/* The wake-up power should have been enabled already. */
		acpi_enable_gpe(dev->wakeup.gpe_device, dev->wakeup.gpe_number,
				ACPI_GPE_TYPE_WAKE);
	}
}

/**
 * acpi_disable_wakeup_device - Disable devices' wakeup capability.
 * @sleep_state: ACPI system sleep state.
 *
 * This function only affects devices with wakeup.state.enabled set, which means
 * that it reverses the changes made by acpi_enable_wakeup_device_prep().
 */
void acpi_disable_wakeup_device(u8 sleep_state)
{
	struct list_head *node, *next;

	list_for_each_safe(node, next, &acpi_wakeup_device_list) {
		struct acpi_device *dev =
			container_of(node, struct acpi_device, wakeup_list);

		if (!dev->wakeup.flags.valid || !dev->wakeup.state.enabled
		    || (sleep_state > (u32) dev->wakeup.sleep_state))
			continue;

		acpi_disable_gpe(dev->wakeup.gpe_device, dev->wakeup.gpe_number,
				ACPI_GPE_TYPE_WAKE);
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
