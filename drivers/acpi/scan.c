/*
 * scan.c - support for transforming the ACPI namespace into individual objects
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/acpi.h>

#include <acpi/acpi_drivers.h>
#include <acpi/acinterp.h>	/* for acpi_ex_eisa_id_to_string() */

#define _COMPONENT		ACPI_BUS_COMPONENT
ACPI_MODULE_NAME("scan")
#define STRUCT_TO_INT(s)	(*((int*)&s))
extern struct acpi_device *acpi_root;

#define ACPI_BUS_CLASS			"system_bus"
#define ACPI_BUS_HID			"ACPI_BUS"
#define ACPI_BUS_DRIVER_NAME		"ACPI Bus Driver"
#define ACPI_BUS_DEVICE_NAME		"System Bus"

static LIST_HEAD(acpi_device_list);
DEFINE_SPINLOCK(acpi_device_lock);
LIST_HEAD(acpi_wakeup_device_list);


static void acpi_device_release(struct kobject *kobj)
{
	struct acpi_device *dev = container_of(kobj, struct acpi_device, kobj);
	kfree(dev->pnp.cid_list);
	kfree(dev);
}

struct acpi_device_attribute {
	struct attribute attr;
	 ssize_t(*show) (struct acpi_device *, char *);
	 ssize_t(*store) (struct acpi_device *, const char *, size_t);
};

typedef void acpi_device_sysfs_files(struct kobject *,
				     const struct attribute *);

static void setup_sys_fs_device_files(struct acpi_device *dev,
				      acpi_device_sysfs_files * func);

#define create_sysfs_device_files(dev)	\
	setup_sys_fs_device_files(dev, (acpi_device_sysfs_files *)&sysfs_create_file)
#define remove_sysfs_device_files(dev)	\
	setup_sys_fs_device_files(dev, (acpi_device_sysfs_files *)&sysfs_remove_file)

#define to_acpi_device(n) container_of(n, struct acpi_device, kobj)
#define to_handle_attr(n) container_of(n, struct acpi_device_attribute, attr);

static ssize_t acpi_device_attr_show(struct kobject *kobj,
				     struct attribute *attr, char *buf)
{
	struct acpi_device *device = to_acpi_device(kobj);
	struct acpi_device_attribute *attribute = to_handle_attr(attr);
	return attribute->show ? attribute->show(device, buf) : -EIO;
}
static ssize_t acpi_device_attr_store(struct kobject *kobj,
				      struct attribute *attr, const char *buf,
				      size_t len)
{
	struct acpi_device *device = to_acpi_device(kobj);
	struct acpi_device_attribute *attribute = to_handle_attr(attr);
	return attribute->store ? attribute->store(device, buf, len) : -EIO;
}

static struct sysfs_ops acpi_device_sysfs_ops = {
	.show = acpi_device_attr_show,
	.store = acpi_device_attr_store,
};

static struct kobj_type ktype_acpi_ns = {
	.sysfs_ops = &acpi_device_sysfs_ops,
	.release = acpi_device_release,
};

static int namespace_uevent(struct kset *kset, struct kobject *kobj,
			     char **envp, int num_envp, char *buffer,
			     int buffer_size)
{
	struct acpi_device *dev = to_acpi_device(kobj);
	int i = 0;
	int len = 0;

	if (!dev->driver)
		return 0;

	if (add_uevent_var(envp, num_envp, &i, buffer, buffer_size, &len,
			   "PHYSDEVDRIVER=%s", dev->driver->name))
		return -ENOMEM;

	envp[i] = NULL;

	return 0;
}

static struct kset_uevent_ops namespace_uevent_ops = {
	.uevent = &namespace_uevent,
};

static struct kset acpi_namespace_kset = {
	.kobj = {
		 .name = "namespace",
		 },
	.subsys = &acpi_subsys,
	.ktype = &ktype_acpi_ns,
	.uevent_ops = &namespace_uevent_ops,
};

