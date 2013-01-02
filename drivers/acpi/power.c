/*
 *  acpi_power.c - ACPI Bus Power Management ($Revision: 39 $)
 *
 *  Copyright (C) 2001, 2002 Andy Grover <andrew.grover@intel.com>
 *  Copyright (C) 2001, 2002 Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 * ACPI power-managed devices may be controlled in two ways:
 * 1. via "Device Specific (D-State) Control"
 * 2. via "Power Resource Control".
 * This module is used to manage devices relying on Power Resource Control.
 * 
 * An ACPI "power resource object" describes a software controllable power
 * plane, clock plane, or other resource used by a power managed device.
 * A device may rely on multiple power resources, and a power resource
 * may be shared by multiple devices.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include "sleep.h"
#include "internal.h"

#define PREFIX "ACPI: "

#define _COMPONENT			ACPI_POWER_COMPONENT
ACPI_MODULE_NAME("power");
#define ACPI_POWER_CLASS		"power_resource"
#define ACPI_POWER_DEVICE_NAME		"Power Resource"
#define ACPI_POWER_FILE_INFO		"info"
#define ACPI_POWER_FILE_STATUS		"state"
#define ACPI_POWER_RESOURCE_STATE_OFF	0x00
#define ACPI_POWER_RESOURCE_STATE_ON	0x01
#define ACPI_POWER_RESOURCE_STATE_UNKNOWN 0xFF

static int acpi_power_add(struct acpi_device *device);
static int acpi_power_remove(struct acpi_device *device, int type);

static const struct acpi_device_id power_device_ids[] = {
	{ACPI_POWER_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, power_device_ids);

#ifdef CONFIG_PM_SLEEP
static int acpi_power_resume(struct device *dev);
#endif
static SIMPLE_DEV_PM_OPS(acpi_power_pm, NULL, acpi_power_resume);

static struct acpi_driver acpi_power_driver = {
	.name = "power",
	.class = ACPI_POWER_CLASS,
	.ids = power_device_ids,
	.ops = {
		.add = acpi_power_add,
		.remove = acpi_power_remove,
		},
	.drv.pm = &acpi_power_pm,
};

/*
 * A power managed device
 * A device may rely on multiple power resources.
 * */
struct acpi_power_managed_device {
	struct device *dev; /* The physical device */
	acpi_handle *handle;
};

struct acpi_power_resource_device {
	struct acpi_power_managed_device *device;
	struct acpi_power_resource_device *next;
};

struct acpi_power_resource {
	struct acpi_device * device;
	acpi_bus_id name;
	u32 system_level;
	u32 order;
	unsigned int ref_count;
	struct mutex resource_lock;

	/* List of devices relying on this power resource */
	struct acpi_power_resource_device *devices;
	struct mutex devices_lock;
};

static struct list_head acpi_power_resource_list;

/* --------------------------------------------------------------------------
                             Power Resource Management
   -------------------------------------------------------------------------- */

static int
acpi_power_get_context(acpi_handle handle,
		       struct acpi_power_resource **resource)
{
	int result = 0;
	struct acpi_device *device = NULL;


	if (!resource)
		return -ENODEV;

	result = acpi_bus_get_device(handle, &device);
	if (result) {
		printk(KERN_WARNING PREFIX "Getting context [%p]\n", handle);
		return result;
	}

	*resource = acpi_driver_data(device);
	if (!*resource)
		return -ENODEV;

	return 0;
}

static int acpi_power_get_state(acpi_handle handle, int *state)
{
	acpi_status status = AE_OK;
	unsigned long long sta = 0;
	char node_name[5];
	struct acpi_buffer buffer = { sizeof(node_name), node_name };


	if (!handle || !state)
		return -EINVAL;

	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	*state = (sta & 0x01)?ACPI_POWER_RESOURCE_STATE_ON:
			      ACPI_POWER_RESOURCE_STATE_OFF;

	acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource [%s] is %s\n",
			  node_name,
				*state ? "on" : "off"));

	return 0;
}

