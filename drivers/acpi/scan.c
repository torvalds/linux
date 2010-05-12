/*
 * scan.c - support for transforming the ACPI namespace into individual objects
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/acpi.h>
#include <linux/signal.h>
#include <linux/kthread.h>
#include <linux/dmi.h>

#include <acpi/acpi_drivers.h>

#include "internal.h"

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("scan");
#define STRUCT_TO_INT(s)	(*((int*)&s))
extern struct acpi_device *acpi_root;

#define ACPI_BUS_CLASS			"system_bus"
#define ACPI_BUS_HID			"LNXSYBUS"
#define ACPI_BUS_DEVICE_NAME		"System Bus"

#define ACPI_IS_ROOT_DEVICE(device)    (!(device)->parent)

static LIST_HEAD(acpi_device_list);
static LIST_HEAD(acpi_bus_id_list);
DEFINE_MUTEX(acpi_device_lock);
LIST_HEAD(acpi_wakeup_device_list);

struct acpi_device_bus_id{
	char bus_id[15];
	unsigned int instance_no;
	struct list_head node;
};

/*
 * Creates hid/cid(s) string needed for modalias and uevent
 * e.g. on a device with hid:IBM0001 and cid:ACPI0001 you get:
 * char *modalias: "acpi:IBM0001:ACPI0001"
*/
static int create_modalias(struct acpi_device *acpi_dev, char *modalias,
			   int size)
{
	int len;
	int count;
	struct acpi_hardware_id *id;

	len = snprintf(modalias, size, "acpi:");
	size -= len;

	list_for_each_entry(id, &acpi_dev->pnp.ids, list) {
		count = snprintf(&modalias[len], size, "%s:", id->id);
		if (count < 0 || count >= size)
			return -EINVAL;
		len += count;
		size -= count;
	}

	modalias[len] = '\0';
	return len;
}

static ssize_t
acpi_device_modalias_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int len;

	/* Device has no HID and no CID or string is >1024 */
	len = create_modalias(acpi_dev, buf, 1024);
	if (len <= 0)
		return 0;
	buf[len++] = '\n';
	return len;
}
static DEVICE_ATTR(modalias, 0444, acpi_device_modalias_show, NULL);

static void acpi_bus_hot_remove_device(void *context)
{
	struct acpi_device *device;
	acpi_handle handle = context;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status = AE_OK;

	if (acpi_bus_get_device(handle, &device))
		return;

	if (!device)
		return;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"Hot-removing device %s...\n", dev_name(&device->dev)));

	if (acpi_bus_trim(device, 1)) {
		printk(KERN_ERR PREFIX
				"Removing device failed\n");
		return;
	}

	/* power off device */
	status = acpi_evaluate_object(handle, "_PS3", NULL, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		printk(KERN_WARNING PREFIX
				"Power-off device failed\n");

	if (device->flags.lockable) {
		arg_list.count = 1;
		arg_list.pointer = &arg;
		arg.type = ACPI_TYPE_INTEGER;
		arg.integer.value = 0;
		acpi_evaluate_object(handle, "_LCK", &arg_list, NULL);
	}

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = 1;

	/*
	 * TBD: _EJD support.
	 */
	status = acpi_evaluate_object(handle, "_EJ0", &arg_list, NULL);
	if (ACPI_FAILURE(status))
		printk(KERN_WARNING PREFIX
				"Eject device failed\n");

	return;
}

static ssize_t
acpi_eject_store(struct device *d, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret = count;
	acpi_status status;
	acpi_object_type type = 0;
	struct acpi_device *acpi_device = to_acpi_device(d);

	if ((!count) || (buf[0] != '1')) {
		return -EINVAL;
	}
#ifndef FORCE_EJECT
	if (acpi_device->driver == NULL) {
		ret = -ENODEV;
		goto err;
	}
#endif
	status = acpi_get_type(acpi_device->handle, &type);
	if (ACPI_FAILURE(status) || (!acpi_device->flags.ejectable)) {
		ret = -ENODEV;
		goto err;
	}

	acpi_os_hotplug_execute(acpi_bus_hot_remove_device, acpi_device->handle);
err:
	return ret;
}

static DEVICE_ATTR(eject, 0200, NULL, acpi_eject_store);

static ssize_t
acpi_device_hid_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_device_hid(acpi_dev));
}
static DEVICE_ATTR(hid, 0444, acpi_device_hid_show, NULL);

static ssize_t
acpi_device_path_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_buffer path = {ACPI_ALLOCATE_BUFFER, NULL};
	int result;

	result = acpi_get_name(acpi_dev->handle, ACPI_FULL_PATHNAME, &path);
	if (result)
		goto end;

	result = sprintf(buf, "%s\n", (char*)path.pointer);
	kfree(path.pointer);
end:
	return result;
}
static DEVICE_ATTR(path, 0444, acpi_device_path_show, NULL);

static int acpi_device_setup_files(struct acpi_device *dev)
{
	acpi_status status;
	acpi_handle temp;
	int result = 0;

	/*
	 * Devices gotten from FADT don't have a "path" attribute
	 */
	if (dev->handle) {
		result = device_create_file(&dev->dev, &dev_attr_path);
		if (result)
			goto end;
	}

	result = device_create_file(&dev->dev, &dev_attr_hid);
	if (result)
		goto end;

	result = device_create_file(&dev->dev, &dev_attr_modalias);
	if (result)
		goto end;

        /*
         * If device has _EJ0, 'eject' file is created that is used to trigger
         * hot-removal function from userland.
         */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status))
		result = device_create_file(&dev->dev, &dev_attr_eject);
