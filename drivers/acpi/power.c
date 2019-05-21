/*
 * drivers/acpi/power.c - ACPI Power Resources management.
 *
 * Copyright (C) 2001 - 2015 Intel Corp.
 * Author: Andy Grover <andrew.grover@intel.com>
 * Author: Paul Diefenbaugh <paul.s.diefenbaugh@intel.com>
 * Author: Rafael J. Wysocki <rafael.j.wysocki@intel.com>
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
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

/*
 * ACPI power-managed devices may be controlled in two ways:
 * 1. via "Device Specific (D-State) Control"
 * 2. via "Power Resource Control".
 * The code below deals with ACPI Power Resources control.
 * 
 * An ACPI "power resource object" represents a software controllable power
 * plane, clock plane, or other resource depended on by a device.
 *
 * A device may rely on multiple power resources, and a power resource
 * may be shared by multiple devices.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/sysfs.h>
#include <linux/acpi.h>
#include "sleep.h"
#include "internal.h"

#define _COMPONENT			ACPI_POWER_COMPONENT
ACPI_MODULE_NAME("power");
#define ACPI_POWER_CLASS		"power_resource"
#define ACPI_POWER_DEVICE_NAME		"Power Resource"
#define ACPI_POWER_FILE_INFO		"info"
#define ACPI_POWER_FILE_STATUS		"state"
#define ACPI_POWER_RESOURCE_STATE_OFF	0x00
#define ACPI_POWER_RESOURCE_STATE_ON	0x01
#define ACPI_POWER_RESOURCE_STATE_UNKNOWN 0xFF

struct acpi_power_resource {
	struct acpi_device device;
	struct list_head list_node;
	char *name;
	u32 system_level;
	u32 order;
	unsigned int ref_count;
	bool wakeup_enabled;
	struct mutex resource_lock;
};

struct acpi_power_resource_entry {
	struct list_head node;
	struct acpi_power_resource *resource;
};

static LIST_HEAD(acpi_power_resource_list);
static DEFINE_MUTEX(power_resource_list_lock);

/* --------------------------------------------------------------------------
                             Power Resource Management
   -------------------------------------------------------------------------- */

static inline
struct acpi_power_resource *to_power_resource(struct acpi_device *device)
{
	return container_of(device, struct acpi_power_resource, device);
}

static struct acpi_power_resource *acpi_power_get_context(acpi_handle handle)
{
	struct acpi_device *device;

	if (acpi_bus_get_device(handle, &device))
		return NULL;

	return to_power_resource(device);
}

static int acpi_power_resources_list_add(acpi_handle handle,
					 struct list_head *list)
{
	struct acpi_power_resource *resource = acpi_power_get_context(handle);
	struct acpi_power_resource_entry *entry;

	if (!resource || !list)
		return -EINVAL;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return -ENOMEM;

	entry->resource = resource;
	if (!list_empty(list)) {
		struct acpi_power_resource_entry *e;

		list_for_each_entry(e, list, node)
			if (e->resource->order > resource->order) {
				list_add_tail(&entry->node, &e->node);
				return 0;
			}
	}
	list_add_tail(&entry->node, list);
	return 0;
}

void acpi_power_resources_list_free(struct list_head *list)
{
	struct acpi_power_resource_entry *entry, *e;

	list_for_each_entry_safe(entry, e, list, node) {
		list_del(&entry->node);
		kfree(entry);
	}
}

static bool acpi_power_resource_is_dup(union acpi_object *package,
				       unsigned int start, unsigned int i)
{
	acpi_handle rhandle, dup;
	unsigned int j;

	/* The caller is expected to check the package element types */
	rhandle = package->package.elements[i].reference.handle;
	for (j = start; j < i; j++) {
		dup = package->package.elements[j].reference.handle;
		if (dup == rhandle)
			return true;
	}

	return false;
}

int acpi_extract_power_resources(union acpi_object *package, unsigned int start,
				 struct list_head *list)
{
	unsigned int i;
	int err = 0;