/* --------------------------------------------------------------------------
		ACPI sysfs device file support
   -------------------------------------------------------------------------- */
static ssize_t acpi_eject_store(struct acpi_device *device,
				const char *buf, size_t count);

#define ACPI_DEVICE_ATTR(_name,_mode,_show,_store) \
static struct acpi_device_attribute acpi_device_attr_##_name = \
		__ATTR(_name, _mode, _show, _store)

ACPI_DEVICE_ATTR(eject, 0200, NULL, acpi_eject_store);

/**
 * setup_sys_fs_device_files - sets up the device files under device namespace
 * @dev:	acpi_device object
 * @func:	function pointer to create or destroy the device file
 */
static void
setup_sys_fs_device_files(struct acpi_device *dev,
			  acpi_device_sysfs_files * func)
{
	acpi_status status;
	acpi_handle temp = NULL;

	/*
	 * If device has _EJ0, 'eject' file is created that is used to trigger
	 * hot-removal function from userland.
	 */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status))
		(*(func)) (&dev->kobj, &acpi_device_attr_eject.attr);
}

static int acpi_eject_operation(acpi_handle handle, int lockable)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status = AE_OK;

	/*
	 * TBD: evaluate _PS3?
	 */

	if (lockable) {
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
	if (ACPI_FAILURE(status)) {
		return (-ENODEV);
	}

	return (0);
}

static ssize_t
acpi_eject_store(struct acpi_device *device, const char *buf, size_t count)
{
	int result;
	int ret = count;
	int islockable;
	acpi_status status;
	acpi_handle handle;
	acpi_object_type type = 0;

	if ((!count) || (buf[0] != '1')) {
		return -EINVAL;
	}
#ifndef FORCE_EJECT
	if (device->driver == NULL) {
		ret = -ENODEV;
		goto err;
	}
#endif
	status = acpi_get_type(device->handle, &type);
	if (ACPI_FAILURE(status) || (!device->flags.ejectable)) {
		ret = -ENODEV;
		goto err;
	}

	islockable = device->flags.lockable;
	handle = device->handle;

	result = acpi_bus_trim(device, 1);

	if (!result)
		result = acpi_eject_operation(handle, islockable);

	if (result) {
		ret = -EBUSY;
	}
      err:
	return ret;
}

/* --------------------------------------------------------------------------
			ACPI Bus operations
   -------------------------------------------------------------------------- */
static inline struct acpi_device * to_acpi_dev(struct device * dev)
{
	return container_of(dev, struct acpi_device, dev);
}

static int root_suspend(struct acpi_device * acpi_dev, pm_message_t state)
{
	struct acpi_device * dev, * next;
	int result;

	spin_lock(&acpi_device_lock);
	list_for_each_entry_safe_reverse(dev, next, &acpi_device_list, g_list) {
		if (dev->driver && dev->driver->ops.suspend) {
			spin_unlock(&acpi_device_lock);
			result = dev->driver->ops.suspend(dev, 0);
			if (result) {
				printk(KERN_ERR PREFIX "[%s - %s] Suspend failed: %d\n",
				       acpi_device_name(dev),
				       acpi_device_bid(dev), result);
			}
			spin_lock(&acpi_device_lock);
		}
	}
	spin_unlock(&acpi_device_lock);
	return 0;
}

static int acpi_device_suspend(struct device * dev, pm_message_t state)
{
	struct acpi_device * acpi_dev = to_acpi_dev(dev);

	/*
	 * For now, we should only register 1 generic device -
	 * the ACPI root device - and from there, we walk the
	 * tree of ACPI devices to suspend each one using the
	 * ACPI driver methods.
	 */
	if (acpi_dev->handle == ACPI_ROOT_OBJECT)
		root_suspend(acpi_dev, state);
	return 0;
}

