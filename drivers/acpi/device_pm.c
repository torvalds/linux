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

#include <linux/acpi.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/pm_qos.h>
#include <linux/pm_runtime.h>

#include "internal.h"

#define _COMPONENT	ACPI_POWER_COMPONENT
ACPI_MODULE_NAME("device_pm");

/**
 * acpi_power_state_string - String representation of ACPI device power state.
 * @state: ACPI device power state to return the string representation of.
 */
const char *acpi_power_state_string(int state)
{
	switch (state) {
	case ACPI_STATE_D0:
		return "D0";
	case ACPI_STATE_D1:
		return "D1";
	case ACPI_STATE_D2:
		return "D2";
	case ACPI_STATE_D3_HOT:
		return "D3hot";
	case ACPI_STATE_D3_COLD:
		return "D3cold";
	default:
		return "(unknown)";
	}
}

/**
 * acpi_device_get_power - Get power state of an ACPI device.
 * @device: Device to get the power state of.
 * @state: Place to store the power state of the device.
 *
 * This function does not update the device's power.state field, but it may
 * update its parent's power.state field (when the parent's power state is
 * unknown and the device's power state turns out to be D0).
 */
int acpi_device_get_power(struct acpi_device *device, int *state)
{
	int result = ACPI_STATE_UNKNOWN;

	if (!device || !state)
		return -EINVAL;

	if (!device->flags.power_manageable) {
		/* TBD: Non-recursive algorithm for walking up hierarchy. */
		*state = device->parent ?
			device->parent->power.state : ACPI_STATE_D0;
		goto out;
	}

	/*
	 * Get the device's power state from power resources settings and _PSC,
	 * if available.
	 */
	if (device->power.flags.power_resources) {
		int error = acpi_power_get_inferred_state(device, &result);
		if (error)
			return error;
	}
	if (device->power.flags.explicit_get) {
		acpi_handle handle = device->handle;
		unsigned long long psc;
		acpi_status status;

		status = acpi_evaluate_integer(handle, "_PSC", NULL, &psc);
		if (ACPI_FAILURE(status))
			return -ENODEV;

		/*
		 * The power resources settings may indicate a power state
		 * shallower than the actual power state of the device.
		 *
		 * Moreover, on systems predating ACPI 4.0, if the device
		 * doesn't depend on any power resources and _PSC returns 3,
		 * that means "power off".  We need to maintain compatibility
		 * with those systems.
		 */
		if (psc > result && psc < ACPI_STATE_D3_COLD)
			result = psc;
		else if (result == ACPI_STATE_UNKNOWN)
			result = psc > ACPI_STATE_D2 ? ACPI_STATE_D3_COLD : psc;
	}

	/*
	 * If we were unsure about the device parent's power state up to this
	 * point, the fact that the device is in D0 implies that the parent has
	 * to be in D0 too, except if ignore_parent is set.
	 */
	if (!device->power.flags.ignore_parent && device->parent
	    && device->parent->power.state == ACPI_STATE_UNKNOWN
	    && result == ACPI_STATE_D0)
		device->parent->power.state = ACPI_STATE_D0;

	*state = result;

 out:
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] power state is %s\n",
			  device->pnp.bus_id, acpi_power_state_string(*state)));

	return 0;
}

static int acpi_dev_pm_explicit_set(struct acpi_device *adev, int state)
{
	if (adev->power.states[state].flags.explicit_set) {
		char method[5] = { '_', 'P', 'S', '0' + state, '\0' };
		acpi_status status;

		status = acpi_evaluate_object(adev->handle, method, NULL, NULL);
		if (ACPI_FAILURE(status))
			return -ENODEV;
	}
	return 0;
}

/**
 * acpi_device_set_power - Set power state of an ACPI device.
 * @device: Device to set the power state of.
 * @state: New power state to set.
 *
 * Callers must ensure that the device is power manageable before using this
 * function.
 */