end:
	return result;
}

static void acpi_device_remove_files(struct acpi_device *dev)
{
	acpi_status status;
	acpi_handle temp;

	/*
	 * If device has _EJ0, 'eject' file is created that is used to trigger
	 * hot-removal function from userland.
	 */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status))
		device_remove_file(&dev->dev, &dev_attr_eject);

	device_remove_file(&dev->dev, &dev_attr_modalias);
	device_remove_file(&dev->dev, &dev_attr_hid);
	if (dev->handle)
		device_remove_file(&dev->dev, &dev_attr_path);
}
/* --------------------------------------------------------------------------
			ACPI Bus operations
   -------------------------------------------------------------------------- */

int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids)
{
	const struct acpi_device_id *id;
	struct acpi_hardware_id *hwid;

	/*
	 * If the device is not present, it is unnecessary to load device
	 * driver for it.
	 */
	if (!device->status.present)
		return -ENODEV;

	for (id = ids; id->id[0]; id++)
		list_for_each_entry(hwid, &device->pnp.ids, list)
			if (!strcmp((char *) id->id, hwid->id))
				return 0;

	return -ENOENT;
}
EXPORT_SYMBOL(acpi_match_device_ids);

static void acpi_free_ids(struct acpi_device *device)
{
	struct acpi_hardware_id *id, *tmp;

	list_for_each_entry_safe(id, tmp, &device->pnp.ids, list) {
		kfree(id->id);
		kfree(id);
	}
}

static void acpi_device_release(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	acpi_free_ids(acpi_dev);
	kfree(acpi_dev);
}

static int acpi_device_suspend(struct device *dev, pm_message_t state)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv && acpi_drv->ops.suspend)
		return acpi_drv->ops.suspend(acpi_dev, state);
	return 0;
}

static int acpi_device_resume(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv && acpi_drv->ops.resume)
		return acpi_drv->ops.resume(acpi_dev);
	return 0;
}

static int acpi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(drv);

	return !acpi_match_device_ids(acpi_dev, acpi_drv->ids);
}

static int acpi_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int len;

	if (add_uevent_var(env, "MODALIAS="))
		return -ENOMEM;
	len = create_modalias(acpi_dev, &env->buf[env->buflen - 1],
			      sizeof(env->buf) - env->buflen);
	if (len >= (sizeof(env->buf) - env->buflen))
		return -ENOMEM;
	env->buflen += len;
	return 0;
}

static void acpi_device_notify(acpi_handle handle, u32 event, void *data)
{
	struct acpi_device *device = data;

	device->driver->ops.notify(device, event);
}

static acpi_status acpi_device_notify_fixed(void *data)
{
	struct acpi_device *device = data;

	/* Fixed hardware devices have no handles */
	acpi_device_notify(NULL, ACPI_FIXED_HARDWARE_EVENT, device);
	return AE_OK;
}

static int acpi_device_install_notify_handler(struct acpi_device *device)
{
	acpi_status status;

	if (device->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						     acpi_device_notify_fixed,
						     device);
	else if (device->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		status =
		    acpi_install_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						     acpi_device_notify_fixed,
						     device);
	else
		status = acpi_install_notify_handler(device->handle,
						     ACPI_DEVICE_NOTIFY,
						     acpi_device_notify,
						     device);

	if (ACPI_FAILURE(status))
		return -EINVAL;
	return 0;
}

static void acpi_device_remove_notify_handler(struct acpi_device *device)
{
	if (device->device_type == ACPI_BUS_TYPE_POWER_BUTTON)
		acpi_remove_fixed_event_handler(ACPI_EVENT_POWER_BUTTON,
						acpi_device_notify_fixed);
	else if (device->device_type == ACPI_BUS_TYPE_SLEEP_BUTTON)
		acpi_remove_fixed_event_handler(ACPI_EVENT_SLEEP_BUTTON,
						acpi_device_notify_fixed);
	else
		acpi_remove_notify_handler(device->handle, ACPI_DEVICE_NOTIFY,
					   acpi_device_notify);
}

static int acpi_bus_driver_init(struct acpi_device *, struct acpi_driver *);
static int acpi_start_single_object(struct acpi_device *);
static int acpi_device_probe(struct device * dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(dev->driver);
	int ret;

	ret = acpi_bus_driver_init(acpi_dev, acpi_drv);
	if (!ret) {
		if (acpi_dev->bus_ops.acpi_op_start)
			acpi_start_single_object(acpi_dev);

		if (acpi_drv->ops.notify) {
			ret = acpi_device_install_notify_handler(acpi_dev);
			if (ret) {
				if (acpi_drv->ops.remove)
					acpi_drv->ops.remove(acpi_dev,
						     acpi_dev->removal_type);
				return ret;
			}
		}

		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Found driver [%s] for device [%s]\n",
			acpi_drv->name, acpi_dev->pnp.bus_id));
		get_device(dev);
	}
	return ret;
}

static int acpi_device_remove(struct device * dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv) {
		if (acpi_drv->ops.notify)
			acpi_device_remove_notify_handler(acpi_dev);
		if (acpi_drv->ops.remove)
			acpi_drv->ops.remove(acpi_dev, acpi_dev->removal_type);
	}
	acpi_dev->driver = NULL;
	acpi_dev->driver_data = NULL;

	put_device(dev);
	return 0;
}

struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.suspend	= acpi_device_suspend,
	.resume		= acpi_device_resume,
	.match		= acpi_bus_match,
	.probe		= acpi_device_probe,
	.remove		= acpi_device_remove,
	.uevent		= acpi_device_uevent,
};

static int acpi_device_register(struct acpi_device *device)
{
	int result;
	struct acpi_device_bus_id *acpi_device_bus_id, *new_bus_id;
	int found = 0;

	/*
	 * Linkage
	 * -------
	 * Link this device to its parent and siblings.
	 */
	INIT_LIST_HEAD(&device->children);
	INIT_LIST_HEAD(&device->node);
	INIT_LIST_HEAD(&device->wakeup_list);

	new_bus_id = kzalloc(sizeof(struct acpi_device_bus_id), GFP_KERNEL);
	if (!new_bus_id) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	mutex_lock(&acpi_device_lock);
	/*
	 * Find suitable bus_id and instance number in acpi_bus_id_list
	 * If failed, create one and link it into acpi_bus_id_list
	 */
	list_for_each_entry(acpi_device_bus_id, &acpi_bus_id_list, node) {
		if (!strcmp(acpi_device_bus_id->bus_id,
			    acpi_device_hid(device))) {
			acpi_device_bus_id->instance_no++;
			found = 1;
			kfree(new_bus_id);
			break;
		}
	}
	if (!found) {
		acpi_device_bus_id = new_bus_id;
		strcpy(acpi_device_bus_id->bus_id, acpi_device_hid(device));
		acpi_device_bus_id->instance_no = 0;
		list_add_tail(&acpi_device_bus_id->node, &acpi_bus_id_list);
	}
	dev_set_name(&device->dev, "%s:%02x", acpi_device_bus_id->bus_id, acpi_device_bus_id->instance_no);

	if (device->parent)
		list_add_tail(&device->node, &device->parent->children);

	if (device->wakeup.flags.valid)
		list_add_tail(&device->wakeup_list, &acpi_wakeup_device_list);
	mutex_unlock(&acpi_device_lock);

	if (device->parent)
		device->dev.parent = &device->parent->dev;
	device->dev.bus = &acpi_bus_type;
	device->dev.release = &acpi_device_release;
	result = device_register(&device->dev);
	if (result) {
		dev_err(&device->dev, "Error registering device\n");
		goto end;
	}

	result = acpi_device_setup_files(device);
	if (result)
		printk(KERN_ERR PREFIX "Error creating sysfs interface for device %s\n",
		       dev_name(&device->dev));

	device->removal_type = ACPI_BUS_REMOVAL_NORMAL;
	return 0;
end:
	mutex_lock(&acpi_device_lock);
	if (device->parent)
		list_del(&device->node);
	list_del(&device->wakeup_list);
	mutex_unlock(&acpi_device_lock);
	return result;
}

static void acpi_device_unregister(struct acpi_device *device, int type)
{
	mutex_lock(&acpi_device_lock);
	if (device->parent)
		list_del(&device->node);

	list_del(&device->wakeup_list);
	mutex_unlock(&acpi_device_lock);

	acpi_detach_data(device->handle, acpi_bus_data_handler);

	acpi_device_remove_files(device);
	device_unregister(&device->dev);
}

/* --------------------------------------------------------------------------
                                 Driver Management
   -------------------------------------------------------------------------- */
/**
 * acpi_bus_driver_init - add a device to a driver
 * @device: the device to add and initialize
 * @driver: driver for the device
 *
 * Used to initialize a device via its device driver.  Called whenever a
 * driver is bound to a device.  Invokes the driver's add() ops.
 */
static int
acpi_bus_driver_init(struct acpi_device *device, struct acpi_driver *driver)
{
	int result = 0;

	if (!device || !driver)
		return -EINVAL;

	if (!driver->ops.add)
		return -ENOSYS;

	result = driver->ops.add(device);
	if (result) {
		device->driver = NULL;
		device->driver_data = NULL;
		return result;
	}

	device->driver = driver;

	/*
	 * TBD - Configuration Management: Assign resources to device based
	 * upon possible configuration and currently allocated resources.
	 */

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			  "Driver successfully bound to device\n"));
	return 0;
}

static int acpi_start_single_object(struct acpi_device *device)
{
	int result = 0;
	struct acpi_driver *driver;


	if (!(driver = device->driver))
		return 0;

	if (driver->ops.start) {
		result = driver->ops.start(device);
		if (result && driver->ops.remove)
			driver->ops.remove(device, ACPI_BUS_REMOVAL_NORMAL);
	}

	return result;
}

/**
 * acpi_bus_register_driver - register a driver with the ACPI bus
 * @driver: driver being registered
 *
 * Registers a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and binds.  Returns zero for
 * success or a negative error status for failure.
 */
int acpi_bus_register_driver(struct acpi_driver *driver)
{
	int ret;

	if (acpi_disabled)
		return -ENODEV;
	driver->drv.name = driver->name;
	driver->drv.bus = &acpi_bus_type;
	driver->drv.owner = driver->owner;

	ret = driver_register(&driver->drv);
	return ret;
}

EXPORT_SYMBOL(acpi_bus_register_driver);

/**
 * acpi_bus_unregister_driver - unregisters a driver with the APIC bus
 * @driver: driver to unregister
 *
 * Unregisters a driver with the ACPI bus.  Searches the namespace for all
 * devices that match the driver's criteria and unbinds.
 */
