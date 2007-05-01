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
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define _COMPONENT		ACPI_POWER_COMPONENT
ACPI_MODULE_NAME("power");
#define ACPI_POWER_COMPONENT		0x00800000
#define ACPI_POWER_CLASS		"power_resource"
#define ACPI_POWER_DEVICE_NAME		"Power Resource"
#define ACPI_POWER_FILE_INFO		"info"
#define ACPI_POWER_FILE_STATUS		"state"
#define ACPI_POWER_RESOURCE_STATE_OFF	0x00
#define ACPI_POWER_RESOURCE_STATE_ON	0x01
#define ACPI_POWER_RESOURCE_STATE_UNKNOWN 0xFF
static int acpi_power_add(struct acpi_device *device);
static int acpi_power_remove(struct acpi_device *device, int type);
static int acpi_power_resume(struct acpi_device *device);
static int acpi_power_open_fs(struct inode *inode, struct file *file);

static struct acpi_driver acpi_power_driver = {
	.name = "power",
	.class = ACPI_POWER_CLASS,
	.ids = ACPI_POWER_HID,
	.ops = {
		.add = acpi_power_add,
		.remove = acpi_power_remove,
		.resume = acpi_power_resume,
		},
};

struct acpi_power_reference {
	struct list_head node;
	struct acpi_device *device;
};

struct acpi_power_resource {
	struct acpi_device * device;
	acpi_bus_id name;
	u32 system_level;
	u32 order;
	int state;
	struct mutex resource_lock;
	struct list_head reference;
};

static struct list_head acpi_power_resource_list;

static const struct file_operations acpi_power_fops = {
	.open = acpi_power_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

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
	if (!resource)
		return -ENODEV;

	return 0;
}

static int acpi_power_get_state(struct acpi_power_resource *resource)
{
	acpi_status status = AE_OK;
	unsigned long sta = 0;


	if (!resource)
		return -EINVAL;

	status = acpi_evaluate_integer(resource->device->handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if (sta & 0x01)
		resource->state = ACPI_POWER_RESOURCE_STATE_ON;
	else
		resource->state = ACPI_POWER_RESOURCE_STATE_OFF;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource [%s] is %s\n",
			  resource->name, resource->state ? "on" : "off"));

	return 0;
}

static int acpi_power_get_list_state(struct acpi_handle_list *list, int *state)
{
	int result = 0;
	struct acpi_power_resource *resource = NULL;
	u32 i = 0;


	if (!list || !state)
		return -EINVAL;

	/* The state of the list is 'on' IFF all resources are 'on'. */

	for (i = 0; i < list->count; i++) {
		result = acpi_power_get_context(list->handles[i], &resource);
		if (result)
			return result;
		result = acpi_power_get_state(resource);
		if (result)
			return result;

		*state = resource->state;

		if (*state != ACPI_POWER_RESOURCE_STATE_ON)
			break;
	}

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource list is %s\n",
			  *state ? "on" : "off"));

	return result;
}

static int acpi_power_on(acpi_handle handle, struct acpi_device *dev)
{
	int result = 0;
	int found = 0;
	acpi_status status = AE_OK;
	struct acpi_power_resource *resource = NULL;
	struct list_head *node, *next;
	struct acpi_power_reference *ref;


	result = acpi_power_get_context(handle, &resource);
	if (result)
		return result;

	mutex_lock(&resource->resource_lock);
	list_for_each_safe(node, next, &resource->reference) {
		ref = container_of(node, struct acpi_power_reference, node);
		if (dev->handle == ref->device->handle) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] already referenced by resource [%s]\n",
				  dev->pnp.bus_id, resource->name));
			found = 1;
			break;
		}
	}

	if (!found) {
		ref = kmalloc(sizeof (struct acpi_power_reference),
		    irqs_disabled() ? GFP_ATOMIC : GFP_KERNEL);
		if (!ref) {
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "kmalloc() failed\n"));
			mutex_unlock(&resource->resource_lock);
			return -ENOMEM;
		}
		list_add_tail(&ref->node, &resource->reference);
		ref->device = dev;
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] added to resource [%s] references\n",
			  dev->pnp.bus_id, resource->name));
	}
	mutex_unlock(&resource->resource_lock);

	if (resource->state == ACPI_POWER_RESOURCE_STATE_ON) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource [%s] already on\n",
				  resource->name));
		return 0;
	}

	status = acpi_evaluate_object(resource->device->handle, "_ON", NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	result = acpi_power_get_state(resource);
	if (result)
		return result;
	if (resource->state != ACPI_POWER_RESOURCE_STATE_ON)
		return -ENOEXEC;

	/* Update the power resource's _device_ power state */
	resource->device->power.state = ACPI_STATE_D0;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource [%s] turned on\n",
			  resource->name));
	return 0;
}