	for (i = start; i < package->package.count; i++) {
		union acpi_object *element = &package->package.elements[i];
		acpi_handle rhandle;

		if (element->type != ACPI_TYPE_LOCAL_REFERENCE) {
			err = -ENODATA;
			break;
		}
		rhandle = element->reference.handle;
		if (!rhandle) {
			err = -ENODEV;
			break;
		}

		/* Some ACPI tables contain duplicate power resource references */
		if (acpi_power_resource_is_dup(package, start, i))
			continue;

		err = acpi_add_power_resource(rhandle);
		if (err)
			break;

		err = acpi_power_resources_list_add(rhandle, list);
		if (err)
			break;
	}
	if (err)
		acpi_power_resources_list_free(list);

	return err;
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

static int acpi_power_get_list_state(struct list_head *list, int *state)
{
	struct acpi_power_resource_entry *entry;
	int cur_state;

	if (!list || !state)
		return -EINVAL;

	/* The state of the list is 'on' IFF all resources are 'on'. */
	cur_state = 0;
	list_for_each_entry(entry, list, node) {
		struct acpi_power_resource *resource = entry->resource;
		acpi_handle handle = resource->device.handle;
		int result;

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

static int __acpi_power_on(struct acpi_power_resource *resource)
{
	acpi_status status = AE_OK;

	status = acpi_evaluate_object(resource->device.handle, "_ON", NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Power resource [%s] turned on\n",
			  resource->name));

	return 0;
}

static int acpi_power_on_unlocked(struct acpi_power_resource *resource)
{
	int result = 0;

	if (resource->ref_count++) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] already on\n",
				  resource->name));
	} else {
		result = __acpi_power_on(resource);
		if (result)
			resource->ref_count--;
	}
	return result;
}

static int acpi_power_on(struct acpi_power_resource *resource)
{
	int result;

	mutex_lock(&resource->resource_lock);
	result = acpi_power_on_unlocked(resource);
	mutex_unlock(&resource->resource_lock);
	return result;
}

static int __acpi_power_off(struct acpi_power_resource *resource)
{
	acpi_status status;

	status = acpi_evaluate_object(resource->device.handle, "_OFF",
				      NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Power resource [%s] turned off\n",
			  resource->name));
	return 0;
}

static int acpi_power_off_unlocked(struct acpi_power_resource *resource)
{
	int result = 0;

	if (!resource->ref_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] already off\n",
				  resource->name));
		return 0;
	}

	if (--resource->ref_count) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Power resource [%s] still in use\n",
				  resource->name));
	} else {
		result = __acpi_power_off(resource);
		if (result)
			resource->ref_count++;
	}
	return result;
}

static int acpi_power_off(struct acpi_power_resource *resource)
{
	int result;

	mutex_lock(&resource->resource_lock);
	result = acpi_power_off_unlocked(resource);
	mutex_unlock(&resource->resource_lock);
	return result;
}

static int acpi_power_off_list(struct list_head *list)
{
	struct acpi_power_resource_entry *entry;
	int result = 0;

	list_for_each_entry_reverse(entry, list, node) {
		result = acpi_power_off(entry->resource);
		if (result)
			goto err;
	}
	return 0;

 err:
	list_for_each_entry_continue(entry, list, node)
		acpi_power_on(entry->resource);

	return result;
}

static int acpi_power_on_list(struct list_head *list)
{
	struct acpi_power_resource_entry *entry;
	int result = 0;

	list_for_each_entry(entry, list, node) {
		result = acpi_power_on(entry->resource);
		if (result)
			goto err;
	}
	return 0;

 err:
	list_for_each_entry_continue_reverse(entry, list, node)
		acpi_power_off(entry->resource);

	return result;
}

static struct attribute *attrs[] = {
	NULL,
};

static const struct attribute_group attr_groups[] = {
	[ACPI_STATE_D0] = {
		.name = "power_resources_D0",
		.attrs = attrs,
	},
	[ACPI_STATE_D1] = {
		.name = "power_resources_D1",
		.attrs = attrs,
	},
	[ACPI_STATE_D2] = {
		.name = "power_resources_D2",
		.attrs = attrs,
	},
	[ACPI_STATE_D3_HOT] = {
		.name = "power_resources_D3hot",
		.attrs = attrs,
	},
};