void acpi_bus_unregister_driver(struct acpi_driver *driver)
{
	driver_unregister(&driver->drv);
}

EXPORT_SYMBOL(acpi_bus_unregister_driver);

/* --------------------------------------------------------------------------
                                 Device Enumeration
   -------------------------------------------------------------------------- */
static struct acpi_device *acpi_bus_get_parent(acpi_handle handle)
{
	acpi_status status;
	int ret;
	struct acpi_device *device;

	/*
	 * Fixed hardware devices do not appear in the namespace and do not
	 * have handles, but we fabricate acpi_devices for them, so we have
	 * to deal with them specially.
	 */
	if (handle == NULL)
		return acpi_root;

	do {
		status = acpi_get_parent(handle, &handle);
		if (status == AE_NULL_ENTRY)
			return NULL;
		if (ACPI_FAILURE(status))
			return acpi_root;

		ret = acpi_bus_get_device(handle, &device);
		if (ret == 0)
			return device;
	} while (1);
}

acpi_status
acpi_bus_get_ejd(acpi_handle handle, acpi_handle *ejd)
{
	acpi_status status;
	acpi_handle tmp;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *obj;

	status = acpi_get_handle(handle, "_EJD", &tmp);
	if (ACPI_FAILURE(status))
		return status;

	status = acpi_evaluate_object(handle, "_EJD", NULL, &buffer);
	if (ACPI_SUCCESS(status)) {
		obj = buffer.pointer;
		status = acpi_get_handle(ACPI_ROOT_OBJECT, obj->string.pointer,
					 ejd);
		kfree(buffer.pointer);
	}
	return status;
}
EXPORT_SYMBOL_GPL(acpi_bus_get_ejd);

void acpi_bus_data_handler(acpi_handle handle, void *context)
{

	/* TBD */

	return;
}

static int acpi_bus_get_perf_flags(struct acpi_device *device)
{
	device->performance.state = ACPI_STATE_UNKNOWN;
	return 0;
}

static acpi_status
acpi_bus_extract_wakeup_device_power_package(struct acpi_device *device,
					     union acpi_object *package)
{
	int i = 0;
	union acpi_object *element = NULL;

	if (!device || !package || (package->package.count < 2))
		return AE_BAD_PARAMETER;

	element = &(package->package.elements[0]);
	if (!element)
		return AE_BAD_PARAMETER;
	if (element->type == ACPI_TYPE_PACKAGE) {
		if ((element->package.count < 2) ||
		    (element->package.elements[0].type !=
		     ACPI_TYPE_LOCAL_REFERENCE)
		    || (element->package.elements[1].type != ACPI_TYPE_INTEGER))
			return AE_BAD_DATA;
		device->wakeup.gpe_device =
		    element->package.elements[0].reference.handle;
		device->wakeup.gpe_number =
		    (u32) element->package.elements[1].integer.value;
	} else if (element->type == ACPI_TYPE_INTEGER) {
		device->wakeup.gpe_number = element->integer.value;
	} else
		return AE_BAD_DATA;

	element = &(package->package.elements[1]);
	if (element->type != ACPI_TYPE_INTEGER) {
		return AE_BAD_DATA;
	}
	device->wakeup.sleep_state = element->integer.value;

	if ((package->package.count - 2) > ACPI_MAX_HANDLES) {
		return AE_NO_MEMORY;
	}
	device->wakeup.resources.count = package->package.count - 2;
	for (i = 0; i < device->wakeup.resources.count; i++) {
		element = &(package->package.elements[i + 2]);
		if (element->type != ACPI_TYPE_LOCAL_REFERENCE)
			return AE_BAD_DATA;

		device->wakeup.resources.handles[i] = element->reference.handle;
	}

	return AE_OK;
}

static void acpi_bus_set_run_wake_flags(struct acpi_device *device)
{
	struct acpi_device_id button_device_ids[] = {
		{"PNP0C0D", 0},
		{"PNP0C0C", 0},
		{"PNP0C0E", 0},
		{"", 0},
	};
	acpi_status status;
	acpi_event_status event_status;

	device->wakeup.run_wake_count = 0;
	device->wakeup.flags.notifier_present = 0;

	/* Power button, Lid switch always enable wakeup */
	if (!acpi_match_device_ids(device, button_device_ids)) {
		device->wakeup.flags.run_wake = 1;
		device->wakeup.flags.always_enabled = 1;
		return;
	}

	status = acpi_get_gpe_status(NULL, device->wakeup.gpe_number,
					ACPI_NOT_ISR, &event_status);
	if (status == AE_OK)
		device->wakeup.flags.run_wake =
				!!(event_status & ACPI_EVENT_FLAG_HANDLE);
}

static int acpi_bus_get_wakeup_device_flags(struct acpi_device *device)
{
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package = NULL;
	int psw_error;

	/* _PRW */
	status = acpi_evaluate_object(device->handle, "_PRW", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRW"));
		goto end;
	}

	package = (union acpi_object *)buffer.pointer;
	status = acpi_bus_extract_wakeup_device_power_package(device, package);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Extracting _PRW package"));
		goto end;
	}

	kfree(buffer.pointer);

	device->wakeup.flags.valid = 1;
	device->wakeup.prepare_count = 0;
	acpi_bus_set_run_wake_flags(device);
	/* Call _PSW/_DSW object to disable its ability to wake the sleeping
	 * system for the ACPI device with the _PRW object.
	 * The _PSW object is depreciated in ACPI 3.0 and is replaced by _DSW.
	 * So it is necessary to call _DSW object first. Only when it is not
	 * present will the _PSW object used.
	 */
	psw_error = acpi_device_sleep_wake(device, 0, 0, 0);
	if (psw_error)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"error in _DSW or _PSW evaluation\n"));