int acpi_device_set_power(struct acpi_device *device, int state)
{
	int result = 0;
	bool cut_power = false;

	if (!device || !device->flags.power_manageable
	    || (state < ACPI_STATE_D0) || (state > ACPI_STATE_D3_COLD))
		return -EINVAL;

	/* Make sure this is a valid target state */

	if (state == device->power.state) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] already in %s\n",
				  device->pnp.bus_id,
				  acpi_power_state_string(state)));
		return 0;
	}

	if (!device->power.states[state].flags.valid) {
		dev_warn(&device->dev, "Power state %s not supported\n",
			 acpi_power_state_string(state));
		return -ENODEV;
	}
	if (!device->power.flags.ignore_parent &&
	    device->parent && (state < device->parent->power.state)) {
		dev_warn(&device->dev,
			 "Cannot transition to power state %s for parent in %s\n",
			 acpi_power_state_string(state),
			 acpi_power_state_string(device->parent->power.state));
		return -ENODEV;
	}

	/* For D3cold we should first transition into D3hot. */
	if (state == ACPI_STATE_D3_COLD
	    && device->power.states[ACPI_STATE_D3_COLD].flags.os_accessible) {
		state = ACPI_STATE_D3_HOT;
		cut_power = true;
	}

	if (state < device->power.state && state != ACPI_STATE_D0
	    && device->power.state >= ACPI_STATE_D3_HOT) {
		dev_warn(&device->dev,
			 "Cannot transition to non-D0 state from D3\n");
		return -ENODEV;
	}

	/*
	 * Transition Power
	 * ----------------
	 * In accordance with the ACPI specification first apply power (via
	 * power resources) and then evalute _PSx.
	 */
	if (device->power.flags.power_resources) {
		result = acpi_power_transition(device, state);
		if (result)
			goto end;
	}
	result = acpi_dev_pm_explicit_set(device, state);
	if (result)
		goto end;

	if (cut_power) {
		device->power.state = state;
		state = ACPI_STATE_D3_COLD;
		result = acpi_power_transition(device, state);
	}

 end:
	if (result) {
		dev_warn(&device->dev, "Failed to change power state to %s\n",
			 acpi_power_state_string(state));
	} else {
		device->power.state = state;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Device [%s] transitioned to %s\n",
				  device->pnp.bus_id,
				  acpi_power_state_string(state)));
	}

	return result;
}
EXPORT_SYMBOL(acpi_device_set_power);

int acpi_bus_set_power(acpi_handle handle, int state)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	return acpi_device_set_power(device, state);
}
EXPORT_SYMBOL(acpi_bus_set_power);

int acpi_bus_init_power(struct acpi_device *device)
{
	int state;
	int result;

	if (!device)
		return -EINVAL;

	device->power.state = ACPI_STATE_UNKNOWN;

	result = acpi_device_get_power(device, &state);
	if (result)
		return result;

	if (state < ACPI_STATE_D3_COLD && device->power.flags.power_resources) {
		result = acpi_power_on_resources(device, state);
		if (result)
			return result;

		result = acpi_dev_pm_explicit_set(device, state);
		if (result)
			return result;
	} else if (state == ACPI_STATE_UNKNOWN) {
		/*
		 * No power resources and missing _PSC?  Cross fingers and make
		 * it D0 in hope that this is what the BIOS put the device into.
		 * [We tried to force D0 here by executing _PS0, but that broke
		 * Toshiba P870-303 in a nasty way.]
		 */
		state = ACPI_STATE_D0;
	}
	device->power.state = state;
	return 0;
}

/**
 * acpi_device_fix_up_power - Force device with missing _PSC into D0.
 * @device: Device object whose power state is to be fixed up.
 *
 * Devices without power resources and _PSC, but having _PS0 and _PS3 defined,
 * are assumed to be put into D0 by the BIOS.  However, in some cases that may
 * not be the case and this function should be used then.
 */