static const struct attribute_group wakeup_attr_group = {
	.name = "power_resources_wakeup",
	.attrs = attrs,
};

static void acpi_power_hide_list(struct acpi_device *adev,
				 struct list_head *resources,
				 const struct attribute_group *attr_group)
{
	struct acpi_power_resource_entry *entry;

	if (list_empty(resources))
		return;

	list_for_each_entry_reverse(entry, resources, node) {
		struct acpi_device *res_dev = &entry->resource->device;

		sysfs_remove_link_from_group(&adev->dev.kobj,
					     attr_group->name,
					     dev_name(&res_dev->dev));
	}
	sysfs_remove_group(&adev->dev.kobj, attr_group);
}

static void acpi_power_expose_list(struct acpi_device *adev,
				   struct list_head *resources,
				   const struct attribute_group *attr_group)
{
	struct acpi_power_resource_entry *entry;
	int ret;

	if (list_empty(resources))
		return;

	ret = sysfs_create_group(&adev->dev.kobj, attr_group);
	if (ret)
		return;

	list_for_each_entry(entry, resources, node) {
		struct acpi_device *res_dev = &entry->resource->device;

		ret = sysfs_add_link_to_group(&adev->dev.kobj,
					      attr_group->name,
					      &res_dev->dev.kobj,
					      dev_name(&res_dev->dev));
		if (ret) {
			acpi_power_hide_list(adev, resources, attr_group);
			break;
		}
	}
}

static void acpi_power_expose_hide(struct acpi_device *adev,
				   struct list_head *resources,
				   const struct attribute_group *attr_group,
				   bool expose)
{
	if (expose)
		acpi_power_expose_list(adev, resources, attr_group);
	else
		acpi_power_hide_list(adev, resources, attr_group);
}

void acpi_power_add_remove_device(struct acpi_device *adev, bool add)
{
	int state;

	if (adev->wakeup.flags.valid)
		acpi_power_expose_hide(adev, &adev->wakeup.resources,
				       &wakeup_attr_group, add);

	if (!adev->power.flags.power_resources)
		return;

	for (state = ACPI_STATE_D0; state <= ACPI_STATE_D3_HOT; state++)
		acpi_power_expose_hide(adev,
				       &adev->power.states[state].resources,
				       &attr_groups[state], add);
}

int acpi_power_wakeup_list_init(struct list_head *list, int *system_level_p)
{
	struct acpi_power_resource_entry *entry;
	int system_level = 5;

	list_for_each_entry(entry, list, node) {
		struct acpi_power_resource *resource = entry->resource;
		acpi_handle handle = resource->device.handle;
		int result;
		int state;

		mutex_lock(&resource->resource_lock);

		result = acpi_power_get_state(handle, &state);
		if (result) {
			mutex_unlock(&resource->resource_lock);
			return result;
		}
		if (state == ACPI_POWER_RESOURCE_STATE_ON) {
			resource->ref_count++;
			resource->wakeup_enabled = true;
		}
		if (system_level > resource->system_level)
			system_level = resource->system_level;

		mutex_unlock(&resource->resource_lock);
	}
	*system_level_p = system_level;
	return 0;
}