end:
	if (ACPI_FAILURE(status))
		device->flags.wake_capable = 0;
	return 0;
}

static int acpi_bus_get_power_flags(struct acpi_device *device)
{
	acpi_status status = 0;
	acpi_handle handle = NULL;
	u32 i = 0;


	/*
	 * Power Management Flags
	 */
	status = acpi_get_handle(device->handle, "_PSC", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.explicit_get = 1;
	status = acpi_get_handle(device->handle, "_IRC", &handle);
	if (ACPI_SUCCESS(status))
		device->power.flags.inrush_current = 1;

	/*
	 * Enumerate supported power management states
	 */
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3; i++) {
		struct acpi_device_power_state *ps = &device->power.states[i];
		char object_name[5] = { '_', 'P', 'R', '0' + i, '\0' };

		/* Evaluate "_PRx" to se if power resources are referenced */
		acpi_evaluate_reference(device->handle, object_name, NULL,
					&ps->resources);
		if (ps->resources.count) {
			device->power.flags.power_resources = 1;
			ps->flags.valid = 1;
		}

		/* Evaluate "_PSx" to see if we can do explicit sets */
		object_name[2] = 'S';
		status = acpi_get_handle(device->handle, object_name, &handle);
		if (ACPI_SUCCESS(status)) {
			ps->flags.explicit_set = 1;
			ps->flags.valid = 1;
		}

		/* State is valid if we have some power control */
		if (ps->resources.count || ps->flags.explicit_set)
			ps->flags.valid = 1;

		ps->power = -1;	/* Unknown - driver assigned */
		ps->latency = -1;	/* Unknown - driver assigned */
	}

	/* Set defaults for D0 and D3 states (always valid) */
	device->power.states[ACPI_STATE_D0].flags.valid = 1;
	device->power.states[ACPI_STATE_D0].power = 100;
	device->power.states[ACPI_STATE_D3].flags.valid = 1;
	device->power.states[ACPI_STATE_D3].power = 0;

	/* TBD: System wake support and resource requirements. */

	device->power.state = ACPI_STATE_UNKNOWN;
	acpi_bus_get_power(device->handle, &(device->power.state));

	return 0;
}