static int root_resume(struct acpi_device * acpi_dev)
{
	struct acpi_device * dev, * next;
	int result;

	spin_lock(&acpi_device_lock);
	list_for_each_entry_safe(dev, next, &acpi_device_list, g_list) {
		if (dev->driver && dev->driver->ops.resume) {
			spin_unlock(&acpi_device_lock);
			result = dev->driver->ops.resume(dev, 0);
			if (result) {
				printk(KERN_ERR PREFIX "[%s - %s] resume failed: %d\n",
				       acpi_device_name(dev),
				       acpi_device_bid(dev), result);
			}
			spin_lock(&acpi_device_lock);
		}
	}
	spin_unlock(&acpi_device_lock);
	return 0;
}

static int acpi_device_resume(struct device * dev)
{
	struct acpi_device * acpi_dev = to_acpi_dev(dev);

	/*
	 * For now, we should only register 1 generic device -
	 * the ACPI root device - and from there, we walk the
	 * tree of ACPI devices to resume each one using the
	 * ACPI driver methods.
	 */
	if (acpi_dev->handle == ACPI_ROOT_OBJECT)
		root_resume(acpi_dev);
	return 0;
}

/**
 * acpi_bus_match - match device IDs to driver's supported IDs
 * @device: the device that we are trying to match to a driver
 * @driver: driver whose device id table is being checked
 *
 * Checks the device's hardware (_HID) or compatible (_CID) ids to see if it
 * matches the specified driver's criteria.
 */
static int
acpi_bus_match(struct acpi_device *device, struct acpi_driver *driver)
{
	if (driver && driver->ops.match)
		return driver->ops.match(device, driver);
	return acpi_match_ids(device, driver->ids);
}

static struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.suspend	= acpi_device_suspend,
	.resume		= acpi_device_resume,
};

static void acpi_device_register(struct acpi_device *device,
				 struct acpi_device *parent)
{
	int err;

	/*
	 * Linkage
	 * -------
	 * Link this device to its parent and siblings.
	 */
	INIT_LIST_HEAD(&device->children);
	INIT_LIST_HEAD(&device->node);
	INIT_LIST_HEAD(&device->g_list);
	INIT_LIST_HEAD(&device->wakeup_list);

	spin_lock(&acpi_device_lock);
	if (device->parent) {
		list_add_tail(&device->node, &device->parent->children);
		list_add_tail(&device->g_list, &device->parent->g_list);
	} else
		list_add_tail(&device->g_list, &acpi_device_list);
	if (device->wakeup.flags.valid)
		list_add_tail(&device->wakeup_list, &acpi_wakeup_device_list);
	spin_unlock(&acpi_device_lock);

	strlcpy(device->kobj.name, device->pnp.bus_id, KOBJ_NAME_LEN);
	if (parent)
		device->kobj.parent = &parent->kobj;
	device->kobj.ktype = &ktype_acpi_ns;
	device->kobj.kset = &acpi_namespace_kset;
	err = kobject_register(&device->kobj);
	if (err < 0)
		printk(KERN_WARNING "%s: kobject_register error: %d\n",
			__FUNCTION__, err);
	create_sysfs_device_files(device);
}

static void acpi_device_unregister(struct acpi_device *device, int type)
{
	spin_lock(&acpi_device_lock);
	if (device->parent) {
		list_del(&device->node);
		list_del(&device->g_list);
	} else
		list_del(&device->g_list);

	list_del(&device->wakeup_list);

	spin_unlock(&acpi_device_lock);

	acpi_detach_data(device->handle, acpi_bus_data_handler);
	remove_sysfs_device_files(device);
	kobject_unregister(&device->kobj);
}

/* --------------------------------------------------------------------------
                                 Driver Management
   -------------------------------------------------------------------------- */
static LIST_HEAD(acpi_bus_drivers);