static int acpi_power_get_list_state(struct acpi_handle_list *list, int *state)
{
	int cur_state;
	int i = 0;

	if (!list || !state)
		return -EINVAL;

	/* The state of the list is 'on' IFF all resources are 'on'. */

	for (i = 0; i < list->count; i++) {
		struct acpi_power_resource *resource;
		acpi_handle handle = list->handles[i];
		int result;

		result = acpi_power_get_context(handle, &resource);
		if (result)
			return result;

		mutex_lock(&resource->resource_lock);

		result = acpi_power_get_state(handle, &cur_state);

		mutex_unlock(&resource->resource_lock);

		if (result)
			return result;

		if (cur_state != ACPI_POWER_RESOURCE_STATE_ON)
			break;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource list is %s\n",
			  cur_state ? "on" : "off"));

	*state = cur_state;

	return 0;
}

/* Resume the device when all power resources in _PR0 are on */
static void acpi_power_on_device(struct acpi_power_managed_device *device)
{
	struct acpi_device *acpi_dev;
	acpi_handle handle = device->handle;
	int state;

	if (acpi_bus_get_device(handle, &acpi_dev))
		return;

	if(acpi_power_get_inferred_state(acpi_dev, &state))
		return;

	if (state == ACPI_STATE_D0 && pm_runtime_suspended(device->dev))
		pm_request_resume(device->dev);
}

static int __acpi_power_on(struct acpi_power_resource *resource)
{
	acpi_status status = AE_OK;

	status = acpi_evaluate_object(resource->device->handle, "_ON", NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	/* Update the power resource's _device_ power state */
	resource->device->power.state = ACPI_STATE_D0;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Power resource [%s] turned on\n",
			  resource->name));

	return 0;
}

static int acpi_power_on(acpi_handle handle)
{
	int result = 0;
	bool resume_device = false;
	struct acpi_power_resource *resource = NULL;
	struct acpi_power_resource_device *device_list;

	result = acpi_power_get_context(handle, &resource);
	if (result)
		return result;

	mutex_lock(&resource->resource_lock);

	if (resource->ref_count++) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] already on",
				  resource->name));
	} else {
		result = __acpi_power_on(resource);
		if (result)
			resource->ref_count--;
		else
			resume_device = true;
	}

	mutex_unlock(&resource->resource_lock);

	if (!resume_device)
		return result;

	mutex_lock(&resource->devices_lock);

	device_list = resource->devices;
	while (device_list) {
		acpi_power_on_device(device_list->device);
		device_list = device_list->next;
	}

	mutex_unlock(&resource->devices_lock);

	return result;
}

static int acpi_power_off(acpi_handle handle)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_power_resource *resource = NULL;

	result = acpi_power_get_context(handle, &resource);
	if (result)
		return result;

	mutex_lock(&resource->resource_lock);

	if (!resource->ref_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] already off",
				  resource->name));
		goto unlock;
	}

	if (--resource->ref_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] still in use\n",
				  resource->name));
		goto unlock;
	}

	status = acpi_evaluate_object(resource->device->handle, "_OFF", NULL, NULL);
	if (ACPI_FAILURE(status)) {
		result = -ENODEV;
	} else {
		/* Update the power resource's _device_ power state */
		resource->device->power.state = ACPI_STATE_D3;

		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] turned off\n",
				  resource->name));
	}

 unlock:
	mutex_unlock(&resource->resource_lock);

	return result;
}

static void __acpi_power_off_list(struct acpi_handle_list *list, int num_res)
{
	int i;

	for (i = num_res - 1; i >= 0 ; i--)
		acpi_power_off(list->handles[i]);
}

static void acpi_power_off_list(struct acpi_handle_list *list)
{
	__acpi_power_off_list(list, list->count);
}

static int acpi_power_on_list(struct acpi_handle_list *list)
{
	int result = 0;
	int i;

	for (i = 0; i < list->count; i++) {
		result = acpi_power_on(list->handles[i]);
		if (result) {
			__acpi_power_off_list(list, i);
			break;
		}
	}

	return result;
}

static void __acpi_power_resource_unregister_device(struct device *dev,
		acpi_handle res_handle)
{
	struct acpi_power_resource *resource = NULL;
	struct acpi_power_resource_device *prev, *curr;

	if (acpi_power_get_context(res_handle, &resource))
		return;

	mutex_lock(&resource->devices_lock);
	prev = NULL;
	curr = resource->devices;
	while (curr) {
		if (curr->device->dev == dev) {
			if (!prev)
				resource->devices = curr->next;
			else
				prev->next = curr->next;

			kfree(curr);
			break;
		}

		prev = curr;
		curr = curr->next;
	}
	mutex_unlock(&resource->devices_lock);
}

