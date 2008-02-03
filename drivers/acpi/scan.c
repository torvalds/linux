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
ACPI_MODULE_NAME("scan");
#define STRUCT_TO_INT(s)	(*((int*)&s))
extern struct acpi_device *acpi_root;

#define ACPI_BUS_CLASS			"system_bus"
#define ACPI_BUS_HID			"LNXSYBUS"
#define ACPI_BUS_DEVICE_NAME		"System Bus"

static LIST_HEAD(acpi_device_list);
static LIST_HEAD(acpi_bus_id_list);
DEFINE_SPINLOCK(acpi_device_lock);
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

	if (!acpi_dev->flags.hardware_id)
		return -ENODEV;

	len = snprintf(modalias, size, "acpi:%s:",
		       acpi_dev->pnp.hardware_id);
	if (len < 0 || len >= size)
		return -EINVAL;
	size -= len;

	if (acpi_dev->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list;
		int i;
		int count;

		cid_list = acpi_dev->pnp.cid_list;
		for (i = 0; i < cid_list->count; i++) {
			count = snprintf(&modalias[len], size, "%s:",
					 cid_list->id[i].value);
			if (count < 0 || count >= size) {
				printk(KERN_ERR "acpi: %s cid[%i] exceeds event buffer size",
				       acpi_dev->pnp.device_name, i);
				break;
			}
			len += count;
			size -= count;
		}
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
acpi_eject_store(struct device *d, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int result;
	int ret = count;
	int islockable;
	acpi_status status;
	acpi_handle handle;
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

	islockable = acpi_device->flags.lockable;
	handle = acpi_device->handle;

	result = acpi_bus_trim(acpi_device, 1);

	if (!result)
		result = acpi_eject_operation(handle, islockable);

	if (result) {
		ret = -EBUSY;
	}
      err:
	return ret;
}

static DEVICE_ATTR(eject, 0200, NULL, acpi_eject_store);

static ssize_t
acpi_device_hid_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_dev->pnp.hardware_id);
}
static DEVICE_ATTR(hid, 0444, acpi_device_hid_show, NULL);

static ssize_t
acpi_device_path_show(struct device *dev, struct device_attribute *attr, char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_buffer path = {ACPI_ALLOCATE_BUFFER, NULL};
	int result;

	result = acpi_get_name(acpi_dev->handle, ACPI_FULL_PATHNAME, &path);
	if(result)
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
	if(dev->handle) {
		result = device_create_file(&dev->dev, &dev_attr_path);
		if(result)
			goto end;
	}

	if(dev->flags.hardware_id) {
		result = device_create_file(&dev->dev, &dev_attr_hid);
		if(result)
			goto end;
	}

	if (dev->flags.hardware_id || dev->flags.compatible_ids){
		result = device_create_file(&dev->dev, &dev_attr_modalias);
		if(result)
			goto end;
	}

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

	if (dev->flags.hardware_id || dev->flags.compatible_ids)
		device_remove_file(&dev->dev, &dev_attr_modalias);

	if(dev->flags.hardware_id)
		device_remove_file(&dev->dev, &dev_attr_hid);
	if(dev->handle)
		device_remove_file(&dev->dev, &dev_attr_path);
}
/* --------------------------------------------------------------------------
			ACPI Bus operations
   -------------------------------------------------------------------------- */

int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids)
{
	const struct acpi_device_id *id;

	if (device->flags.hardware_id) {
		for (id = ids; id->id[0]; id++) {
			if (!strcmp((char*)id->id, device->pnp.hardware_id))
				return 0;
		}
	}

	if (device->flags.compatible_ids) {
		struct acpi_compatible_id_list *cid_list = device->pnp.cid_list;
		int i;

		for (id = ids; id->id[0]; id++) {
			/* compare multiple _CID entries against driver ids */
			for (i = 0; i < cid_list->count; i++) {
				if (!strcmp((char*)id->id,
					    cid_list->id[i].value))
					return 0;
			}
		}
	}

	return -ENOENT;
}
EXPORT_SYMBOL(acpi_match_device_ids);