/**
 * acpi_bus_driver_init - add a device to a driver
 * @device: the device to add and initialize
 * @driver: driver for the device
 *
 * Used to initialize a device via its device driver.  Called whenever a 
 * driver is bound to a device.  Invokes the driver's add() and start() ops.
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
		acpi_driver_data(device) = NULL;
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

static void acpi_driver_attach(struct acpi_driver *drv)
{
	struct list_head *node, *next;


	spin_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, g_list);

		if (dev->driver || !dev->status.present)
			continue;
		spin_unlock(&acpi_device_lock);

		if (!acpi_bus_match(dev, drv)) {
			if (!acpi_bus_driver_init(dev, drv)) {
				acpi_start_single_object(dev);
				atomic_inc(&drv->references);
				ACPI_DEBUG_PRINT((ACPI_DB_INFO,
						  "Found driver [%s] for device [%s]\n",
						  drv->name, dev->pnp.bus_id));
			}
		}
		spin_lock(&acpi_device_lock);
	}
	spin_unlock(&acpi_device_lock);
}

static void acpi_driver_detach(struct acpi_driver *drv)
{
	struct list_head *node, *next;


	spin_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_device_list) {
		struct acpi_device *dev =
		    container_of(node, struct acpi_device, g_list);

		if (dev->driver == drv) {
			spin_unlock(&acpi_device_lock);
			if (drv->ops.remove)
				drv->ops.remove(dev, ACPI_BUS_REMOVAL_NORMAL);
			spin_lock(&acpi_device_lock);
			dev->driver = NULL;
			dev->driver_data = NULL;
			atomic_dec(&drv->references);
		}
	}
	spin_unlock(&acpi_device_lock);
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

	if (acpi_disabled)
		return -ENODEV;

	spin_lock(&acpi_device_lock);
	list_add_tail(&driver->node, &acpi_bus_drivers);
	spin_unlock(&acpi_device_lock);
	acpi_driver_attach(driver);

	return 0;
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
	acpi_driver_detach(driver);

	if (!atomic_read(&driver->references)) {
		spin_lock(&acpi_device_lock);
		list_del_init(&driver->node);
		spin_unlock(&acpi_device_lock);
	}
	return;
}

EXPORT_SYMBOL(acpi_bus_unregister_driver);

/**
 * acpi_bus_find_driver - check if there is a driver installed for the device
 * @device: device that we are trying to find a supporting driver for
 *
 * Parses the list of registered drivers looking for a driver applicable for
 * the specified device.
 */
static int acpi_bus_find_driver(struct acpi_device *device)
{
	int result = 0;
	struct list_head *node, *next;


	spin_lock(&acpi_device_lock);
	list_for_each_safe(node, next, &acpi_bus_drivers) {
		struct acpi_driver *driver =
		    container_of(node, struct acpi_driver, node);

		atomic_inc(&driver->references);
		spin_unlock(&acpi_device_lock);
		if (!acpi_bus_match(device, driver)) {
			result = acpi_bus_driver_init(device, driver);
			if (!result)
				goto Done;
		}
		atomic_dec(&driver->references);
		spin_lock(&acpi_device_lock);
	}
	spin_unlock(&acpi_device_lock);

      Done:
	return result;
}

/* --------------------------------------------------------------------------
                                 Device Enumeration
   -------------------------------------------------------------------------- */
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
		status = acpi_get_handle(NULL, obj->string.pointer, ejd);
		kfree(buffer.pointer);
	}
	return status;
}
EXPORT_SYMBOL_GPL(acpi_bus_get_ejd);

void acpi_bus_data_handler(acpi_handle handle, u32 function, void *context)
{

	/* TBD */

	return;
}

int acpi_match_ids(struct acpi_device *device, char *ids)
{
	if (device->flags.hardware_id)
		if (strstr(ids, device->pnp.hardware_id))
			return 0;

	if (device->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list = device->pnp.cid_list;
		int i;

		/* compare multiple _CID entries against driver ids */
		for (i = 0; i < cid_list->count; i++) {
			if (strstr(ids, cid_list->id[i].value))
				return 0;
		}
	}
	return -ENOENT;
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
		if (element->type != ACPI_TYPE_ANY) {
			return AE_BAD_DATA;
		}

		device->wakeup.resources.handles[i] = element->reference.handle;
	}

	return AE_OK;
}