int acpi_device_fix_up_power(struct acpi_device *device)
{
	int ret = 0;

	if (!device->power.flags.power_resources
	    && !device->power.flags.explicit_get
	    && device->power.state == ACPI_STATE_D0)
		ret = acpi_dev_pm_explicit_set(device, ACPI_STATE_D0);

	return ret;
}

int acpi_bus_update_power(acpi_handle handle, int *state_p)
{
	struct acpi_device *device;
	int state;
	int result;

	result = acpi_bus_get_device(handle, &device);
	if (result)
		return result;

	result = acpi_device_get_power(device, &state);
	if (result)
		return result;

	if (state == ACPI_STATE_UNKNOWN) {
		state = ACPI_STATE_D0;
		result = acpi_device_set_power(device, state);
		if (result)
			return result;
	} else {
		if (device->power.flags.power_resources) {
			/*
			 * We don't need to really switch the state, bu we need
			 * to update the power resources' reference counters.
			 */
			result = acpi_power_transition(device, state);
			if (result)
				return result;
		}
		device->power.state = state;
	}
	if (state_p)
		*state_p = state;

	return 0;
}
EXPORT_SYMBOL_GPL(acpi_bus_update_power);

bool acpi_bus_power_manageable(acpi_handle handle)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	return result ? false : device->flags.power_manageable;
}
EXPORT_SYMBOL(acpi_bus_power_manageable);

#ifdef CONFIG_PM
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

bool acpi_bus_can_wakeup(acpi_handle handle)
{
	struct acpi_device *device;
	int result;

	result = acpi_bus_get_device(handle, &device);
	return result ? false : device->wakeup.flags.valid;
}
EXPORT_SYMBOL(acpi_bus_can_wakeup);

/**
 * acpi_dev_pm_get_state - Get preferred power state of ACPI device.
 * @dev: Device whose preferred target power state to return.
 * @adev: ACPI device node corresponding to @dev.
 * @target_state: System state to match the resultant device state.
 * @d_min_p: Location to store the highest power state available to the device.
 * @d_max_p: Location to store the lowest power state available to the device.
 *
 * Find the lowest power (highest number) and highest power (lowest number) ACPI
 * device power states that the device can be in while the system is in the
 * state represented by @target_state.  Store the integer numbers representing
 * those stats in the memory locations pointed to by @d_max_p and @d_min_p,
 * respectively.
 *
 * Callers must ensure that @dev and @adev are valid pointers and that @adev
 * actually corresponds to @dev before using this function.
 *
 * Returns 0 on success or -ENODATA when one of the ACPI methods fails or
 * returns a value that doesn't make sense.  The memory locations pointed to by
 * @d_max_p and @d_min_p are only modified on success.
 */