/* --------------------------------------------------------------------------
                             Device Power Management
   -------------------------------------------------------------------------- */

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
	 * Three arguments are needed for the _DSW object:
	 * Argument 0: enable/disable the wake capabilities
	 * Argument 1: target system state
	 * Argument 2: target device state
	 * When _DSW object is called to disable the wake capabilities, maybe
	 * the first argument is filled. The values of the other two arguments
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
	status = acpi_execute_simple_method(dev->handle, "_PSW", enable);
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
	struct acpi_power_resource_entry *entry;
	int err = 0;

	if (!dev || !dev->wakeup.flags.valid)
		return -EINVAL;

	mutex_lock(&acpi_device_lock);

	if (dev->wakeup.prepare_count++)
		goto out;

	list_for_each_entry(entry, &dev->wakeup.resources, node) {
		struct acpi_power_resource *resource = entry->resource;

		mutex_lock(&resource->resource_lock);

		if (!resource->wakeup_enabled) {
			err = acpi_power_on_unlocked(resource);
			if (!err)
				resource->wakeup_enabled = true;
		}

		mutex_unlock(&resource->resource_lock);

		if (err) {
			dev_err(&dev->dev,
				"Cannot turn wakeup power resources on\n");
			dev->wakeup.flags.valid = 0;
			goto out;
		}
	}
	/*
	 * Passing 3 as the third argument below means the device may be
	 * put into arbitrary power state afterward.
	 */
	err = acpi_device_sleep_wake(dev, 1, sleep_state, 3);
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
	struct acpi_power_resource_entry *entry;
	int err = 0;

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

	list_for_each_entry(entry, &dev->wakeup.resources, node) {
		struct acpi_power_resource *resource = entry->resource;

		mutex_lock(&resource->resource_lock);

		if (resource->wakeup_enabled) {
			err = acpi_power_off_unlocked(resource);
			if (!err)
				resource->wakeup_enabled = false;
		}

		mutex_unlock(&resource->resource_lock);

		if (err) {
			dev_err(&dev->dev,
				"Cannot turn wakeup power resources off\n");
			dev->wakeup.flags.valid = 0;
			break;
		}
	}

 out:
	mutex_unlock(&acpi_device_lock);
	return err;
}

int acpi_power_get_inferred_state(struct acpi_device *device, int *state)
{
	int result = 0;
	int list_state = 0;
	int i = 0;

	if (!device || !state)
		return -EINVAL;

	/*
	 * We know a device's inferred power state when all the resources
	 * required for a given D-state are 'on'.
	 */
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3_HOT; i++) {
		struct list_head *list = &device->power.states[i].resources;

		if (list_empty(list))
			continue;

		result = acpi_power_get_list_state(list, &list_state);
		if (result)
			return result;

		if (list_state == ACPI_POWER_RESOURCE_STATE_ON) {
			*state = i;
			return 0;
		}
	}

	*state = device->power.states[ACPI_STATE_D3_COLD].flags.valid ?
		ACPI_STATE_D3_COLD : ACPI_STATE_D3_HOT;
	return 0;
}

int acpi_power_on_resources(struct acpi_device *device, int state)
{
	if (!device || state < ACPI_STATE_D0 || state > ACPI_STATE_D3_HOT)
		return -EINVAL;

	return acpi_power_on_list(&device->power.states[state].resources);
}