/* Unlink dev from all power resources in _PR0 */
void acpi_power_resource_unregister_device(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev;
	struct acpi_handle_list *list;
	int i;

	if (!dev || !handle)
		return;

	if (acpi_bus_get_device(handle, &acpi_dev))
		return;

	list = &acpi_dev->power.states[ACPI_STATE_D0].resources;

	for (i = 0; i < list->count; i++)
		__acpi_power_resource_unregister_device(dev,
			list->handles[i]);
}
EXPORT_SYMBOL_GPL(acpi_power_resource_unregister_device);

static int __acpi_power_resource_register_device(
	struct acpi_power_managed_device *powered_device, acpi_handle handle)
{
	struct acpi_power_resource *resource = NULL;
	struct acpi_power_resource_device *power_resource_device;
	int result;

	result = acpi_power_get_context(handle, &resource);
	if (result)
		return result;

	power_resource_device = kzalloc(
		sizeof(*power_resource_device), GFP_KERNEL);
	if (!power_resource_device)
		return -ENOMEM;

	power_resource_device->device = powered_device;

	mutex_lock(&resource->devices_lock);
	power_resource_device->next = resource->devices;
	resource->devices = power_resource_device;
	mutex_unlock(&resource->devices_lock);

	return 0;
}

/* Link dev to all power resources in _PR0 */
int acpi_power_resource_register_device(struct device *dev, acpi_handle handle)
{
	struct acpi_device *acpi_dev;
	struct acpi_handle_list *list;
	struct acpi_power_managed_device *powered_device;
	int i, ret;

	if (!dev || !handle)
		return -ENODEV;

	ret = acpi_bus_get_device(handle, &acpi_dev);
	if (ret)
		goto no_power_resource;

	if (!acpi_dev->power.flags.power_resources)
		goto no_power_resource;

	powered_device = kzalloc(sizeof(*powered_device), GFP_KERNEL);
	if (!powered_device)
		return -ENOMEM;

	powered_device->dev = dev;
	powered_device->handle = handle;

	list = &acpi_dev->power.states[ACPI_STATE_D0].resources;

	for (i = 0; i < list->count; i++) {
		ret = __acpi_power_resource_register_device(powered_device,
			list->handles[i]);

		if (ret) {
			acpi_power_resource_unregister_device(dev, handle);
			break;
		}
	}

	return ret;

no_power_resource:
	printk(KERN_DEBUG PREFIX "Invalid Power Resource to register!\n");
	return -ENODEV;
}
EXPORT_SYMBOL_GPL(acpi_power_resource_register_device);

/**
 * acpi_device_sleep_wake - execute _DSW (Device Sleep Wake) or (deprecated in
 *                          ACPI 3.0) _PSW (Power State Wake)
 * @dev: Device to handle.
 * @enable: 0 - disable, 1 - enable the wake capabilities of the device.
 * @sleep_state: Target sleep state of the system.
 * @dev_state: Target power state of the device.
 *
 * Execute _DSW (Device Sleep Wake) or (deprecated in ACPI 3.0) _PSW (Power
 * State Wake) for the device, if present.  On failure reset the device's
 * wakeup.flags.valid flag.
 *
 * RETURN VALUE:
 * 0 if either _DSW or _PSW has been successfully executed
 * 0 if neither _DSW nor _PSW has been found
 * -ENODEV if the execution of either _DSW or _PSW has failed
 */
