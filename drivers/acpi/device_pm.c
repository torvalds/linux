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
 * acpi_wakeup_device - Wakeup notification handler for ACPI devices.
 * @handle: ACPI handle of the device the notification is for.
 * @event: Type of the signaled event.
 * @context: Device corresponding to @handle.
 */
static void acpi_wakeup_device(acpi_handle handle, u32 event, void *context)
{
	struct device *dev = context;

	if (event == ACPI_NOTIFY_DEVICE_WAKE && dev) {
		pm_wakeup_event(dev, 0);
		pm_runtime_resume(dev);
	}
}

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
#else
static inline void acpi_wakeup_device(acpi_handle handle, u32 event,
				      void *context) {}
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

/**
 * acpi_dev_pm_get_node - Get ACPI device node for the given physical device.
 * @dev: Device to get the ACPI node for.
 */
static struct acpi_device *acpi_dev_pm_get_node(struct device *dev)
{
	acpi_handle handle = DEVICE_ACPI_HANDLE(dev);
	struct acpi_device *adev;

	return handle && !acpi_bus_get_device(handle, &adev) ? adev : NULL;
}

/**
 * acpi_dev_pm_low_power - Put ACPI device into a low-power state.
 * @dev: Device to put into a low-power state.
 * @adev: ACPI device node corresponding to @dev.
 * @system_state: System state to choose the device state for.
 */
static int acpi_dev_pm_low_power(struct device *dev, struct acpi_device *adev,
				 u32 system_state)
{
	int power_state;

	if (!acpi_device_power_manageable(adev))
		return 0;

	power_state = acpi_device_power_state(dev, adev, system_state,
					      ACPI_STATE_D3, NULL);
	if (power_state < ACPI_STATE_D0 || power_state > ACPI_STATE_D3)
		return -EIO;

	return acpi_device_set_power(adev, power_state);
}

/**
 * acpi_dev_pm_full_power - Put ACPI device into the full-power state.
 * @adev: ACPI device node to put into the full-power state.
 */
static int acpi_dev_pm_full_power(struct acpi_device *adev)
{
	return acpi_device_power_manageable(adev) ?
		acpi_device_set_power(adev, ACPI_STATE_D0) : 0;
}

#ifdef CONFIG_PM_RUNTIME
/**
 * acpi_dev_runtime_suspend - Put device into a low-power state using ACPI.
 * @dev: Device to put into a low-power state.
 *
 * Put the given device into a runtime low-power state using the standard ACPI
 * mechanism.  Set up remote wakeup if desired, choose the state to put the
 * device into (this checks if remote wakeup is expected to work too), and set
 * the power state of the device.
 */
int acpi_dev_runtime_suspend(struct device *dev)
{
	struct acpi_device *adev = acpi_dev_pm_get_node(dev);
	bool remote_wakeup;
	int error;

	if (!adev)
		return 0;

	remote_wakeup = dev_pm_qos_flags(dev, PM_QOS_FLAG_REMOTE_WAKEUP) >
				PM_QOS_FLAGS_NONE;
	error = __acpi_device_run_wake(adev, remote_wakeup);
	if (remote_wakeup && error)
		return -EAGAIN;

	error = acpi_dev_pm_low_power(dev, adev, ACPI_STATE_S0);
	if (error)
		__acpi_device_run_wake(adev, false);

	return error;
}
EXPORT_SYMBOL_GPL(acpi_dev_runtime_suspend);

/**
 * acpi_dev_runtime_resume - Put device into the full-power state using ACPI.
 * @dev: Device to put into the full-power state.
 *
 * Put the given device into the full-power state using the standard ACPI
 * mechanism at run time.  Set the power state of the device to ACPI D0 and
 * disable remote wakeup.
 */
int acpi_dev_runtime_resume(struct device *dev)
{
	struct acpi_device *adev = acpi_dev_pm_get_node(dev);
	int error;

	if (!adev)
		return 0;

	error = acpi_dev_pm_full_power(adev);
	__acpi_device_run_wake(adev, false);
	return error;
}
EXPORT_SYMBOL_GPL(acpi_dev_runtime_resume);