static int acpi_power_off_device(acpi_handle handle, struct acpi_device *dev)
{
	int result = 0;
	acpi_status status = AE_OK;
	struct acpi_power_resource *resource = NULL;
	struct list_head *node, *next;
	struct acpi_power_reference *ref;


	result = acpi_power_get_context(handle, &resource);
	if (result)
		return result;

	mutex_lock(&resource->resource_lock);
	list_for_each_safe(node, next, &resource->reference) {
		ref = container_of(node, struct acpi_power_reference, node);
		if (dev->handle == ref->device->handle) {
			list_del(&ref->node);
			kfree(ref);
			ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device [%s] removed from resource [%s] references\n",
			    dev->pnp.bus_id, resource->name));
			break;
		}
	}

	if (!list_empty(&resource->reference)) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Cannot turn resource [%s] off - resource is in use\n",
		    resource->name));
		mutex_unlock(&resource->resource_lock);
		return 0;
	}
	mutex_unlock(&resource->resource_lock);

	if (resource->state == ACPI_POWER_RESOURCE_STATE_OFF) {
		ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource [%s] already off\n",
				  resource->name));
		return 0;
	}

	status = acpi_evaluate_object(resource->device->handle, "_OFF", NULL, NULL);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	result = acpi_power_get_state(resource);
	if (result)
		return result;
	if (resource->state != ACPI_POWER_RESOURCE_STATE_OFF)
		return -ENOEXEC;

	/* Update the power resource's _device_ power state */
	resource->device->power.state = ACPI_STATE_D3;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Resource [%s] turned off\n",
			  resource->name));

	return 0;
}

/*
 * Prepare a wakeup device, two steps (Ref ACPI 2.0:P229):
 * 1. Power on the power resources required for the wakeup device 
 * 2. Enable _PSW (power state wake) for the device if present
 */
int acpi_enable_wakeup_device_power(struct acpi_device *dev)
{
	union acpi_object arg = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg };
	acpi_status status = AE_OK;
	int i;
	int ret = 0;

	if (!dev || !dev->wakeup.flags.valid)
		return -1;

	arg.integer.value = 1;
	/* Open power resource */
	for (i = 0; i < dev->wakeup.resources.count; i++) {
		ret = acpi_power_on(dev->wakeup.resources.handles[i], dev);
		if (ret) {
			printk(KERN_ERR PREFIX "Transition power state\n");
			dev->wakeup.flags.valid = 0;
			return -1;
		}
	}

	/* Execute PSW */
	status = acpi_evaluate_object(dev->handle, "_PSW", &arg_list, NULL);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		printk(KERN_ERR PREFIX "Evaluate _PSW\n");
		dev->wakeup.flags.valid = 0;
		ret = -1;
	}

	return ret;
}

/*
 * Shutdown a wakeup device, counterpart of above method
 * 1. Disable _PSW (power state wake)
 * 2. Shutdown down the power resources
 */
int acpi_disable_wakeup_device_power(struct acpi_device *dev)
{
	union acpi_object arg = { ACPI_TYPE_INTEGER };
	struct acpi_object_list arg_list = { 1, &arg };
	acpi_status status = AE_OK;
	int i;
	int ret = 0;


	if (!dev || !dev->wakeup.flags.valid)
		return -1;

	arg.integer.value = 0;
	/* Execute PSW */
	status = acpi_evaluate_object(dev->handle, "_PSW", &arg_list, NULL);
	if (ACPI_FAILURE(status) && (status != AE_NOT_FOUND)) {
		printk(KERN_ERR PREFIX "Evaluate _PSW\n");
		dev->wakeup.flags.valid = 0;
		return -1;
	}

	/* Close power resource */
	for (i = 0; i < dev->wakeup.resources.count; i++) {
		ret = acpi_power_off_device(dev->wakeup.resources.handles[i], dev);
		if (ret) {
			printk(KERN_ERR PREFIX "Transition power state\n");
			dev->wakeup.flags.valid = 0;
			return -1;
		}
	}

	return ret;
}