static int acpi_bus_get_wakeup_device_flags(struct acpi_device *device)
{
	acpi_status status = 0;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package = NULL;


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
	/* Power button, Lid switch always enable wakeup */
	if (!acpi_match_ids(device, "PNP0C0D,PNP0C0C,PNP0C0E"))
		device->wakeup.flags.run_wake = 1;

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

	/* Presence of _CID indicates 'compatible_ids' */
	status = acpi_get_handle(device->handle, "_CID", &temp);
	if (ACPI_SUCCESS(status))
		device->flags.compatible_ids = 1;

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

	/* TBD: Peformance management */

	return 0;
}

static void acpi_device_get_busid(struct acpi_device *device,
				  acpi_handle handle, int type)
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
	switch (type) {
	case ACPI_BUS_TYPE_SYSTEM:
		strcpy(device->pnp.bus_id, "ACPI");
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		strcpy(device->pnp.bus_id, "PWRF");
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		strcpy(device->pnp.bus_id, "SLPF");
		break;
	default:
		acpi_get_name(handle, ACPI_SINGLE_NAME, &buffer);
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

static void acpi_device_set_id(struct acpi_device *device,
			       struct acpi_device *parent, acpi_handle handle,
			       int type)
{
	struct acpi_device_info *info;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	char *hid = NULL;
	char *uid = NULL;
	struct acpi_compatible_id_list *cid_list = NULL;
	acpi_status status;

	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		status = acpi_get_object_info(handle, &buffer);
		if (ACPI_FAILURE(status)) {
			printk("%s: Error reading device info\n", __FUNCTION__);
			return;
		}

		info = buffer.pointer;
		if (info->valid & ACPI_VALID_HID)
			hid = info->hardware_id.value;
		if (info->valid & ACPI_VALID_UID)
			uid = info->unique_id.value;
		if (info->valid & ACPI_VALID_CID)
			cid_list = &info->compatibility_id;
		if (info->valid & ACPI_VALID_ADR) {
			device->pnp.bus_address = info->address;
			device->flags.bus_address = 1;
		}
		break;
	case ACPI_BUS_TYPE_POWER:
		hid = ACPI_POWER_HID;
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		hid = ACPI_PROCESSOR_HID;
		break;
	case ACPI_BUS_TYPE_SYSTEM:
		hid = ACPI_SYSTEM_HID;
		break;
	case ACPI_BUS_TYPE_THERMAL:
		hid = ACPI_THERMAL_HID;
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		hid = ACPI_BUTTON_HID_POWERF;
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		hid = ACPI_BUTTON_HID_SLEEPF;
		break;
	}

	/* 
	 * \_SB
	 * ----
	 * Fix for the system root bus device -- the only root-level device.
	 */
	if (((acpi_handle)parent == ACPI_ROOT_OBJECT) && (type == ACPI_BUS_TYPE_DEVICE)) {
		hid = ACPI_BUS_HID;
		strcpy(device->pnp.device_name, ACPI_BUS_DEVICE_NAME);
		strcpy(device->pnp.device_class, ACPI_BUS_CLASS);
	}

	if (hid) {
		strcpy(device->pnp.hardware_id, hid);
		device->flags.hardware_id = 1;
	}
	if (uid) {
		strcpy(device->pnp.unique_id, uid);
		device->flags.unique_id = 1;
	}
	if (cid_list) {
		device->pnp.cid_list = kmalloc(cid_list->size, GFP_KERNEL);
		if (device->pnp.cid_list)
			memcpy(device->pnp.cid_list, cid_list, cid_list->size);
		else
			printk(KERN_ERR "Memory allocation error\n");
	}

	kfree(buffer.pointer);
}