/**
 * acpi_subsys_runtime_suspend - Suspend device using ACPI.
 * @dev: Device to suspend.
 *
 * Carry out the generic runtime suspend procedure for @dev and use ACPI to put
 * it into a runtime low-power state.
 */
int acpi_subsys_runtime_suspend(struct device *dev)
{
	int ret = pm_generic_runtime_suspend(dev);
	return ret ? ret : acpi_dev_runtime_suspend(dev);
}
EXPORT_SYMBOL_GPL(acpi_subsys_runtime_suspend);

/**
 * acpi_subsys_runtime_resume - Resume device using ACPI.
 * @dev: Device to Resume.
 *
 * Use ACPI to put the given device into the full-power state and carry out the
 * generic runtime resume procedure for it.
 */
int acpi_subsys_runtime_resume(struct device *dev)
{
	int ret = acpi_dev_runtime_resume(dev);
	return ret ? ret : pm_generic_runtime_resume(dev);
}
EXPORT_SYMBOL_GPL(acpi_subsys_runtime_resume);
#endif /* CONFIG_PM_RUNTIME */

#ifdef CONFIG_PM_SLEEP
/**
 * acpi_dev_suspend_late - Put device into a low-power state using ACPI.
 * @dev: Device to put into a low-power state.
 *
 * Put the given device into a low-power state during system transition to a
 * sleep state using the standard ACPI mechanism.  Set up system wakeup if
 * desired, choose the state to put the device into (this checks if system
 * wakeup is expected to work too), and set the power state of the device.
 */
int acpi_dev_suspend_late(struct device *dev)
{
	struct acpi_device *adev = acpi_dev_pm_get_node(dev);
	u32 target_state;
	bool wakeup;
	int error;

	if (!adev)
		return 0;

	target_state = acpi_target_system_state();
	wakeup = device_may_wakeup(dev);
	error = __acpi_device_sleep_wake(adev, target_state, wakeup);
	if (wakeup && error)
		return error;

	error = acpi_dev_pm_low_power(dev, adev, target_state);
	if (error)
		__acpi_device_sleep_wake(adev, ACPI_STATE_UNKNOWN, false);

	return error;
}
EXPORT_SYMBOL_GPL(acpi_dev_suspend_late);

/**
 * acpi_dev_resume_early - Put device into the full-power state using ACPI.
 * @dev: Device to put into the full-power state.
 *
 * Put the given device into the full-power state using the standard ACPI
 * mechanism during system transition to the working state.  Set the power
 * state of the device to ACPI D0 and disable remote wakeup.
 */
int acpi_dev_resume_early(struct device *dev)
{
	struct acpi_device *adev = acpi_dev_pm_get_node(dev);
	int error;

	if (!adev)
		return 0;

	error = acpi_dev_pm_full_power(adev);
	__acpi_device_sleep_wake(adev, ACPI_STATE_UNKNOWN, false);
	return error;
}
EXPORT_SYMBOL_GPL(acpi_dev_resume_early);

/**
 * acpi_subsys_prepare - Prepare device for system transition to a sleep state.
 * @dev: Device to prepare.
 */
int acpi_subsys_prepare(struct device *dev)
{
	/*
	 * Follow PCI and resume devices suspended at run time before running
	 * their system suspend callbacks.
	 */
	pm_runtime_resume(dev);
	return pm_generic_prepare(dev);
}
EXPORT_SYMBOL_GPL(acpi_subsys_prepare);

/**
 * acpi_subsys_suspend_late - Suspend device using ACPI.
 * @dev: Device to suspend.
 *
 * Carry out the generic late suspend procedure for @dev and use ACPI to put
 * it into a low-power state during system transition into a sleep state.
 */