int acpi_device_sleep_wake(struct acpi_device *dev,
                           int enable, int sleep_state, int dev_state)
{
	union acpi_object in_arg[3];
	struct acpi_object_list arg_list = { 3, in_arg };
	acpi_status status = AE_OK;

	/*
	 * Try to execute _DSW first.
	 *
	 * Three agruments are needed for the _DSW object:
	 * Argument 0: enable/disable the wake capabilities
	 * Argument 1: target system state
	 * Argument 2: target device state
	 * When _DSW object is called to disable the wake capabilities, maybe
	 * the first argument is filled. The values of the other two agruments
	 * are meaningless.
	 */
	in_arg[0].type = ACPI_TYPE_INTEGER;
	in_arg[0].integer.value = enable;
	in_arg[1].type = ACPI_TYPE_INTEGER;
	in_arg[1].integer.value = sleep_state;
	in_arg[2].type = ACPI_TYPE_INTEGER;
	in_arg[2].integer.value = dev_state;
	status = acpi_evaluate_object(dev->handle, "_DSW", &arg_list, NULL);
	if (ACPI_SUCCESS(status)) {
		return 0;
	} else if (status != AE_NOT_FOUND) {
		printk(KERN_ERR PREFIX "_DSW execution failed\n");
		dev->wakeup.flags.valid = 0;
		return -ENODEV;
	}

	/* Execute _PSW */
	arg_list.count = 1;
	in_arg[0].integer.value = enable;
	status = acpi_evaluate_object(dev->handle, "_PSW", &arg_list, NULL);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		printk(KERN_ERR PREFIX "_PSW execution failed\n");
		dev->wakeup.flags.valid = 0;
		return -ENODEV;
	}

	return 0;
}

/*
 * Prepare a wakeup device, two steps (Ref ACPI 2.0:P229):
 * 1. Power on the power resources required for the wakeup device 
 * 2. Execute _DSW (Device Sleep Wake) or (deprecated in ACPI 3.0) _PSW (Power
 *    State Wake) for the device, if present
 */
int acpi_enable_wakeup_device_power(struct acpi_device *dev, int sleep_state)
{
	int i, err = 0;

	if (!dev || !dev->wakeup.flags.valid)
		return -EINVAL;

	mutex_lock(&acpi_device_lock);

	if (dev->wakeup.prepare_count++)
		goto out;

	/* Open power resource */
	for (i = 0; i < dev->wakeup.resources.count; i++) {
		int ret = acpi_power_on(dev->wakeup.resources.handles[i]);
		if (ret) {
			printk(KERN_ERR PREFIX "Transition power state\n");
			dev->wakeup.flags.valid = 0;
			err = -ENODEV;
			goto err_out;
		}
	}

	/*
	 * Passing 3 as the third argument below means the device may be placed
	 * in arbitrary power state afterwards.
	 */
	err = acpi_device_sleep_wake(dev, 1, sleep_state, 3);

 err_out:
	if (err)
		dev->wakeup.prepare_count = 0;

 out:
	mutex_unlock(&acpi_device_lock);
	return err;
}

/*
 * Shutdown a wakeup device, counterpart of above method
 * 1. Execute _DSW (Device Sleep Wake) or (deprecated in ACPI 3.0) _PSW (Power
 *    State Wake) for the device, if present
 * 2. Shutdown down the power resources
 */
int acpi_disable_wakeup_device_power(struct acpi_device *dev)
{
	int i, err = 0;

	if (!dev || !dev->wakeup.flags.valid)
		return -EINVAL;

	mutex_lock(&acpi_device_lock);

	if (--dev->wakeup.prepare_count > 0)
		goto out;

	/*
	 * Executing the code below even if prepare_count is already zero when
	 * the function is called may be useful, for example for initialisation.
	 */
	if (dev->wakeup.prepare_count < 0)
		dev->wakeup.prepare_count = 0;

	err = acpi_device_sleep_wake(dev, 0, 0, 0);
	if (err)
		goto out;

	/* Close power resource */
	for (i = 0; i < dev->wakeup.resources.count; i++) {
		int ret = acpi_power_off(dev->wakeup.resources.handles[i]);
		if (ret) {
			printk(KERN_ERR PREFIX "Transition power state\n");
			dev->wakeup.flags.valid = 0;
			err = -ENODEV;
			goto out;
		}
	}

 out:
	mutex_unlock(&acpi_device_lock);
	return err;
}

/* --------------------------------------------------------------------------
                             Device Power Management
   -------------------------------------------------------------------------- */

int acpi_power_get_inferred_state(struct acpi_device *device, int *state)
{
	int result = 0;
	struct acpi_handle_list *list = NULL;
	int list_state = 0;
	int i = 0;

	if (!device || !state)
		return -EINVAL;

	/*
	 * We know a device's inferred power state when all the resources
	 * required for a given D-state are 'on'.
	 */
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3_HOT; i++) {
		list = &device->power.states[i].resources;
		if (list->count < 1)
			continue;

		result = acpi_power_get_list_state(list, &list_state);
		if (result)
			return result;

		if (list_state == ACPI_POWER_RESOURCE_STATE_ON) {
			*state = i;
			return 0;
		}
	}

	*state = ACPI_STATE_D3;
	return 0;
}