static int acpi_device_set_context(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	int result = 0;
	/*
	 * Context
	 * -------
	 * Attach this 'struct acpi_device' to the ACPI object.  This makes
	 * resolutions from handle->device very efficient.  Note that we need
	 * to be careful with fixed-feature devices as they all attach to the
	 * root object.
	 */
	if (type != ACPI_BUS_TYPE_POWER_BUTTON &&
	    type != ACPI_BUS_TYPE_SLEEP_BUTTON) {
		status = acpi_attach_data(device->handle,
					  acpi_bus_data_handler, device);

		if (ACPI_FAILURE(status)) {
			printk("Error attaching device data\n");
			result = -ENODEV;
		}
	}
	return result;
}

static void acpi_device_get_debug_info(struct acpi_device *device,
				       acpi_handle handle, int type)
{
#ifdef CONFIG_ACPI_DEBUG_OUTPUT
	char *type_string = NULL;
	char name[80] = { '?', '\0' };
	struct acpi_buffer buffer = { sizeof(name), name };

	switch (type) {
	case ACPI_BUS_TYPE_DEVICE:
		type_string = "Device";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_POWER:
		type_string = "Power Resource";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_PROCESSOR:
		type_string = "Processor";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_SYSTEM:
		type_string = "System";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_THERMAL:
		type_string = "Thermal Zone";
		acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
		break;
	case ACPI_BUS_TYPE_POWER_BUTTON:
		type_string = "Power Button";
		sprintf(name, "PWRB");
		break;
	case ACPI_BUS_TYPE_SLEEP_BUTTON:
		type_string = "Sleep Button";
		sprintf(name, "SLPB");
		break;
	}

	printk(KERN_DEBUG "Found %s %s [%p]\n", type_string, name, handle);
#endif				/*CONFIG_ACPI_DEBUG_OUTPUT */
}

static int acpi_bus_remove(struct acpi_device *dev, int rmdevice)
{
	int result = 0;
	struct acpi_driver *driver;


	if (!dev)
		return -EINVAL;

	driver = dev->driver;

	if ((driver) && (driver->ops.remove)) {

		if (driver->ops.stop) {
			result = driver->ops.stop(dev, ACPI_BUS_REMOVAL_EJECT);
			if (result)
				return result;
		}

		result = dev->driver->ops.remove(dev, ACPI_BUS_REMOVAL_EJECT);
		if (result) {
			return result;
		}

		atomic_dec(&dev->driver->references);
		dev->driver = NULL;
		acpi_driver_data(dev) = NULL;
	}

	if (!rmdevice)
		return 0;

	if (dev->flags.bus_address) {
		if ((dev->parent) && (dev->parent->ops.unbind))
			dev->parent->ops.unbind(dev);
	}

	acpi_device_unregister(dev, ACPI_BUS_REMOVAL_EJECT);

	return 0;
}

static int
acpi_add_single_object(struct acpi_device **child,
		       struct acpi_device *parent, acpi_handle handle, int type)
{
	int result = 0;
	struct acpi_device *device = NULL;


	if (!child)
		return -EINVAL;