static int acpi_bus_get_flags(struct acpi_device *device)
{
	acpi_status status = AE_OK;
	acpi_handle temp = NULL;


	/* Presence of _STA indicates 'dynamic_status' */
	status = acpi_get_handle(device->handle, "_STA", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.dynamic_status = 1;

	/* Presence of _RMV indicates 'removable' */
	status = acpi_get_handle(device->handle, "_RMV", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.removable = 1;

	/* Presence of _EJD|_EJ0 indicates 'ejectable' */
	status = acpi_get_handle(device->handle, "_EJD", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.ejectable = 1;
	else {
		status = acpi_get_handle(device->handle, "_EJ0", &temp);
		if (ACPI_SUCCESS(status))
			device->flags.ejectable = 1;
	}

	/* Presence of _LCK indicates 'lockable' */
	status = acpi_get_handle(device->handle, "_LCK", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.lockable = 1;

	/* Presence of _PS0|_PR0 indicates 'power manageable' */
	status = acpi_get_handle(device->handle, "_PS0", &temp);
	if (ACPI_FAILURE(status))
		status = acpi_get_handle(device->handle, "_PR0", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.power_manageable = 1;

	/* Presence of _PRW indicates wake capable */
	status = acpi_get_handle(device->handle, "_PRW", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.wake_capable = 1;

	/* TBD: Performance management */

	return 0;
}

static void acpi_device_get_busid(struct acpi_device *device)
{
	char bus_id[5] = { '?', 0 };
	struct acpi_buffer buffer = { sizeof(bus_id), bus_id };
	int i = 0;

	/*
	 * Bus ID
	 * ------
	 * The device's Bus ID is simply the object name.
	 * TBD: Shouldn't this value be unique (within the ACPI namespace)?
	 */
	if (ACPI_IS_ROOT_DEVICE(device)) {
		strcpy(device->pnp.bus_id, "ACPI");
		return;
	}

	switch (device->device_type) {
	case ACPI_BUS_TYPE_POWER_BUTTON:
		strcpy(device->pnp.bus_id, "PWRF");
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		strcpy(device->pnp.bus_id, "SLPF");
		break;
	default:
		acpi_get_name(device->handle, ACPI_SINGLE_NAME, &buffer);
		/* Clean up trailing underscores (if any) */
		for (i = 3; i > 1; i--) {
			if (bus_id[i] == '_')
				bus_id[i] = '\0';
			else
				break;
		}
		strcpy(device->pnp.bus_id, bus_id);
		break;
	}
}

/*
 * acpi_bay_match - see if a device is an ejectable driver bay
 *
 * If an acpi object is ejectable and has one of the ACPI ATA methods defined,
 * then we can safely call it an ejectable drive bay
 */
static int acpi_bay_match(struct acpi_device *device){
	acpi_status status;
	acpi_handle handle;
	acpi_handle tmp;
	acpi_handle phandle;

	handle = device->handle;

	status = acpi_get_handle(handle, "_EJ0", &tmp);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	if ((ACPI_SUCCESS(acpi_get_handle(handle, "_GTF", &tmp))) ||
		(ACPI_SUCCESS(acpi_get_handle(handle, "_GTM", &tmp))) ||
		(ACPI_SUCCESS(acpi_get_handle(handle, "_STM", &tmp))) ||
		(ACPI_SUCCESS(acpi_get_handle(handle, "_SDD", &tmp))))
		return 0;

	if (acpi_get_parent(handle, &phandle))
		return -ENODEV;

        if ((ACPI_SUCCESS(acpi_get_handle(phandle, "_GTF", &tmp))) ||
                (ACPI_SUCCESS(acpi_get_handle(phandle, "_GTM", &tmp))) ||
                (ACPI_SUCCESS(acpi_get_handle(phandle, "_STM", &tmp))) ||
                (ACPI_SUCCESS(acpi_get_handle(phandle, "_SDD", &tmp))))
                return 0;

	return -ENODEV;
}

/*
 * acpi_dock_match - see if a device has a _DCK method
 */
static int acpi_dock_match(struct acpi_device *device)
{
	acpi_handle tmp;
	return acpi_get_handle(device->handle, "_DCK", &tmp);
}

char *acpi_device_hid(struct acpi_device *device)
{
	struct acpi_hardware_id *hid;

	hid = list_first_entry(&device->pnp.ids, struct acpi_hardware_id, list);
	return hid->id;
}
EXPORT_SYMBOL(acpi_device_hid);

static void acpi_add_id(struct acpi_device *device, const char *dev_id)
{
	struct acpi_hardware_id *id;

	id = kmalloc(sizeof(*id), GFP_KERNEL);
	if (!id)
		return;

	id->id = kmalloc(strlen(dev_id) + 1, GFP_KERNEL);
	if (!id->id) {
		kfree(id);
		return;
	}

	strcpy(id->id, dev_id);
	list_add_tail(&id->list, &device->pnp.ids);
}

/*
 * Old IBM workstations have a DSDT bug wherein the SMBus object
 * lacks the SMBUS01 HID and the methods do not have the necessary "_"
 * prefix.  Work around this.
 */
static int acpi_ibm_smbus_match(struct acpi_device *device)
{
	acpi_handle h_dummy;
	struct acpi_buffer path = {ACPI_ALLOCATE_BUFFER, NULL};
	int result;

	if (!dmi_name_in_vendors("IBM"))
		return -ENODEV;

	/* Look for SMBS object */
	result = acpi_get_name(device->handle, ACPI_SINGLE_NAME, &path);
	if (result)
		return result;

	if (strcmp("SMBS", path.pointer)) {
		result = -ENODEV;
		goto out;
	}

	/* Does it have the necessary (but misnamed) methods? */
	result = -ENODEV;
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "SBI", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "SBR", &h_dummy)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "SBW", &h_dummy)))
		result = 0;
out:
	kfree(path.pointer);
	return result;
}

static void acpi_device_set_id(struct acpi_device *device)
{
	acpi_status status;
	struct acpi_device_info *info;
	struct acpica_device_id_list *cid_list;
	int i;

	switch (device->device_type) {
	case ACPI_BUS_TYPE_DEVICE:
		if (ACPI_IS_ROOT_DEVICE(device)) {
			acpi_add_id(device, ACPI_SYSTEM_HID);
			break;
		}

		status = acpi_get_object_info(device->handle, &info);
		if (ACPI_FAILURE(status)) {
			printk(KERN_ERR PREFIX "%s: Error reading device info\n", __func__);
			return;
		}

		if (info->valid & ACPI_VALID_HID)
			acpi_add_id(device, info->hardware_id.string);
		if (info->valid & ACPI_VALID_CID) {
			cid_list = &info->compatible_id_list;
			for (i = 0; i < cid_list->count; i++)
				acpi_add_id(device, cid_list->ids[i].string);
		}
		if (info->valid & ACPI_VALID_ADR) {
			device->pnp.bus_address = info->address;
			device->flags.bus_address = 1;
		}

		kfree(info);

		/*
		 * Some devices don't reliably have _HIDs & _CIDs, so add
		 * synthetic HIDs to make sure drivers can find them.
		 */
		if (acpi_is_video_device(device))
			acpi_add_id(device, ACPI_VIDEO_HID);
		else if (ACPI_SUCCESS(acpi_bay_match(device)))
			acpi_add_id(device, ACPI_BAY_HID);
		else if (ACPI_SUCCESS(acpi_dock_match(device)))
			acpi_add_id(device, ACPI_DOCK_HID);
		else if (!acpi_ibm_smbus_match(device))
			acpi_add_id(device, ACPI_SMBUS_IBM_HID);
		else if (!acpi_device_hid(device) &&
			 ACPI_IS_ROOT_DEVICE(device->parent)) {
			acpi_add_id(device, ACPI_BUS_HID); /* \_SB, LNXSYBUS */
			strcpy(device->pnp.device_name, ACPI_BUS_DEVICE_NAME);
			strcpy(device->pnp.device_class, ACPI_BUS_CLASS);
		}

		break;
	case ACPI_BUS_TYPE_POWER:
		acpi_add_id(device, ACPI_POWER_HID);
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		acpi_add_id(device, ACPI_PROCESSOR_OBJECT_HID);
		break;
	case ACPI_BUS_TYPE_THERMAL:
		acpi_add_id(device, ACPI_THERMAL_HID);
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		acpi_add_id(device, ACPI_BUTTON_HID_POWERF);
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		acpi_add_id(device, ACPI_BUTTON_HID_SLEEPF);
		break;
	}

	/*
	 * We build acpi_devices for some objects that don't have _HID or _CID,
	 * e.g., PCI bridges and slots.  Drivers can't bind to these objects,
	 * but we do use them indirectly by traversing the acpi_device tree.
	 * This generic ID isn't useful for driver binding, but it provides
	 * the useful property that "every acpi_device has an ID."
	 */
	if (list_empty(&device->pnp.ids))
		acpi_add_id(device, "device");
}

static int acpi_device_set_context(struct acpi_device *device)
{
	acpi_status status;

	/*
	 * Context
	 * -------
	 * Attach this 'struct acpi_device' to the ACPI object.  This makes
	 * resolutions from handle->device very efficient.  Fixed hardware
	 * devices have no handles, so we skip them.
	 */
	if (!device->handle)
		return 0;

	status = acpi_attach_data(device->handle,
				  acpi_bus_data_handler, device);
	if (ACPI_SUCCESS(status))
		return 0;

	printk(KERN_ERR PREFIX "Error attaching device data\n");
	return -ENODEV;
}

static int acpi_bus_remove(struct acpi_device *dev, int rmdevice)
{
	if (!dev)
		return -EINVAL;

	dev->removal_type = ACPI_BUS_REMOVAL_EJECT;
	device_release_driver(&dev->dev);

	if (!rmdevice)
		return 0;

	/*
	 * unbind _ADR-Based Devices when hot removal
	 */
	if (dev->flags.bus_address) {
		if ((dev->parent) && (dev->parent->ops.unbind))
			dev->parent->ops.unbind(dev);
	}
	acpi_device_unregister(dev, ACPI_BUS_REMOVAL_EJECT);

	return 0;
}

static int acpi_add_single_object(struct acpi_device **child,
				  acpi_handle handle, int type,
				  unsigned long long sta,
				  struct acpi_bus_ops *ops)
{
	int result;
	struct acpi_device *device;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	device = kzalloc(sizeof(struct acpi_device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	INIT_LIST_HEAD(&device->pnp.ids);
	device->device_type = type;
	device->handle = handle;
	device->parent = acpi_bus_get_parent(handle);
	device->bus_ops = *ops; /* workround for not call .start */
	STRUCT_TO_INT(device->status) = sta;

	acpi_device_get_busid(device);

	/*
	 * Flags
	 * -----
	 * Note that we only look for object handles -- cannot evaluate objects
	 * until we know the device is present and properly initialized.
	 */
	result = acpi_bus_get_flags(device);
	if (result)
		goto end;

	/*
	 * Initialize Device
	 * -----------------
	 * TBD: Synch with Core's enumeration/initialization process.
	 */
	acpi_device_set_id(device);

	/*
	 * Power Management
	 * ----------------
	 */
	if (device->flags.power_manageable) {
		result = acpi_bus_get_power_flags(device);
		if (result)
			goto end;
	}

	/*
	 * Wakeup device management
	 *-----------------------
	 */
	if (device->flags.wake_capable) {
		result = acpi_bus_get_wakeup_device_flags(device);
		if (result)
			goto end;
	}

	/*
	 * Performance Management
	 * ----------------------
	 */
	if (device->flags.performance_manageable) {
		result = acpi_bus_get_perf_flags(device);
		if (result)
			goto end;
	}

	if ((result = acpi_device_set_context(device)))
		goto end;

	result = acpi_device_register(device);

	/*
	 * Bind _ADR-Based Devices when hot add
	 */
	if (device->flags.bus_address) {
		if (device->parent && device->parent->ops.bind)
			device->parent->ops.bind(device);
	}

end:
	if (!result) {
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Adding %s [%s] parent %s\n", dev_name(&device->dev),
			 (char *) buffer.pointer,
			 device->parent ? dev_name(&device->parent->dev) :
					  "(null)"));
		kfree(buffer.pointer);
		*child = device;
	} else
		acpi_device_release(&device->dev);

	return result;
}

#define ACPI_STA_DEFAULT (ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED | \
			  ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING)

static int acpi_bus_type_and_status(acpi_handle handle, int *type,
				    unsigned long long *sta)
{
	acpi_status status;
	acpi_object_type acpi_type;

	status = acpi_get_type(handle, &acpi_type);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	switch (acpi_type) {
	case ACPI_TYPE_ANY:		/* for ACPI_ROOT_OBJECT */
	case ACPI_TYPE_DEVICE:
		*type = ACPI_BUS_TYPE_DEVICE;
		status = acpi_bus_get_status_handle(handle, sta);
		if (ACPI_FAILURE(status))
			return -ENODEV;
		break;
	case ACPI_TYPE_PROCESSOR:
		*type = ACPI_BUS_TYPE_PROCESSOR;
		status = acpi_bus_get_status_handle(handle, sta);
		if (ACPI_FAILURE(status))
			return -ENODEV;
		break;
	case ACPI_TYPE_THERMAL:
		*type = ACPI_BUS_TYPE_THERMAL;
		*sta = ACPI_STA_DEFAULT;
		break;
	case ACPI_TYPE_POWER:
		*type = ACPI_BUS_TYPE_POWER;
		*sta = ACPI_STA_DEFAULT;
		break;
	default:
		return -ENODEV;
	}

	return 0;
}

static acpi_status acpi_bus_check_add(acpi_handle handle, u32 lvl,
				      void *context, void **return_value)
{
	struct acpi_bus_ops *ops = context;
	int type;
	unsigned long long sta;
	struct acpi_device *device;
	acpi_status status;
	int result;

	result = acpi_bus_type_and_status(handle, &type, &sta);
	if (result)
		return AE_OK;

	if (!(sta & ACPI_STA_DEVICE_PRESENT) &&
	    !(sta & ACPI_STA_DEVICE_FUNCTIONING))
		return AE_CTRL_DEPTH;

	/*
	 * We may already have an acpi_device from a previous enumeration.  If
	 * so, we needn't add it again, but we may still have to start it.
	 */
	device = NULL;
	acpi_bus_get_device(handle, &device);
	if (ops->acpi_op_add && !device)
		acpi_add_single_object(&device, handle, type, sta, ops);

	if (!device)
		return AE_CTRL_DEPTH;

	if (ops->acpi_op_start && !(ops->acpi_op_add)) {
		status = acpi_start_single_object(device);
		if (ACPI_FAILURE(status))
			return AE_CTRL_DEPTH;
	}

	if (!*return_value)
		*return_value = device;
	return AE_OK;
}

static int acpi_bus_scan(acpi_handle handle, struct acpi_bus_ops *ops,
			 struct acpi_device **child)
{
	acpi_status status;
	void *device = NULL;

	status = acpi_bus_check_add(handle, 0, ops, &device);
	if (ACPI_SUCCESS(status))
		acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				    acpi_bus_check_add, NULL, ops, &device);

	if (child)
		*child = device;

	if (device)
		return 0;
	else
		return -ENODEV;
}