static void acpi_device_release(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	kfree(acpi_dev->pnp.cid_list);
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
		if (acpi_drv->ops.stop)
			acpi_drv->ops.stop(acpi_dev, acpi_dev->removal_type);
		if (acpi_drv->ops.remove)
			acpi_drv->ops.remove(acpi_dev, acpi_dev->removal_type);
	}
	acpi_dev->driver = NULL;
	acpi_driver_data(dev) = NULL;

	put_device(dev);
	return 0;
}

static void acpi_device_shutdown(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = acpi_dev->driver;

	if (acpi_drv && acpi_drv->ops.shutdown)
		acpi_drv->ops.shutdown(acpi_dev);

	return ;
}

struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.suspend	= acpi_device_suspend,
	.resume		= acpi_device_resume,
	.shutdown	= acpi_device_shutdown,
	.match		= acpi_bus_match,
	.probe		= acpi_device_probe,
	.remove		= acpi_device_remove,
	.uevent		= acpi_device_uevent,
};

static int acpi_device_register(struct acpi_device *device,
				 struct acpi_device *parent)
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
	INIT_LIST_HEAD(&device->g_list);
	INIT_LIST_HEAD(&device->wakeup_list);

	new_bus_id = kzalloc(sizeof(struct acpi_device_bus_id), GFP_KERNEL);
	if (!new_bus_id) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	spin_lock(&acpi_device_lock);
	/*
	 * Find suitable bus_id and instance number in acpi_bus_id_list
	 * If failed, create one and link it into acpi_bus_id_list
	 */
	list_for_each_entry(acpi_device_bus_id, &acpi_bus_id_list, node) {
		if(!strcmp(acpi_device_bus_id->bus_id, device->flags.hardware_id? device->pnp.hardware_id : "device")) {
			acpi_device_bus_id->instance_no ++;
			found = 1;
			kfree(new_bus_id);
			break;
		}
	}
	if(!found) {
		acpi_device_bus_id = new_bus_id;
		strcpy(acpi_device_bus_id->bus_id, device->flags.hardware_id ? device->pnp.hardware_id : "device");
		acpi_device_bus_id->instance_no = 0;
		list_add_tail(&acpi_device_bus_id->node, &acpi_bus_id_list);
	}
	sprintf(device->dev.bus_id, "%s:%02x", acpi_device_bus_id->bus_id, acpi_device_bus_id->instance_no);

	if (device->parent) {
		list_add_tail(&device->node, &device->parent->children);
		list_add_tail(&device->g_list, &device->parent->g_list);
	} else
		list_add_tail(&device->g_list, &acpi_device_list);
	if (device->wakeup.flags.valid)
		list_add_tail(&device->wakeup_list, &acpi_wakeup_device_list);
	spin_unlock(&acpi_device_lock);

	if (device->parent)
		device->dev.parent = &parent->dev;
	device->dev.bus = &acpi_bus_type;
	device_initialize(&device->dev);
	device->dev.release = &acpi_device_release;
	result = device_add(&device->dev);
	if(result) {
		printk("Error adding device %s", device->dev.bus_id);
		goto end;
	}

	result = acpi_device_setup_files(device);
	if(result)
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "Error creating sysfs interface for device %s\n", device->dev.bus_id));

	device->removal_type = ACPI_BUS_REMOVAL_NORMAL;
	return 0;
  end:
	spin_lock(&acpi_device_lock);
	if (device->parent) {
		list_del(&device->node);
		list_del(&device->g_list);
	} else
		list_del(&device->g_list);
	list_del(&device->wakeup_list);
	spin_unlock(&acpi_device_lock);
	return result;
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

	struct acpi_device_id button_device_ids[] = {
		{"PNP0C0D", 0},
		{"PNP0C0C", 0},
		{"PNP0C0E", 0},
		{"", 0},
	};


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
	if (!acpi_match_device_ids(device, button_device_ids))
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

	/* TBD: Performance management */

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