	device = kmalloc(sizeof(struct acpi_device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}
	memset(device, 0, sizeof(struct acpi_device));

	device->handle = handle;
	device->parent = parent;

	acpi_device_get_busid(device, handle, type);

	/*
	 * Flags
	 * -----
	 * Get prior to calling acpi_bus_get_status() so we know whether
	 * or not _STA is present.  Note that we only look for object
	 * handles -- cannot evaluate objects until we know the device is
	 * present and properly initialized.
	 */
	result = acpi_bus_get_flags(device);
	if (result)
		goto end;

	/*
	 * Status
	 * ------
	 * See if the device is present.  We always assume that non-Device
	 * and non-Processor objects (e.g. thermal zones, power resources,
	 * etc.) are present, functioning, etc. (at least when parent object
	 * is present).  Note that _STA has a different meaning for some
	 * objects (e.g. power resources) so we need to be careful how we use
	 * it.
	 */
	switch (type) {
	case ACPI_BUS_TYPE_PROCESSOR:
	case ACPI_BUS_TYPE_DEVICE:
		result = acpi_bus_get_status(device);
		if (ACPI_FAILURE(result) || !device->status.present) {
			result = -ENOENT;
			goto end;
		}
		break;
	default:
		STRUCT_TO_INT(device->status) = 0x0F;
		break;
	}

	/*
	 * Initialize Device
	 * -----------------
	 * TBD: Synch with Core's enumeration/initialization process.
	 */

	/*
	 * Hardware ID, Unique ID, & Bus Address
	 * -------------------------------------
	 */
	acpi_device_set_id(device, parent, handle, type);

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

	if ((result = acpi_device_set_context(device, type)))
		goto end;

	acpi_device_get_debug_info(device, handle, type);

	acpi_device_register(device, parent);

	/*
	 * Bind _ADR-Based Devices
	 * -----------------------
	 * If there's a a bus address (_ADR) then we utilize the parent's 
	 * 'bind' function (if exists) to bind the ACPI- and natively-
	 * enumerated device representations.
	 */
	if (device->flags.bus_address) {
		if (device->parent && device->parent->ops.bind)
			device->parent->ops.bind(device);
	}

	/*
	 * Locate & Attach Driver
	 * ----------------------
	 * If there's a hardware id (_HID) or compatible ids (_CID) we check
	 * to see if there's a driver installed for this kind of device.  Note
	 * that drivers can install before or after a device is enumerated.
	 *
	 * TBD: Assumes LDM provides driver hot-plug capability.
	 */
	acpi_bus_find_driver(device);

      end:
	if (!result)
		*child = device;
	else {
		kfree(device->pnp.cid_list);
		kfree(device);
	}

	return result;
}

static int acpi_bus_scan(struct acpi_device *start, struct acpi_bus_ops *ops)
{
	acpi_status status = AE_OK;
	struct acpi_device *parent = NULL;
	struct acpi_device *child = NULL;
	acpi_handle phandle = NULL;
	acpi_handle chandle = NULL;
	acpi_object_type type = 0;
	u32 level = 1;


	if (!start)
		return -EINVAL;

	parent = start;
	phandle = start->handle;

	/*
	 * Parse through the ACPI namespace, identify all 'devices', and
	 * create a new 'struct acpi_device' for each.
	 */
	while ((level > 0) && parent) {

		status = acpi_get_next_object(ACPI_TYPE_ANY, phandle,
					      chandle, &chandle);

		/*
		 * If this scope is exhausted then move our way back up.
		 */
		if (ACPI_FAILURE(status)) {
			level--;
			chandle = phandle;
			acpi_get_parent(phandle, &phandle);
			if (parent->parent)
				parent = parent->parent;
			continue;
		}

		status = acpi_get_type(chandle, &type);
		if (ACPI_FAILURE(status))
			continue;

		/*
		 * If this is a scope object then parse it (depth-first).
		 */
		if (type == ACPI_TYPE_LOCAL_SCOPE) {
			level++;
			phandle = chandle;
			chandle = NULL;
			continue;
		}

		/*
		 * We're only interested in objects that we consider 'devices'.
		 */
		switch (type) {
		case ACPI_TYPE_DEVICE:
			type = ACPI_BUS_TYPE_DEVICE;
			break;
		case ACPI_TYPE_PROCESSOR:
			type = ACPI_BUS_TYPE_PROCESSOR;
			break;
		case ACPI_TYPE_THERMAL:
			type = ACPI_BUS_TYPE_THERMAL;
			break;
		case ACPI_TYPE_POWER:
			type = ACPI_BUS_TYPE_POWER;
			break;
		default:
			continue;
		}

		if (ops->acpi_op_add)
			status = acpi_add_single_object(&child, parent,
							chandle, type);
		else
			status = acpi_bus_get_device(chandle, &child);

		if (ACPI_FAILURE(status))
			continue;

		if (ops->acpi_op_start) {
			status = acpi_start_single_object(child);
			if (ACPI_FAILURE(status))
				continue;
		}

		/*
		 * If the device is present, enabled, and functioning then
		 * parse its scope (depth-first).  Note that we need to
		 * represent absent devices to facilitate PnP notifications
		 * -- but only the subtree head (not all of its children,
		 * which will be enumerated when the parent is inserted).
		 *
		 * TBD: Need notifications and other detection mechanisms
		 *      in place before we can fully implement this.
		 */
		if (child->status.present) {
			status = acpi_get_next_object(ACPI_TYPE_ANY, chandle,
						      NULL, NULL);
			if (ACPI_SUCCESS(status)) {
				level++;
				phandle = chandle;
				chandle = NULL;
				parent = child;
			}
		}
	}

	return 0;
}

int
acpi_bus_add(struct acpi_device **child,
	     struct acpi_device *parent, acpi_handle handle, int type)
{
	int result;
	struct acpi_bus_ops ops;