static int acpi_dev_pm_get_state(struct device *dev, struct acpi_device *adev,
				 u32 target_state, int *d_min_p, int *d_max_p)
{
	char method[] = { '_', 'S', '0' + target_state, 'D', '\0' };
	acpi_handle handle = adev->handle;
	unsigned long long ret;
	int d_min, d_max;
	bool wakeup = false;
	acpi_status status;

	/*
	 * If the system state is S0, the lowest power state the device can be
	 * in is D3cold, unless the device has _S0W and is supposed to signal
	 * wakeup, in which case the return value of _S0W has to be used as the
	 * lowest power state available to the device.
	 */
	d_min = ACPI_STATE_D0;
	d_max = ACPI_STATE_D3_COLD;

	/*
	 * If present, _SxD methods return the minimum D-state (highest power
	 * state) we can use for the corresponding S-states.  Otherwise, the
	 * minimum D-state is D0 (ACPI 3.x).
	 */
	if (target_state > ACPI_STATE_S0) {
		/*
		 * We rely on acpi_evaluate_integer() not clobbering the integer
		 * provided if AE_NOT_FOUND is returned.
		 */
		ret = d_min;
		status = acpi_evaluate_integer(handle, method, NULL, &ret);
		if ((ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		    || ret > ACPI_STATE_D3_COLD)
			return -ENODATA;

		/*
		 * We need to handle legacy systems where D3hot and D3cold are
		 * the same and 3 is returned in both cases, so fall back to
		 * D3cold if D3hot is not a valid state.
		 */
		if (!adev->power.states[ret].flags.valid) {
			if (ret == ACPI_STATE_D3_HOT)
				ret = ACPI_STATE_D3_COLD;
			else
				return -ENODATA;
		}
		d_min = ret;
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
		method[3] = 'W';
		status = acpi_evaluate_integer(handle, method, NULL, &ret);
		if (status == AE_NOT_FOUND) {
			if (target_state > ACPI_STATE_S0)
				d_max = d_min;
		} else if (ACPI_SUCCESS(status) && ret <= ACPI_STATE_D3_COLD) {
			/* Fall back to D3cold if ret is not a valid state. */
			if (!adev->power.states[ret].flags.valid)
				ret = ACPI_STATE_D3_COLD;

			d_max = ret > d_min ? ret : d_min;
		} else {
			return -ENODATA;
		}
	}

	if (d_min_p)
		*d_min_p = d_min;

	if (d_max_p)
		*d_max_p = d_max;

	return 0;
}

/**
 * acpi_pm_device_sleep_state - Get preferred power state of ACPI device.
 * @dev: Device whose preferred target power state to return.
 * @d_min_p: Location to store the upper limit of the allowed states range.
 * @d_max_in: Deepest low-power state to take into consideration.
 * Return value: Preferred power state of the device on success, -ENODEV
 * if there's no 'struct acpi_device' for @dev, -EINVAL if @d_max_in is
 * incorrect, or -ENODATA on ACPI method failure.
 *
 * The caller must ensure that @dev is valid before using this function.
 */
int acpi_pm_device_sleep_state(struct device *dev, int *d_min_p, int d_max_in)
{
	acpi_handle handle = ACPI_HANDLE(dev);
	struct acpi_device *adev;
	int ret, d_min, d_max;

	if (d_max_in < ACPI_STATE_D0 || d_max_in > ACPI_STATE_D3_COLD)
		return -EINVAL;

	if (d_max_in > ACPI_STATE_D3_HOT) {
		enum pm_qos_flags_status stat;

		stat = dev_pm_qos_flags(dev, PM_QOS_FLAG_NO_POWER_OFF);
		if (stat == PM_QOS_FLAGS_ALL)
			d_max_in = ACPI_STATE_D3_HOT;
	}

	if (!handle || acpi_bus_get_device(handle, &adev)) {
		dev_dbg(dev, "ACPI handle without context in %s!\n", __func__);
		return -ENODEV;
	}

	ret = acpi_dev_pm_get_state(dev, adev, acpi_target_system_state(),
				    &d_min, &d_max);
	if (ret)
		return ret;

	if (d_max_in < d_min)
		return -EINVAL;

	if (d_max > d_max_in) {
		for (d_max = d_max_in; d_max > d_min; d_max--) {
			if (adev->power.states[d_max].flags.valid)
				break;
		}
	}

	if (d_min_p)
		*d_min_p = d_min;

	return d_max;
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

	handle = ACPI_HANDLE(phys_dev);
	if (!handle || acpi_bus_get_device(handle, &adev)) {
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

	handle = ACPI_HANDLE(dev);
	if (!handle || acpi_bus_get_device(handle, &adev)) {
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
struct acpi_device *acpi_dev_pm_get_node(struct device *dev)
{
	acpi_handle handle = ACPI_HANDLE(dev);
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
	int ret, state;

	if (!acpi_device_power_manageable(adev))
		return 0;

	ret = acpi_dev_pm_get_state(dev, adev, system_state, NULL, &state);
	return ret ? ret : acpi_device_set_power(adev, state);
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
#endif /* CONFIG_PM */