/*
 * acpi_bus_add and acpi_bus_start
 *
 * scan a given ACPI tree and (probably recently hot-plugged)
 * create and add or starts found devices.
 *
 * If no devices were found -ENODEV is returned which does not
 * mean that this is a real error, there just have been no suitable
 * ACPI objects in the table trunk from which the kernel could create
 * a device and add/start an appropriate driver.
 */

int
acpi_bus_add(struct acpi_device **child,
	     struct acpi_device *parent, acpi_handle handle, int type)
{
	struct acpi_bus_ops ops;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;

	return acpi_bus_scan(handle, &ops, child);
}
EXPORT_SYMBOL(acpi_bus_add);

int acpi_bus_start(struct acpi_device *device)
{
	struct acpi_bus_ops ops;

	if (!device)
		return -EINVAL;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_start = 1;

	return acpi_bus_scan(device->handle, &ops, NULL);
}
EXPORT_SYMBOL(acpi_bus_start);

int acpi_bus_trim(struct acpi_device *start, int rmdevice)
{
	acpi_status status;
	struct acpi_device *parent, *child;
	acpi_handle phandle, chandle;
	acpi_object_type type;
	u32 level = 1;
	int err = 0;

	parent = start;
	phandle = start->handle;
	child = chandle = NULL;

	while ((level > 0) && parent && (!err)) {
		status = acpi_get_next_object(ACPI_TYPE_ANY, phandle,
					      chandle, &chandle);

		/*
		 * If this scope is exhausted then move our way back up.
		 */
		if (ACPI_FAILURE(status)) {
			level--;
			chandle = phandle;
			acpi_get_parent(phandle, &phandle);
			child = parent;
			parent = parent->parent;

			if (level == 0)
				err = acpi_bus_remove(child, rmdevice);
			else
				err = acpi_bus_remove(child, 1);

			continue;
		}

		status = acpi_get_type(chandle, &type);
		if (ACPI_FAILURE(status)) {
			continue;
		}
		/*
		 * If there is a device corresponding to chandle then
		 * parse it (depth-first).
		 */
		if (acpi_bus_get_device(chandle, &child) == 0) {
			level++;
			phandle = chandle;
			chandle = NULL;
			parent = child;
		}
		continue;
	}
	return err;
}
EXPORT_SYMBOL_GPL(acpi_bus_trim);