/* --------------------------------------------------------------------------
                             Device Power Management
   -------------------------------------------------------------------------- */

int acpi_power_get_inferred_state(struct acpi_device *device)
{
	int result = 0;
	struct acpi_handle_list *list = NULL;
	int list_state = 0;
	int i = 0;


	if (!device)
		return -EINVAL;

	device->power.state = ACPI_STATE_UNKNOWN;

	/*
	 * We know a device's inferred power state when all the resources
	 * required for a given D-state are 'on'.
	 */
	for (i = ACPI_STATE_D0; i < ACPI_STATE_D3; i++) {
		list = &device->power.states[i].resources;
		if (list->count < 1)
			continue;

		result = acpi_power_get_list_state(list, &list_state);
		if (result)
			return result;

		if (list_state == ACPI_POWER_RESOURCE_STATE_ON) {
			device->power.state = i;
			return 0;
		}
	}

	device->power.state = ACPI_STATE_D3;

	return 0;
}

int acpi_power_transition(struct acpi_device *device, int state)
{
	int result = 0;
	struct acpi_handle_list *cl = NULL;	/* Current Resources */
	struct acpi_handle_list *tl = NULL;	/* Target Resources */
	int i = 0;


	if (!device || (state < ACPI_STATE_D0) || (state > ACPI_STATE_D3))
		return -EINVAL;

	if ((device->power.state < ACPI_STATE_D0)
	    || (device->power.state > ACPI_STATE_D3))
		return -ENODEV;

	cl = &device->power.states[device->power.state].resources;
	tl = &device->power.states[state].resources;

	if (!cl->count && !tl->count) {
		result = -ENODEV;
		goto end;
	}

	/* TBD: Resources must be ordered. */

	/*
	 * First we reference all power resources required in the target list
	 * (e.g. so the device doesn't lose power while transitioning).
	 */
	for (i = 0; i < tl->count; i++) {
		result = acpi_power_on(tl->handles[i], device);
		if (result)
			goto end;
	}

	if (device->power.state == state) {
		goto end;
	}

	/*
	 * Then we dereference all power resources used in the current list.
	 */
	for (i = 0; i < cl->count; i++) {
		result = acpi_power_off_device(cl->handles[i], device);
		if (result)
			goto end;
	}

     end:
	if (result) {
		device->power.state = ACPI_STATE_UNKNOWN;
		printk(KERN_WARNING PREFIX "Transitioning device [%s] to D%d\n",
			      device->pnp.bus_id, state);
	} else {
	/* We shouldn't change the state till all above operations succeed */
		device->power.state = state;
	}

	return result;
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc)
   -------------------------------------------------------------------------- */

static struct proc_dir_entry *acpi_power_dir;

static int acpi_power_seq_show(struct seq_file *seq, void *offset)
{
	int count = 0;
	int result = 0;
	struct acpi_power_resource *resource = NULL;
	struct list_head *node, *next;
	struct acpi_power_reference *ref;


	resource = seq->private;

	if (!resource)
		goto end;

	result = acpi_power_get_state(resource);
	if (result)
		goto end;

	seq_puts(seq, "state:                   ");
	switch (resource->state) {
	case ACPI_POWER_RESOURCE_STATE_ON:
		seq_puts(seq, "on\n");
		break;
	case ACPI_POWER_RESOURCE_STATE_OFF:
		seq_puts(seq, "off\n");
		break;
	default:
		seq_puts(seq, "unknown\n");
		break;
	}

	mutex_lock(&resource->resource_lock);
	list_for_each_safe(node, next, &resource->reference) {
		ref = container_of(node, struct acpi_power_reference, node);
		count++;
	}
	mutex_unlock(&resource->resource_lock);

	seq_printf(seq, "system level:            S%d\n"
		   "order:                   %d\n"
		   "reference count:         %d\n",
		   resource->system_level,
		   resource->order, count);

      end:
	return 0;
}

static int acpi_power_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_power_seq_show, PDE(inode)->data);
}