int acpi_power_transition(struct acpi_device *device, int state)
{
	int result = 0;

	if (!device || (state < ACPI_STATE_D0) || (state > ACPI_STATE_D3_COLD))
		return -EINVAL;

	if (device->power.state == state || !device->flags.power_manageable)
		return 0;

	if ((device->power.state < ACPI_STATE_D0)
	    || (device->power.state > ACPI_STATE_D3_COLD))
		return -ENODEV;

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

static void acpi_release_power_resource(struct device *dev)
{
	struct acpi_device *device = to_acpi_device(dev);
	struct acpi_power_resource *resource;

	resource = container_of(device, struct acpi_power_resource, device);

	mutex_lock(&power_resource_list_lock);
	list_del(&resource->list_node);
	mutex_unlock(&power_resource_list_lock);

	acpi_free_pnp_ids(&device->pnp);
	kfree(resource);
}

static ssize_t acpi_power_in_use_show(struct device *dev,
				      struct device_attribute *attr,
				      char *buf) {
	struct acpi_power_resource *resource;

	resource = to_power_resource(to_acpi_device(dev));
	return sprintf(buf, "%u\n", !!resource->ref_count);
}
static DEVICE_ATTR(resource_in_use, 0444, acpi_power_in_use_show, NULL);

static void acpi_power_sysfs_remove(struct acpi_device *device)
{
	device_remove_file(&device->dev, &dev_attr_resource_in_use);
}

static void acpi_power_add_resource_to_list(struct acpi_power_resource *resource)
{
	mutex_lock(&power_resource_list_lock);

	if (!list_empty(&acpi_power_resource_list)) {
		struct acpi_power_resource *r;

		list_for_each_entry(r, &acpi_power_resource_list, list_node)
			if (r->order > resource->order) {
				list_add_tail(&resource->list_node, &r->list_node);
				goto out;
			}
	}
	list_add_tail(&resource->list_node, &acpi_power_resource_list);

 out:
	mutex_unlock(&power_resource_list_lock);
}

int acpi_add_power_resource(acpi_handle handle)
{
	struct acpi_power_resource *resource;
	struct acpi_device *device = NULL;
	union acpi_object acpi_object;
	struct acpi_buffer buffer = { sizeof(acpi_object), &acpi_object };
	acpi_status status;
	int state, result = -ENODEV;

	acpi_bus_get_device(handle, &device);
	if (device)
		return 0;

	resource = kzalloc(sizeof(*resource), GFP_KERNEL);
	if (!resource)
		return -ENOMEM;

	device = &resource->device;
	acpi_init_device_object(device, handle, ACPI_BUS_TYPE_POWER,
				ACPI_STA_DEFAULT);
	mutex_init(&resource->resource_lock);
	INIT_LIST_HEAD(&resource->list_node);
	resource->name = device->pnp.bus_id;
	strcpy(acpi_device_name(device), ACPI_POWER_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_POWER_CLASS);
	device->power.state = ACPI_STATE_UNKNOWN;

	/* Evalute the object to get the system level and resource order. */
	status = acpi_evaluate_object(handle, NULL, NULL, &buffer);
	if (ACPI_FAILURE(status))
		goto err;

	resource->system_level = acpi_object.power_resource.system_level;
	resource->order = acpi_object.power_resource.resource_order;

	result = acpi_power_get_state(handle, &state);
	if (result)
		goto err;

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n", acpi_device_name(device),
	       acpi_device_bid(device), state ? "on" : "off");

	device->flags.match_driver = true;
	result = acpi_device_add(device, acpi_release_power_resource);
	if (result)
		goto err;

	if (!device_create_file(&device->dev, &dev_attr_resource_in_use))
		device->remove = acpi_power_sysfs_remove;

	acpi_power_add_resource_to_list(resource);
	acpi_device_add_finalize(device);
	return 0;

 err:
	acpi_release_power_resource(&device->dev);
	return result;
}

#ifdef CONFIG_ACPI_SLEEP
void acpi_resume_power_resources(void)
{
	struct acpi_power_resource *resource;

	mutex_lock(&power_resource_list_lock);

	list_for_each_entry(resource, &acpi_power_resource_list, list_node) {
		int result, state;

		mutex_lock(&resource->resource_lock);

		result = acpi_power_get_state(resource->device.handle, &state);
		if (result) {
			mutex_unlock(&resource->resource_lock);
			continue;
		}

		if (state == ACPI_POWER_RESOURCE_STATE_OFF
		    && resource->ref_count) {
			dev_info(&resource->device.dev, "Turning ON\n");
			__acpi_power_on(resource);
		}

		mutex_unlock(&resource->resource_lock);
	}

	mutex_unlock(&power_resource_list_lock);
}

void acpi_turn_off_unused_power_resources(void)
{
	struct acpi_power_resource *resource;

	mutex_lock(&power_resource_list_lock);

	list_for_each_entry_reverse(resource, &acpi_power_resource_list, list_node) {
		int result, state;

		mutex_lock(&resource->resource_lock);

		result = acpi_power_get_state(resource->device.handle, &state);
		if (result) {
			mutex_unlock(&resource->resource_lock);
			continue;
		}

		if (state == ACPI_POWER_RESOURCE_STATE_ON
		    && !resource->ref_count) {
			dev_info(&resource->device.dev, "Turning OFF\n");
			__acpi_power_off(resource);
		}

		mutex_unlock(&resource->resource_lock);
	}

	mutex_unlock(&power_resource_list_lock);
}
#endif
