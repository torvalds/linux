/*
 * drivers/acpi/device_pm.c - ACPI device power management routines.
 *
 * Copyright (C) 2012, Intel Corp.
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as published
 *  by the Free Software Foundation.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/device.h>
#include <linux/mutex.h>

#include <acpi/acpi.h>
#include <acpi/acpi_bus.h>

static DEFINE_MUTEX(acpi_pm_notifier_lock);

/**
 * acpi_add_pm_notifier - Register PM notifier for given ACPI device.
 * @adev: ACPI device to add the notifier for.
 * @context: Context information to pass to the notifier routine.
 *
 * NOTE: @adev need not be a run-wake or wakeup device to be a valid source of
 * PM wakeup events.  For example, wakeup events may be generated for bridges
 * if one of the devices below the bridge is signaling wakeup, even if the
 * bridge itself doesn't have a wakeup GPE associated with it.
 */
acpi_status acpi_add_pm_notifier(struct acpi_device *adev,
				 acpi_notify_handler handler, void *context)
{
	acpi_status status = AE_ALREADY_EXISTS;

	mutex_lock(&acpi_pm_notifier_lock);

	if (adev->wakeup.flags.notifier_present)
		goto out;

	status = acpi_install_notify_handler(adev->handle,
					     ACPI_SYSTEM_NOTIFY,
					     handler, context);
	if (ACPI_FAILURE(status))
		goto out;

	adev->wakeup.flags.notifier_present = true;

 out:
	mutex_unlock(&acpi_pm_notifier_lock);
	return status;
}

/**
 * acpi_remove_pm_notifier - Unregister PM notifier from given ACPI device.
 * @adev: ACPI device to remove the notifier from.
 */
acpi_status acpi_remove_pm_notifier(struct acpi_device *adev,
				    acpi_notify_handler handler)
{
	acpi_status status = AE_BAD_PARAMETER;

	mutex_lock(&acpi_pm_notifier_lock);

	if (!adev->wakeup.flags.notifier_present)
		goto out;

	status = acpi_remove_notify_handler(adev->handle,
					    ACPI_SYSTEM_NOTIFY,
					    handler);
	if (ACPI_FAILURE(status))
		goto out;

	adev->wakeup.flags.notifier_present = false;

 out:
	mutex_unlock(&acpi_pm_notifier_lock);
	return status;
}