int acpi_subsys_suspend_late(struct device *dev)
{
	int ret = pm_generic_suspend_late(dev);
	return ret ? ret : acpi_dev_suspend_late(dev);
}
EXPORT_SYMBOL_GPL(acpi_subsys_suspend_late);

/**
 * acpi_subsys_resume_early - Resume device using ACPI.
 * @dev: Device to Resume.
 *
 * Use ACPI to put the given device into the full-power state and carry out the
 * generic early resume procedure for it during system transition into the
 * working state.
 */
int acpi_subsys_resume_early(struct device *dev)
{
	int ret = acpi_dev_resume_early(dev);
	return ret ? ret : pm_generic_resume_early(dev);
}
EXPORT_SYMBOL_GPL(acpi_subsys_resume_early);
#endif /* CONFIG_PM_SLEEP */

static struct dev_pm_domain acpi_general_pm_domain = {
	.ops = {
#ifdef CONFIG_PM_RUNTIME
		.runtime_suspend = acpi_subsys_runtime_suspend,
		.runtime_resume = acpi_subsys_runtime_resume,
		.runtime_idle = pm_generic_runtime_idle,
#endif
#ifdef CONFIG_PM_SLEEP
		.prepare = acpi_subsys_prepare,
		.suspend_late = acpi_subsys_suspend_late,
		.resume_early = acpi_subsys_resume_early,
		.poweroff_late = acpi_subsys_suspend_late,
		.restore_early = acpi_subsys_resume_early,
#endif
	},
};

/**
 * acpi_dev_pm_attach - Prepare device for ACPI power management.
 * @dev: Device to prepare.
 * @power_on: Whether or not to power on the device.
 *
 * If @dev has a valid ACPI handle that has a valid struct acpi_device object
 * attached to it, install a wakeup notification handler for the device and
 * add it to the general ACPI PM domain.  If @power_on is set, the device will
 * be put into the ACPI D0 state before the function returns.
 *
 * This assumes that the @dev's bus type uses generic power management callbacks
 * (or doesn't use any power management callbacks at all).
 *
 * Callers must ensure proper synchronization of this function with power
 * management callbacks.
 */
int acpi_dev_pm_attach(struct device *dev, bool power_on)
{
	struct acpi_device *adev = acpi_dev_pm_get_node(dev);

	if (!adev)
		return -ENODEV;

	if (dev->pm_domain)
		return -EEXIST;

	acpi_add_pm_notifier(adev, acpi_wakeup_device, dev);
	dev->pm_domain = &acpi_general_pm_domain;
	if (power_on) {
		acpi_dev_pm_full_power(adev);
		__acpi_device_run_wake(adev, false);
	}
	return 0;
}
EXPORT_SYMBOL_GPL(acpi_dev_pm_attach);

/**
 * acpi_dev_pm_detach - Remove ACPI power management from the device.
 * @dev: Device to take care of.
 * @power_off: Whether or not to try to remove power from the device.
 *
 * Remove the device from the general ACPI PM domain and remove its wakeup
 * notifier.  If @power_off is set, additionally remove power from the device if
 * possible.
 *
 * Callers must ensure proper synchronization of this function with power
 * management callbacks.
 */
void acpi_dev_pm_detach(struct device *dev, bool power_off)
{
	struct acpi_device *adev = acpi_dev_pm_get_node(dev);

	if (adev && dev->pm_domain == &acpi_general_pm_domain) {
		dev->pm_domain = NULL;
		acpi_remove_pm_notifier(adev, acpi_wakeup_device);
		if (power_off) {
			/*
			 * If the device's PM QoS resume latency limit or flags
			 * have been exposed to user space, they have to be
			 * hidden at this point, so that they don't affect the
			 * choice of the low-power state to put the device into.
			 */
			dev_pm_qos_hide_latency_limit(dev);
			dev_pm_qos_hide_flags(dev);
			__acpi_device_run_wake(adev, false);
			acpi_dev_pm_low_power(dev, adev, ACPI_STATE_S0);
		}
	}
}
EXPORT_SYMBOL_GPL(acpi_dev_pm_detach);
