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
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>

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

/**
 * acpi_device_power_state - Get preferred power state of ACPI device.
 * @dev: Device whose preferred target power state to return.
 * @adev: ACPI device node corresponding to @dev.
 * @target_state: System state to match the resultant device state.
 * @d_max_in: Deepest low-power state to take into consideration.
 * @d_min_p: Location to store the upper limit of the allowed states range.
 * Return value: Preferred power state of the device on success, -ENODEV
 * (if there's no 'struct acpi_device' for @dev) or -EINVAL on failure
 *
 * Find the lowest power (highest number) ACPI device power state that the
 * device can be in while the system is in the state represented by
 * @target_state.  If @d_min_p is set, the highest power (lowest number) device
 * power state that @dev can be in for the given system sleep state is stored
 * at the location pointed to by it.
 *
 * Callers must ensure that @dev and @adev are valid pointers and that @adev
 * actually corresponds to @dev before using this function.
 */
int acpi_device_power_state(struct device *dev, struct acpi_device *adev,
			    u32 target_state, int d_max_in, int *d_min_p)
{
	char acpi_method[] = "_SxD";
	unsigned long long d_min, d_max;
	bool wakeup = false;

	if (d_max_in < ACPI_STATE_D0 || d_max_in > ACPI_STATE_D3)
		return -EINVAL;

	if (d_max_in > ACPI_STATE_D3_HOT) {
		enum pm_qos_flags_status stat;

		stat = dev_pm_qos_flags(dev, PM_QOS_FLAG_NO_POWER_OFF);
		if (stat == PM_QOS_FLAGS_ALL)
			d_max_in = ACPI_STATE_D3_HOT;
	}

	acpi_method[2] = '0' + target_state;
	/*
	 * If the sleep state is S0, the lowest limit from ACPI is D3,
	 * but if the device has _S0W, we will use the value from _S0W
	 * as the lowest limit from ACPI.  Finally, we will constrain
	 * the lowest limit with the specified one.
	 */
	d_min = ACPI_STATE_D0;
	d_max = ACPI_STATE_D3;

	/*
	 * If present, _SxD methods return the minimum D-state (highest power
	 * state) we can use for the corresponding S-states.  Otherwise, the
	 * minimum D-state is D0 (ACPI 3.x).
	 *
	 * NOTE: We rely on acpi_evaluate_integer() not clobbering the integer
	 * provided -- that's our fault recovery, we ignore retval.
	 */
	if (target_state > ACPI_STATE_S0) {
		acpi_evaluate_integer(adev->handle, acpi_method, NULL, &d_min);
		wakeup = device_may_wakeup(dev) && adev->wakeup.flags.valid
			&& adev->wakeup.sleep_state >= target_state;
	} else if (dev_pm_qos_flags(dev, PM_QOS_FLAG_REMOTE_WAKEUP) !=
			PM_QOS_FLAGS_NONE) {
		wakeup = adev->wakeup.flags.valid;
	}

	/*
	 * If _PRW says we can wake up the system from the target sleep state,
	 * the D-state returned by _SxD is sufficient for that (we assume a
	 * wakeup-aware driver if wake is set).  Still, if _SxW exists
	 * (ACPI 3.x), it should return the maximum (lowest power) D-state that
	 * can wake the system.  _S0W may be valid, too.
	 */
	if (wakeup) {
		acpi_status status;

		acpi_method[3] = 'W';
		status = acpi_evaluate_integer(adev->handle, acpi_method, NULL,
						&d_max);
		if (ACPI_FAILURE(status)) {
			if (target_state != ACPI_STATE_S0 ||
			    status != AE_NOT_FOUND)
				d_max = d_min;
		} else if (d_max < d_min) {
			/* Warn the user of the broken DSDT */
			printk(KERN_WARNING "ACPI: Wrong value from %s\n",
				acpi_method);
			/* Sanitize it */
			d_min = d_max;
		}
	}

	if (d_max_in < d_min)
		return -EINVAL;
	if (d_min_p)
		*d_min_p = d_min;
	/* constrain d_max with specified lowest limit (max number) */
	if (d_max > d_max_in) {
		for (d_max = d_max_in; d_max > d_min; d_max--) {
			if (adev->power.states[d_max].flags.valid)
				break;
		}
	}
	return d_max;
}
EXPORT_SYMBOL_GPL(acpi_device_power_state);