static int acpi_bus_scan_fixed(void)
{
	int result = 0;
	struct acpi_device *device = NULL;
	struct acpi_bus_ops ops;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;
	ops.acpi_op_start = 1;

	/*
	 * Enumerate all fixed-feature devices.
	 */
	if ((acpi_gbl_FADT.flags & ACPI_FADT_POWER_BUTTON) == 0) {
		result = acpi_add_single_object(&device, NULL,
						ACPI_BUS_TYPE_POWER_BUTTON,
						ACPI_STA_DEFAULT,
						&ops);
	}

	if ((acpi_gbl_FADT.flags & ACPI_FADT_SLEEP_BUTTON) == 0) {
		result = acpi_add_single_object(&device, NULL,
						ACPI_BUS_TYPE_SLEEP_BUTTON,
						ACPI_STA_DEFAULT,
						&ops);
	}

	return result;
}

int __init acpi_scan_init(void)
{
	int result;
	struct acpi_bus_ops ops;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;
	ops.acpi_op_start = 1;

	result = bus_register(&acpi_bus_type);
	if (result) {
		/* We don't want to quit even if we failed to add suspend/resume */
		printk(KERN_ERR PREFIX "Could not register bus type\n");
	}

	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan(ACPI_ROOT_OBJECT, &ops, &acpi_root);

	if (!result)
		result = acpi_bus_scan_fixed();

	if (result)
		acpi_device_unregister(acpi_root, ACPI_BUS_REMOVAL_NORMAL);

	return result;
}