	result = acpi_add_single_object(child, parent, handle, type);
	if (!result) {
		memset(&ops, 0, sizeof(ops));
		ops.acpi_op_add = 1;
		result = acpi_bus_scan(*child, &ops);
	}
	return result;
}

EXPORT_SYMBOL(acpi_bus_add);

int acpi_bus_start(struct acpi_device *device)
{
	int result;
	struct acpi_bus_ops ops;


	if (!device)
		return -EINVAL;

	result = acpi_start_single_object(device);
	if (!result) {
		memset(&ops, 0, sizeof(ops));
		ops.acpi_op_start = 1;
		result = acpi_bus_scan(device, &ops);
	}
	return result;
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


static int acpi_bus_scan_fixed(struct acpi_device *root)
{
	int result = 0;
	struct acpi_device *device = NULL;


	if (!root)
		return -ENODEV;

	/*
	 * Enumerate all fixed-feature devices.
	 */
	if (acpi_fadt.pwr_button == 0) {
		result = acpi_add_single_object(&device, acpi_root,
						NULL,
						ACPI_BUS_TYPE_POWER_BUTTON);
		if (!result)
			result = acpi_start_single_object(device);
	}

	if (acpi_fadt.sleep_button == 0) {
		result = acpi_add_single_object(&device, acpi_root,
						NULL,
						ACPI_BUS_TYPE_SLEEP_BUTTON);
		if (!result)
			result = acpi_start_single_object(device);
	}

	return result;
}

static int __init acpi_scan_init(void)
{
	int result;
	struct acpi_bus_ops ops;


	if (acpi_disabled)
		return 0;

	result = kset_register(&acpi_namespace_kset);
	if (result < 0)
		printk(KERN_ERR PREFIX "kset_register error: %d\n", result);

	result = bus_register(&acpi_bus_type);
	if (result) {
		/* We don't want to quit even if we failed to add suspend/resume */
		printk(KERN_ERR PREFIX "Could not register bus type\n");
	}

	/*
	 * Create the root device in the bus's device tree
	 */
	result = acpi_add_single_object(&acpi_root, NULL, ACPI_ROOT_OBJECT,
					ACPI_BUS_TYPE_SYSTEM);
	if (result)
		goto Done;

	result = acpi_start_single_object(acpi_root);
	if (result)
		goto Done;

	acpi_root->dev.bus = &acpi_bus_type;
	snprintf(acpi_root->dev.bus_id, BUS_ID_SIZE, "%s", acpi_bus_type.name);
	result = device_register(&acpi_root->dev);
	if (result) {
		/* We don't want to quit even if we failed to add suspend/resume */
		printk(KERN_ERR PREFIX "Could not register device\n");
	}

	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan_fixed(acpi_root);
	if (!result) {
		memset(&ops, 0, sizeof(ops));
		ops.acpi_op_add = 1;
		ops.acpi_op_start = 1;
		result = acpi_bus_scan(acpi_root, &ops);
	}

	if (result)
		acpi_device_unregister(acpi_root, ACPI_BUS_REMOVAL_NORMAL);

      Done:
	return result;
}

subsys_initcall(acpi_scan_init);