int acpi_power_on_resources(struct acpi_device *device, int state)
{
	if (!device || state < ACPI_STATE_D0 || state > ACPI_STATE_D3)
		return -EINVAL;

	return acpi_power_on_list(&device->power.states[state].resources);
}

int acpi_power_transition(struct acpi_device *device, int state)
{
	int result = 0;

	if (!device || (state < ACPI_STATE_D0) || (state > ACPI_STATE_D3_COLD))
		return -EINVAL;

	if (device->power.state == state)
		return 0;

	if ((device->power.state < ACPI_STATE_D0)
	    || (device->power.state > ACPI_STATE_D3_COLD))
		return -ENODEV;

	/* TBD: Resources must be ordered. */

	/*
	 * First we reference all power resources required in the target list
	 * (e.g. so the device doesn't lose power while transitioning).  Then,
	 * we dereference all power resources used in the current list.
	 */
	if (state < ACPI_STATE_D3_COLD)
		result = acpi_power_on_list(
			&device->power.states[state].resources);

	if (!result && device->power.state < ACPI_STATE_D3_COLD)
		acpi_power_off_list(
			&device->power.states[device->power.state].resources);

	/* We shouldn't change the state unless the above operations succeed. */
	device->power.state = result ? ACPI_STATE_UNKNOWN : state;

	return result;
}

/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */

static int acpi_power_add(struct acpi_device *device)
{
	int result = 0, state;
	acpi_status status = AE_OK;
	struct acpi_power_resource *resource = NULL;
	union acpi_object acpi_object;
	struct acpi_buffer buffer = { sizeof(acpi_object), &acpi_object };


	if (!device)
		return -EINVAL;

	resource = kzalloc(sizeof(struct acpi_power_resource), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	resource->device = device;
	mutex_init(&resource->resource_lock);
	mutex_init(&resource->devices_lock);
	strcpy(resource->name, device->pnp.bus_id);
	strcpy(acpi_device_name(device), ACPI_POWER_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_POWER_CLASS);
	device->driver_data = resource;

	/* Evalute the object to get the system level and resource order. */
	status = acpi_evaluate_object(device->handle, NULL, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		result = -ENODEV;
		goto end;
	}
	resource->system_level = acpi_object.power_resource.system_level;
	resource->order = acpi_object.power_resource.resource_order;

	result = acpi_power_get_state(device->handle, &state);
	if (result)
		goto end;

	switch (state) {
	case ACPI_POWER_RESOURCE_STATE_ON:
		device->power.state = ACPI_STATE_D0;
		break;
	case ACPI_POWER_RESOURCE_STATE_OFF:
		device->power.state = ACPI_STATE_D3;
		break;
	default:
		device->power.state = ACPI_STATE_UNKNOWN;
		break;
	}

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n", acpi_device_name(device),
	       acpi_device_bid(device), state ? "on" : "off");

      end:
	if (result)
		kfree(resource);

	return result;
}

static int acpi_power_remove(struct acpi_device *device, int type)
{
	struct acpi_power_resource *resource;

	if (!device)
		return -EINVAL;

	resource = acpi_driver_data(device);
	if (!resource)
		return -EINVAL;

	kfree(resource);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int acpi_power_resume(struct device *dev)
{
	int result = 0, state;
	struct acpi_device *device;
	struct acpi_power_resource *resource;

	if (!dev)
		return -EINVAL;

	device = to_acpi_device(dev);
	resource = acpi_driver_data(device);
	if (!resource)
		return -EINVAL;

	mutex_lock(&resource->resource_lock);

	result = acpi_power_get_state(device->handle, &state);
	if (result)
		goto unlock;

	if (state == ACPI_POWER_RESOURCE_STATE_OFF && resource->ref_count)
		result = __acpi_power_on(resource);

 unlock:
	mutex_unlock(&resource->resource_lock);

	return result;
}
#endif

int __init acpi_power_init(void)
{
	INIT_LIST_HEAD(&acpi_power_resource_list);
	return acpi_bus_register_driver(&acpi_power_driver);
}