static int
acpi_video_bus_match(struct acpi_device *device)
{
	acpi_handle h_dummy1;
	acpi_handle h_dummy2;
	acpi_handle h_dummy3;


	if (!device)
		return -EINVAL;

	/* Since there is no HID, CID for ACPI Video drivers, we have
	 * to check well known required nodes for each feature we support.
	 */

	/* Does this device able to support video switching ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_DOD", &h_dummy1)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_DOS", &h_dummy2)))
		return 0;

	/* Does this device able to retrieve a video ROM ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_ROM", &h_dummy1)))
		return 0;

	/* Does this device able to configure which video head to be POSTed ? */
	if (ACPI_SUCCESS(acpi_get_handle(device->handle, "_VPO", &h_dummy1)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_GPD", &h_dummy2)) &&
	    ACPI_SUCCESS(acpi_get_handle(device->handle, "_SPD", &h_dummy3)))
		return 0;

	return -ENODEV;
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

		if(!(info->valid & (ACPI_VALID_HID | ACPI_VALID_CID))){
			status = acpi_video_bus_match(device);
			if(ACPI_SUCCESS(status))
				hid = ACPI_VIDEO_HID;

			status = acpi_bay_match(device);
			if (ACPI_SUCCESS(status))
				hid = ACPI_BAY_HID;
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

static int
acpi_add_single_object(struct acpi_device **child,
		       struct acpi_device *parent, acpi_handle handle, int type,
			struct acpi_bus_ops *ops)
{
	int result = 0;
	struct acpi_device *device = NULL;


	if (!child)
		return -EINVAL;

	device = kzalloc(sizeof(struct acpi_device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	device->handle = handle;
	device->parent = parent;
	device->bus_ops = *ops; /* workround for not call .start */


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
		STRUCT_TO_INT(device->status) =
		    ACPI_STA_DEVICE_PRESENT | ACPI_STA_DEVICE_ENABLED |
		    ACPI_STA_DEVICE_UI      | ACPI_STA_DEVICE_FUNCTIONING;
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

	result = acpi_device_register(device, parent);

	/*
	 * Bind _ADR-Based Devices when hot add
	 */
	if (device->flags.bus_address) {
		if (device->parent && device->parent->ops.bind)
			device->parent->ops.bind(device);
	}

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
				chandle, type, ops);
		else
			status = acpi_bus_get_device(chandle, &child);

		if (ACPI_FAILURE(status))
			continue;

		if (ops->acpi_op_start && !(ops->acpi_op_add)) {
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

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;

	result = acpi_add_single_object(child, parent, handle, type, &ops);
	if (!result)
		result = acpi_bus_scan(*child, &ops);

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
	struct acpi_bus_ops ops;

	if (!root)
		return -ENODEV;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;
	ops.acpi_op_start = 1;

	/*
	 * Enumerate all fixed-feature devices.
	 */
	if ((acpi_gbl_FADT.flags & ACPI_FADT_POWER_BUTTON) == 0) {
		result = acpi_add_single_object(&device, acpi_root,
						NULL,
						ACPI_BUS_TYPE_POWER_BUTTON,
						&ops);
	}

	if ((acpi_gbl_FADT.flags & ACPI_FADT_SLEEP_BUTTON) == 0) {
		result = acpi_add_single_object(&device, acpi_root,
						NULL,
						ACPI_BUS_TYPE_SLEEP_BUTTON,
						&ops);
	}

	return result;
}

int __init acpi_boot_ec_enable(void);

static int __init acpi_scan_init(void)
{
	int result;
	struct acpi_bus_ops ops;


	if (acpi_disabled)
		return 0;

	memset(&ops, 0, sizeof(ops));
	ops.acpi_op_add = 1;
	ops.acpi_op_start = 1;

	result = bus_register(&acpi_bus_type);
	if (result) {
		/* We don't want to quit even if we failed to add suspend/resume */
		printk(KERN_ERR PREFIX "Could not register bus type\n");
	}

	/*
	 * Create the root device in the bus's device tree
	 */
	result = acpi_add_single_object(&acpi_root, NULL, ACPI_ROOT_OBJECT,
					ACPI_BUS_TYPE_SYSTEM, &ops);
	if (result)
		goto Done;

	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan_fixed(acpi_root);

	/* EC region might be needed at bus_scan, so enable it now */
	acpi_boot_ec_enable();

	if (!result)
		result = acpi_bus_scan(acpi_root, &ops);

	if (result)
		acpi_device_unregister(acpi_root, ACPI_BUS_REMOVAL_NORMAL);

      Done:
	return result;
}

subsys_initcall(acpi_scan_init);