static int acpi_power_add_fs(struct acpi_device *device)
{
	struct proc_dir_entry *entry = NULL;


	if (!device)
		return -EINVAL;

	if (!acpi_device_dir(device)) {
		acpi_device_dir(device) = proc_mkdir(acpi_device_bid(device),
						     acpi_power_dir);
		if (!acpi_device_dir(device))
			return -ENODEV;
	}

	/* 'status' [R] */
	entry = create_proc_entry(ACPI_POWER_FILE_STATUS,
				  S_IRUGO, acpi_device_dir(device));
	if (!entry)
		return -EIO;
	else {
		entry->proc_fops = &acpi_power_fops;
		entry->data = acpi_driver_data(device);
	}

	return 0;
}

static int acpi_power_remove_fs(struct acpi_device *device)
{

	if (acpi_device_dir(device)) {
		remove_proc_entry(ACPI_POWER_FILE_STATUS,
				  acpi_device_dir(device));
		remove_proc_entry(acpi_device_bid(device), acpi_power_dir);
		acpi_device_dir(device) = NULL;
	}

	return 0;
}

/* --------------------------------------------------------------------------
                                Driver Interface
   -------------------------------------------------------------------------- */

static int acpi_power_add(struct acpi_device *device)
{
	int result = 0;
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
	INIT_LIST_HEAD(&resource->reference);
	strcpy(resource->name, device->pnp.bus_id);
	strcpy(acpi_device_name(device), ACPI_POWER_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_POWER_CLASS);
	acpi_driver_data(device) = resource;

	/* Evalute the object to get the system level and resource order. */
	status = acpi_evaluate_object(device->handle, NULL, NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		result = -ENODEV;
		goto end;
	}
	resource->system_level = acpi_object.power_resource.system_level;
	resource->order = acpi_object.power_resource.resource_order;

	result = acpi_power_get_state(resource);
	if (result)
		goto end;

	switch (resource->state) {
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

	result = acpi_power_add_fs(device);
	if (result)
		goto end;

	printk(KERN_INFO PREFIX "%s [%s] (%s)\n", acpi_device_name(device),
	       acpi_device_bid(device), resource->state ? "on" : "off");

      end:
	if (result)
		kfree(resource);

	return result;
}

static int acpi_power_remove(struct acpi_device *device, int type)
{
	struct acpi_power_resource *resource = NULL;
	struct list_head *node, *next;


	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	resource = acpi_driver_data(device);

	acpi_power_remove_fs(device);

	mutex_lock(&resource->resource_lock);
	list_for_each_safe(node, next, &resource->reference) {
		struct acpi_power_reference *ref = container_of(node, struct acpi_power_reference, node);
		list_del(&ref->node);
		kfree(ref);
	}
	mutex_unlock(&resource->resource_lock);

	kfree(resource);

	return 0;
}

static int acpi_power_resume(struct acpi_device *device)
{
	int result = 0;
	struct acpi_power_resource *resource = NULL;
	struct acpi_power_reference *ref;

	if (!device || !acpi_driver_data(device))
		return -EINVAL;

	resource = (struct acpi_power_resource *)acpi_driver_data(device);

	result = acpi_power_get_state(resource);
	if (result)
		return result;

	mutex_lock(&resource->resource_lock);
	if ((resource->state == ACPI_POWER_RESOURCE_STATE_OFF) &&
	    !list_empty(&resource->reference)) {
		ref = container_of(resource->reference.next, struct acpi_power_reference, node);
		mutex_unlock(&resource->resource_lock);
		result = acpi_power_on(device->handle, ref->device);
		return result;
	}

	mutex_unlock(&resource->resource_lock);
	return 0;
}

static int __init acpi_power_init(void)
{
	int result = 0;


	if (acpi_disabled)
		return 0;

	INIT_LIST_HEAD(&acpi_power_resource_list);

	acpi_power_dir = proc_mkdir(ACPI_POWER_CLASS, acpi_root_dir);
	if (!acpi_power_dir)
		return -ENODEV;

	result = acpi_bus_register_driver(&acpi_power_driver);
	if (result < 0) {
		remove_proc_entry(ACPI_POWER_CLASS, acpi_root_dir);
		return -ENODEV;
	}

	return 0;
}

subsys_initcall(acpi_power_init);
