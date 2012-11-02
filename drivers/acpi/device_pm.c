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
#include <linux/pm_runtime.h>

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

/**
 * acpi_pm_device_sleep_state - Get preferred power state of ACPI device.
 * @dev: Device whose preferred target power state to return.
 * @d_min_p: Location to store the upper limit of the allowed states range.
 * @d_max_in: Deepest low-power state to take into consideration.
 * Return value: Preferred power state of the device on success, -ENODEV
 * (if there's no 'struct acpi_device' for @dev) or -EINVAL on failure
 *
 * The caller must ensure that @dev is valid before using this function.
 */
int acpi_pm_device_sleep_state(struct device *dev, int *d_min_p, int d_max_in)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(dev);
	struct acpi_device *adev;

	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &adev))) {
		dev_dbg(dev, "ACPI handle without context in %s!\n", __func__);
		return -ENODEV;
	}

	return acpi_device_power_state(dev, adev, acpi_target_system_state(),
				       d_max_in, d_min_p);
}
EXPORT_SYMBOL(acpi_pm_device_sleep_state);

#ifdef CONFIG_PM_RUNTIME
/**
 * __acpi_device_run_wake - Enable/disable runtime remote wakeup for device.
 * @adev: ACPI device to enable/disable the remote wakeup for.
 * @enable: Whether to enable or disable the wakeup functionality.
 *
 * Enable/disable the GPE associated with @adev so that it can generate
 * wakeup signals for the device in response to external (remote) events and
 * enable/disable device wakeup power.
 *
 * Callers must ensure that @adev is a valid ACPI device node before executing
 * this function.
 */
int __acpi_device_run_wake(struct acpi_device *adev, bool enable)
{
	struct acpi_device_wakeup *wakeup = &adev->wakeup;

	if (enable) {
		acpi_status res;
		int error;

		error = acpi_enable_wakeup_device_power(adev, ACPI_STATE_S0);
		if (error)
			return error;

		res = acpi_enable_gpe(wakeup->gpe_device, wakeup->gpe_number);
		if (ACPI_FAILURE(res)) {
			acpi_disable_wakeup_device_power(adev);
			return -EIO;
		}
	} else {
		acpi_disable_gpe(wakeup->gpe_device, wakeup->gpe_number);
		acpi_disable_wakeup_device_power(adev);
	}
	return 0;
}

/**
 * acpi_pm_device_run_wake - Enable/disable remote wakeup for given device.
 * @dev: Device to enable/disable the platform to wake up.
 * @enable: Whether to enable or disable the wakeup functionality.
 */
int acpi_pm_device_run_wake(struct device *phys_dev, bool enable)
{
	struct acpi_device *adev;
	acpi_handle handle;

	if (!device_run_wake(phys_dev))
		return -EINVAL;

	handle = DEVICE_ACPI_HANDLE(phys_dev);
	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &adev))) {
		dev_dbg(phys_dev, "ACPI handle without context in %s!\n",
			__func__);
		return -ENODEV;
	}

	return __acpi_device_run_wake(adev, enable);
}
EXPORT_SYMBOL(acpi_pm_device_run_wake);
#endif /* CONFIG_PM_RUNTIME */

 #ifdef CONFIG_PM_SLEEP
/**
 * __acpi_device_sleep_wake - Enable or disable device to wake up the system.
 * @dev: Device to enable/desible to wake up the system.
 * @target_state: System state the device is supposed to wake up from.
 * @enable: Whether to enable or disable @dev to wake up the system.
 */
int __acpi_device_sleep_wake(struct acpi_device *adev, u32 target_state,
			     bool enable)
{
	return enable ?
		acpi_enable_wakeup_device_power(adev, target_state) :
		acpi_disable_wakeup_device_power(adev);
}

/**
 * acpi_pm_device_sleep_wake - Enable or disable device to wake up the system.
 * @dev: Device to enable/desible to wake up the system from sleep states.
 * @enable: Whether to enable or disable @dev to wake up the system.
 */
int acpi_pm_device_sleep_wake(struct device *dev, bool enable)
{
	acpi_handle handle;
	struct acpi_device *adev;
	int error;

	if (!device_can_wakeup(dev))
		return -EINVAL;

	handle = DEVICE_ACPI_HANDLE(dev);
	if (!handle || ACPI_FAILURE(acpi_bus_get_device(handle, &adev))) {
		dev_dbg(dev, "ACPI handle without context in %s!\n", __func__);
		return -ENODEV;
	}

	error = __acpi_device_sleep_wake(adev, acpi_target_system_state(),
					 enable);
	if (!error)
		dev_info(dev, "System wakeup %s by ACPI\n",
				enable ? "enabled" : "disabled");

	return error;
}
#endif /* CONFIG_PM_SLEEP */
