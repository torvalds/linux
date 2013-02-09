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
#include <linux/nls.h>

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

static const char *dummy_hid = "device";

static LIST_HEAD(acpi_device_list);
static LIST_HEAD(acpi_bus_id_list);
static DEFINE_MUTEX(acpi_scan_lock);
static LIST_HEAD(acpi_scan_handlers_list);
DEFINE_MUTEX(acpi_device_lock);
LIST_HEAD(acpi_wakeup_device_list);

struct acpi_device_bus_id{
	char bus_id[15];
	unsigned int instance_no;
	struct list_head node;
};

int acpi_scan_add_handler(struct acpi_scan_handler *handler)
{
	if (!handler || !handler->attach)
		return -EINVAL;

	list_add_tail(&handler->list_node, &acpi_scan_handlers_list);
	return 0;
}

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

	if (list_empty(&acpi_dev->pnp.ids))
		return 0;

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

static void __acpi_bus_trim(struct acpi_device *start);

/**
 * acpi_bus_hot_remove_device: hot-remove a device and its children
 * @context: struct acpi_eject_event pointer (freed in this func)
 *
 * Hot-remove a device and its children. This function frees up the
 * memory space passed by arg context, so that the caller may call
 * this function asynchronously through acpi_os_hotplug_execute().
 */
void acpi_bus_hot_remove_device(void *context)
{
	struct acpi_eject_event *ej_event = (struct acpi_eject_event *) context;
	struct acpi_device *device = ej_event->device;
	acpi_handle handle = device->handle;
	acpi_handle temp;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status = AE_OK;
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE; /* default */

	mutex_lock(&acpi_scan_lock);

	ACPI_DEBUG_PRINT((ACPI_DB_INFO,
		"Hot-removing device %s...\n", dev_name(&device->dev)));

	__acpi_bus_trim(device);
	/* Device node has been released. */
	device = NULL;

	/* power off device */
	status = acpi_evaluate_object(handle, "_PS3", NULL, NULL);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		printk(KERN_WARNING PREFIX
				"Power-off device failed\n");

	if (ACPI_SUCCESS(acpi_get_handle(handle, "_LCK", &temp))) {
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
		if (status != AE_NOT_FOUND)
			acpi_handle_warn(handle, "Eject failed\n");

		/* Tell the firmware the hot-remove operation has failed. */
		acpi_evaluate_hotplug_ost(handle, ej_event->event,
					  ost_code, NULL);
	}

	mutex_unlock(&acpi_scan_lock);
	kfree(context);
	return;
}
EXPORT_SYMBOL(acpi_bus_hot_remove_device);

static ssize_t real_power_state_show(struct device *dev,
				     struct device_attribute *attr, char *buf)
{
	struct acpi_device *adev = to_acpi_device(dev);
	int state;
	int ret;

	ret = acpi_device_get_power(adev, &state);
	if (ret)
		return ret;

	return sprintf(buf, "%s\n", acpi_power_state_string(state));
}

static DEVICE_ATTR(real_power_state, 0444, real_power_state_show, NULL);

static ssize_t power_state_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct acpi_device *adev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_power_state_string(adev->power.state));
}

static DEVICE_ATTR(power_state, 0444, power_state_show, NULL);

static ssize_t
acpi_eject_store(struct device *d, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int ret = count;
	acpi_status status;
	acpi_object_type type = 0;
	struct acpi_device *acpi_device = to_acpi_device(d);
	struct acpi_eject_event *ej_event;

	if ((!count) || (buf[0] != '1')) {
		return -EINVAL;
	}
	if (!acpi_device->driver && !acpi_device->handler) {
		ret = -ENODEV;
		goto err;
	}
	status = acpi_get_type(acpi_device->handle, &type);
	if (ACPI_FAILURE(status) || (!acpi_device->flags.ejectable)) {
		ret = -ENODEV;
		goto err;
	}

	ej_event = kmalloc(sizeof(*ej_event), GFP_KERNEL);
	if (!ej_event) {
		ret = -ENOMEM;
		goto err;
	}

	ej_event->device = acpi_device;
	if (acpi_device->flags.eject_pending) {
		/* event originated from ACPI eject notification */
		ej_event->event = ACPI_NOTIFY_EJECT_REQUEST;
		acpi_device->flags.eject_pending = 0;
	} else {
		/* event originated from user */
		ej_event->event = ACPI_OST_EC_OSPM_EJECT;
		(void) acpi_evaluate_hotplug_ost(acpi_device->handle,
			ej_event->event, ACPI_OST_SC_EJECT_IN_PROGRESS, NULL);
	}

	acpi_os_hotplug_execute(acpi_bus_hot_remove_device, (void *)ej_event);
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

static ssize_t acpi_device_uid_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%s\n", acpi_dev->pnp.unique_id);
}
static DEVICE_ATTR(uid, 0444, acpi_device_uid_show, NULL);

static ssize_t acpi_device_adr_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "0x%08x\n",
		       (unsigned int)(acpi_dev->pnp.bus_address));
}
static DEVICE_ATTR(adr, 0444, acpi_device_adr_show, NULL);

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

/* sysfs file that shows description text from the ACPI _STR method */
static ssize_t description_show(struct device *dev,
				struct device_attribute *attr,
				char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int result;

	if (acpi_dev->pnp.str_obj == NULL)
		return 0;

	/*
	 * The _STR object contains a Unicode identifier for a device.
	 * We need to convert to utf-8 so it can be displayed.
	 */
	result = utf16s_to_utf8s(
		(wchar_t *)acpi_dev->pnp.str_obj->buffer.pointer,
		acpi_dev->pnp.str_obj->buffer.length,
		UTF16_LITTLE_ENDIAN, buf,
		PAGE_SIZE);

	buf[result++] = '\n';

	return result;
}
static DEVICE_ATTR(description, 0444, description_show, NULL);

static ssize_t
acpi_device_sun_show(struct device *dev, struct device_attribute *attr,
		     char *buf) {
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	return sprintf(buf, "%lu\n", acpi_dev->pnp.sun);
}
static DEVICE_ATTR(sun, 0444, acpi_device_sun_show, NULL);

static int acpi_device_setup_files(struct acpi_device *dev)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	acpi_status status;
	acpi_handle temp;
	unsigned long long sun;
	int result = 0;

	/*
	 * Devices gotten from FADT don't have a "path" attribute
	 */
	if (dev->handle) {
		result = device_create_file(&dev->dev, &dev_attr_path);
		if (result)
			goto end;
	}

	if (!list_empty(&dev->pnp.ids)) {
		result = device_create_file(&dev->dev, &dev_attr_hid);
		if (result)
			goto end;

		result = device_create_file(&dev->dev, &dev_attr_modalias);
		if (result)
			goto end;
	}

	/*
	 * If device has _STR, 'description' file is created
	 */
	status = acpi_get_handle(dev->handle, "_STR", &temp);
	if (ACPI_SUCCESS(status)) {
		status = acpi_evaluate_object(dev->handle, "_STR",
					NULL, &buffer);
		if (ACPI_FAILURE(status))
			buffer.pointer = NULL;
		dev->pnp.str_obj = buffer.pointer;
		result = device_create_file(&dev->dev, &dev_attr_description);
		if (result)
			goto end;
	}

	if (dev->flags.bus_address)
		result = device_create_file(&dev->dev, &dev_attr_adr);
	if (dev->pnp.unique_id)
		result = device_create_file(&dev->dev, &dev_attr_uid);

	status = acpi_evaluate_integer(dev->handle, "_SUN", NULL, &sun);
	if (ACPI_SUCCESS(status)) {
		dev->pnp.sun = (unsigned long)sun;
		result = device_create_file(&dev->dev, &dev_attr_sun);
		if (result)
			goto end;
	} else {
		dev->pnp.sun = (unsigned long)-1;
	}

        /*
         * If device has _EJ0, 'eject' file is created that is used to trigger
         * hot-removal function from userland.
         */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status)) {
		result = device_create_file(&dev->dev, &dev_attr_eject);
		if (result)
			return result;
	}

	if (dev->flags.power_manageable) {
		result = device_create_file(&dev->dev, &dev_attr_power_state);
		if (result)
			return result;

		if (dev->power.flags.power_resources)
			result = device_create_file(&dev->dev,
						    &dev_attr_real_power_state);
	}

end:
	return result;
}

static void acpi_device_remove_files(struct acpi_device *dev)
{
	acpi_status status;
	acpi_handle temp;

	if (dev->flags.power_manageable) {
		device_remove_file(&dev->dev, &dev_attr_power_state);
		if (dev->power.flags.power_resources)
			device_remove_file(&dev->dev,
					   &dev_attr_real_power_state);
	}

	/*
	 * If device has _STR, remove 'description' file
	 */
	status = acpi_get_handle(dev->handle, "_STR", &temp);
	if (ACPI_SUCCESS(status)) {
		kfree(dev->pnp.str_obj);
		device_remove_file(&dev->dev, &dev_attr_description);
	}
	/*
	 * If device has _EJ0, remove 'eject' file.
	 */
	status = acpi_get_handle(dev->handle, "_EJ0", &temp);
	if (ACPI_SUCCESS(status))
		device_remove_file(&dev->dev, &dev_attr_eject);

	status = acpi_get_handle(dev->handle, "_SUN", &temp);
	if (ACPI_SUCCESS(status))
		device_remove_file(&dev->dev, &dev_attr_sun);

	if (dev->pnp.unique_id)
		device_remove_file(&dev->dev, &dev_attr_uid);
	if (dev->flags.bus_address)
		device_remove_file(&dev->dev, &dev_attr_adr);
	device_remove_file(&dev->dev, &dev_attr_modalias);
	device_remove_file(&dev->dev, &dev_attr_hid);
	if (dev->handle)
		device_remove_file(&dev->dev, &dev_attr_path);
}
/* --------------------------------------------------------------------------
			ACPI Bus operations
   -------------------------------------------------------------------------- */

static const struct acpi_device_id *__acpi_match_device(
	struct acpi_device *device, const struct acpi_device_id *ids)
{
	const struct acpi_device_id *id;
	struct acpi_hardware_id *hwid;

	/*
	 * If the device is not present, it is unnecessary to load device
	 * driver for it.
	 */
	if (!device->status.present)
		return NULL;

	for (id = ids; id->id[0]; id++)
		list_for_each_entry(hwid, &device->pnp.ids, list)
			if (!strcmp((char *) id->id, hwid->id))
				return id;

	return NULL;
}

/**
 * acpi_match_device - Match a struct device against a given list of ACPI IDs
 * @ids: Array of struct acpi_device_id object to match against.
 * @dev: The device structure to match.
 *
 * Check if @dev has a valid ACPI handle and if there is a struct acpi_device
 * object for that handle and use that object to match against a given list of
 * device IDs.
 *
 * Return a pointer to the first matching ID on success or %NULL on failure.
 */
const struct acpi_device_id *acpi_match_device(const struct acpi_device_id *ids,
					       const struct device *dev)
{
	struct acpi_device *adev;

	if (!ids || !ACPI_HANDLE(dev)
	    || ACPI_FAILURE(acpi_bus_get_device(ACPI_HANDLE(dev), &adev)))
		return NULL;

	return __acpi_match_device(adev, ids);
}
EXPORT_SYMBOL_GPL(acpi_match_device);

int acpi_match_device_ids(struct acpi_device *device,
			  const struct acpi_device_id *ids)
{
	return __acpi_match_device(device, ids) ? 0 : -ENOENT;
}
EXPORT_SYMBOL(acpi_match_device_ids);

void acpi_free_ids(struct acpi_device *device)
{
	struct acpi_hardware_id *id, *tmp;

	list_for_each_entry_safe(id, tmp, &device->pnp.ids, list) {
		kfree(id->id);
		kfree(id);
	}
	kfree(device->pnp.unique_id);
}

static void acpi_free_power_resources_lists(struct acpi_device *device)
{
	int i;

	if (device->wakeup.flags.valid)
		acpi_power_resources_list_free(&device->wakeup.resources);

	if (!device->flags.power_manageable)
		return;

	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3_HOT; i++) {
		struct acpi_device_power_state *ps = &device->power.states[i];
		acpi_power_resources_list_free(&ps->resources);
	}
}

static void acpi_device_release(struct device *dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);

	acpi_free_ids(acpi_dev);
	acpi_free_power_resources_lists(acpi_dev);
	kfree(acpi_dev);
}

static int acpi_bus_match(struct device *dev, struct device_driver *drv)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(drv);

	return acpi_dev->flags.match_driver
		&& !acpi_match_device_ids(acpi_dev, acpi_drv->ids);
}

static int acpi_device_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	int len;

	if (list_empty(&acpi_dev->pnp.ids))
		return 0;

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
static int acpi_device_probe(struct device * dev)
{
	struct acpi_device *acpi_dev = to_acpi_device(dev);
	struct acpi_driver *acpi_drv = to_acpi_driver(dev->driver);
	int ret;

	ret = acpi_bus_driver_init(acpi_dev, acpi_drv);
	if (!ret) {
		if (acpi_drv->ops.notify) {
			ret = acpi_device_install_notify_handler(acpi_dev);
			if (ret) {
				if (acpi_drv->ops.remove)
					acpi_drv->ops.remove(acpi_dev);
				acpi_dev->driver = NULL;
				acpi_dev->driver_data = NULL;
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
			acpi_drv->ops.remove(acpi_dev);
	}
	acpi_dev->driver = NULL;
	acpi_dev->driver_data = NULL;

	put_device(dev);
	return 0;
}

struct bus_type acpi_bus_type = {
	.name		= "acpi",
	.match		= acpi_bus_match,
	.probe		= acpi_device_probe,
	.remove		= acpi_device_remove,
	.uevent		= acpi_device_uevent,
};

int acpi_device_add(struct acpi_device *device,
		    void (*release)(struct device *))
{
	int result;
	struct acpi_device_bus_id *acpi_device_bus_id, *new_bus_id;
	int found = 0;

	if (device->handle) {
		acpi_status status;

		status = acpi_attach_data(device->handle, acpi_bus_data_handler,
					  device);
		if (ACPI_FAILURE(status)) {
			acpi_handle_err(device->handle,
					"Unable to attach device data\n");
			return -ENODEV;
		}
	}

	/*
	 * Linkage
	 * -------
	 * Link this device to its parent and siblings.
	 */
	INIT_LIST_HEAD(&device->children);
	INIT_LIST_HEAD(&device->node);
	INIT_LIST_HEAD(&device->wakeup_list);
	INIT_LIST_HEAD(&device->physical_node_list);
	mutex_init(&device->physical_node_lock);
	INIT_LIST_HEAD(&device->power_dependent);

	new_bus_id = kzalloc(sizeof(struct acpi_device_bus_id), GFP_KERNEL);
	if (!new_bus_id) {
		pr_err(PREFIX "Memory allocation error\n");
		result = -ENOMEM;
		goto err_detach;
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
	device->dev.release = release;
	result = device_add(&device->dev);
	if (result) {
		dev_err(&device->dev, "Error registering device\n");
		goto err;
	}

	result = acpi_device_setup_files(device);
	if (result)
		printk(KERN_ERR PREFIX "Error creating sysfs interface for device %s\n",
		       dev_name(&device->dev));

	device->removal_type = ACPI_BUS_REMOVAL_NORMAL;
	return 0;

 err:
	mutex_lock(&acpi_device_lock);
	if (device->parent)
		list_del(&device->node);
	list_del(&device->wakeup_list);
	mutex_unlock(&acpi_device_lock);

 err_detach:
	acpi_detach_data(device->handle, acpi_bus_data_handler);
	return result;
}

static void acpi_device_unregister(struct acpi_device *device)
{
	mutex_lock(&acpi_device_lock);
	if (device->parent)
		list_del(&device->node);

	list_del(&device->wakeup_list);
	mutex_unlock(&acpi_device_lock);

	acpi_detach_data(device->handle, acpi_bus_data_handler);

	acpi_power_add_remove_device(device, false);
	acpi_device_remove_files(device);
	if (device->remove)
		device->remove(device);

	device_del(&device->dev);
	/*
	 * Drop the reference counts of all power resources the device depends
	 * on and turn off the ones that have no more references.
	 */
	acpi_power_transition(device, ACPI_STATE_D3_COLD);
	put_device(&device->dev);
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
	struct acpi_device *device = NULL;
	acpi_status status;

	/*
	 * Fixed hardware devices do not appear in the namespace and do not
	 * have handles, but we fabricate acpi_devices for them, so we have
	 * to deal with them specially.
	 */
	if (!handle)
		return acpi_root;

	do {
		status = acpi_get_parent(handle, &handle);
		if (ACPI_FAILURE(status))
			return status == AE_NULL_ENTRY ? NULL : acpi_root;
	} while (acpi_bus_get_device(handle, &device));
	return device;
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

static int acpi_bus_extract_wakeup_device_power_package(acpi_handle handle,
					struct acpi_device_wakeup *wakeup)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *package = NULL;
	union acpi_object *element = NULL;
	acpi_status status;
	int err = -ENODATA;

	if (!wakeup)
		return -EINVAL;

	INIT_LIST_HEAD(&wakeup->resources);

	/* _PRW */
	status = acpi_evaluate_object(handle, "_PRW", NULL, &buffer);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, status, "Evaluating _PRW"));
		return err;
	}

	package = (union acpi_object *)buffer.pointer;

	if (!package || package->package.count < 2)
		goto out;

	element = &(package->package.elements[0]);
	if (!element)
		goto out;

	if (element->type == ACPI_TYPE_PACKAGE) {
		if ((element->package.count < 2) ||
		    (element->package.elements[0].type !=
		     ACPI_TYPE_LOCAL_REFERENCE)
		    || (element->package.elements[1].type != ACPI_TYPE_INTEGER))
			goto out;

		wakeup->gpe_device =
		    element->package.elements[0].reference.handle;
		wakeup->gpe_number =
		    (u32) element->package.elements[1].integer.value;
	} else if (element->type == ACPI_TYPE_INTEGER) {
		wakeup->gpe_device = NULL;
		wakeup->gpe_number = element->integer.value;
	} else {
		goto out;
	}

	element = &(package->package.elements[1]);
	if (element->type != ACPI_TYPE_INTEGER)
		goto out;

	wakeup->sleep_state = element->integer.value;

	err = acpi_extract_power_resources(package, 2, &wakeup->resources);
	if (err)
		goto out;

	if (!list_empty(&wakeup->resources)) {
		int sleep_state;

		sleep_state = acpi_power_min_system_level(&wakeup->resources);
		if (sleep_state < wakeup->sleep_state) {
			acpi_handle_warn(handle, "Overriding _PRW sleep state "
					 "(S%d) by S%d from power resources\n",
					 (int)wakeup->sleep_state, sleep_state);
			wakeup->sleep_state = sleep_state;
		}
	}
	acpi_setup_gpe_for_wake(handle, wakeup->gpe_device, wakeup->gpe_number);

 out:
	kfree(buffer.pointer);
	return err;
}

static void acpi_bus_set_run_wake_flags(struct acpi_device *device)
{
	struct acpi_device_id button_device_ids[] = {
		{"PNP0C0C", 0},
		{"PNP0C0D", 0},
		{"PNP0C0E", 0},
		{"", 0},
	};
	acpi_status status;
	acpi_event_status event_status;

	device->wakeup.flags.notifier_present = 0;

	/* Power button, Lid switch always enable wakeup */
	if (!acpi_match_device_ids(device, button_device_ids)) {
		device->wakeup.flags.run_wake = 1;
		if (!acpi_match_device_ids(device, &button_device_ids[1])) {
			/* Do not use Lid/sleep button for S5 wakeup */
			if (device->wakeup.sleep_state == ACPI_STATE_S5)
				device->wakeup.sleep_state = ACPI_STATE_S4;
		}
		device_set_wakeup_capable(&device->dev, true);
		return;
	}

	status = acpi_get_gpe_status(device->wakeup.gpe_device,
					device->wakeup.gpe_number,
						&event_status);
	if (status == AE_OK)
		device->wakeup.flags.run_wake =
				!!(event_status & ACPI_EVENT_FLAG_HANDLE);
}

static void acpi_bus_get_wakeup_device_flags(struct acpi_device *device)
{
	acpi_handle temp;
	acpi_status status = 0;
	int err;

	/* Presence of _PRW indicates wake capable */
	status = acpi_get_handle(device->handle, "_PRW", &temp);
	if (ACPI_FAILURE(status))
		return;

	err = acpi_bus_extract_wakeup_device_power_package(device->handle,
							   &device->wakeup);
	if (err) {
		dev_err(&device->dev, "_PRW evaluation error: %d\n", err);
		return;
	}

	device->wakeup.flags.valid = 1;
	device->wakeup.prepare_count = 0;
	acpi_bus_set_run_wake_flags(device);
	/* Call _PSW/_DSW object to disable its ability to wake the sleeping
	 * system for the ACPI device with the _PRW object.
	 * The _PSW object is depreciated in ACPI 3.0 and is replaced by _DSW.
	 * So it is necessary to call _DSW object first. Only when it is not
	 * present will the _PSW object used.
	 */
	err = acpi_device_sleep_wake(device, 0, 0, 0);
	if (err)
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				"error in _DSW or _PSW evaluation\n"));
}

static void acpi_bus_init_power_state(struct acpi_device *device, int state)
{
	struct acpi_device_power_state *ps = &device->power.states[state];
	char pathname[5] = { '_', 'P', 'R', '0' + state, '\0' };
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	acpi_handle handle;
	acpi_status status;

	INIT_LIST_HEAD(&ps->resources);

	/* Evaluate "_PRx" to get referenced power resources */
	status = acpi_evaluate_object(device->handle, pathname, NULL, &buffer);
	if (ACPI_SUCCESS(status)) {
		union acpi_object *package = buffer.pointer;

		if (buffer.length && package
		    && package->type == ACPI_TYPE_PACKAGE
		    && package->package.count) {
			int err = acpi_extract_power_resources(package, 0,
							       &ps->resources);
			if (!err)
				device->power.flags.power_resources = 1;
		}
		ACPI_FREE(buffer.pointer);
	}

	/* Evaluate "_PSx" to see if we can do explicit sets */
	pathname[2] = 'S';
	status = acpi_get_handle(device->handle, pathname, &handle);
	if (ACPI_SUCCESS(status))
		ps->flags.explicit_set = 1;

	/*
	 * State is valid if there are means to put the device into it.
	 * D3hot is only valid if _PR3 present.
	 */
	if (!list_empty(&ps->resources)
	    || (ps->flags.explicit_set && state < ACPI_STATE_D3_HOT)) {
		ps->flags.valid = 1;
		ps->flags.os_accessible = 1;
	}

	ps->power = -1;		/* Unknown - driver assigned */
	ps->latency = -1;	/* Unknown - driver assigned */
}

static void acpi_bus_get_power_flags(struct acpi_device *device)
{
	acpi_status status;
	acpi_handle handle;
	u32 i;

	/* Presence of _PS0|_PR0 indicates 'power manageable' */
	status = acpi_get_handle(device->handle, "_PS0", &handle);
	if (ACPI_FAILURE(status)) {
		status = acpi_get_handle(device->handle, "_PR0", &handle);
		if (ACPI_FAILURE(status))
			return;
	}

	device->flags.power_manageable = 1;

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
	for (i = ACPI_STATE_D0; i <= ACPI_STATE_D3_HOT; i++)
		acpi_bus_init_power_state(device, i);

	INIT_LIST_HEAD(&device->power.states[ACPI_STATE_D3_COLD].resources);

	/* Set defaults for D0 and D3 states (always valid) */
	device->power.states[ACPI_STATE_D0].flags.valid = 1;
	device->power.states[ACPI_STATE_D0].power = 100;
	device->power.states[ACPI_STATE_D3].flags.valid = 1;
	device->power.states[ACPI_STATE_D3].power = 0;

	/* Set D3cold's explicit_set flag if _PS3 exists. */
	if (device->power.states[ACPI_STATE_D3_HOT].flags.explicit_set)
		device->power.states[ACPI_STATE_D3_COLD].flags.explicit_set = 1;

	/* Presence of _PS3 or _PRx means we can put the device into D3 cold */
	if (device->power.states[ACPI_STATE_D3_HOT].flags.explicit_set ||
			device->power.flags.power_resources)
		device->power.states[ACPI_STATE_D3_COLD].flags.os_accessible = 1;

	acpi_bus_init_power(device);
}

static void acpi_bus_get_flags(struct acpi_device *device)
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

const char *acpi_device_hid(struct acpi_device *device)
{
	struct acpi_hardware_id *hid;

	if (list_empty(&device->pnp.ids))
		return dummy_hid;

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

	id->id = kstrdup(dev_id, GFP_KERNEL);
	if (!id->id) {
		kfree(id);
		return;
	}

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
	struct acpi_pnp_device_id_list *cid_list;
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
		if (info->valid & ACPI_VALID_UID)
			device->pnp.unique_id = kstrdup(info->unique_id.string,
							GFP_KERNEL);

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
		else if (list_empty(&device->pnp.ids) &&
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
}

void acpi_init_device_object(struct acpi_device *device, acpi_handle handle,
			     int type, unsigned long long sta)
{
	INIT_LIST_HEAD(&device->pnp.ids);
	device->device_type = type;
	device->handle = handle;
	device->parent = acpi_bus_get_parent(handle);
	STRUCT_TO_INT(device->status) = sta;
	acpi_device_get_busid(device);
	acpi_device_set_id(device);
	acpi_bus_get_flags(device);
	device->flags.match_driver = false;
	device_initialize(&device->dev);
	dev_set_uevent_suppress(&device->dev, true);
}

void acpi_device_add_finalize(struct acpi_device *device)
{
	device->flags.match_driver = true;
	dev_set_uevent_suppress(&device->dev, false);
	kobject_uevent(&device->dev.kobj, KOBJ_ADD);
}

static int acpi_add_single_object(struct acpi_device **child,
				  acpi_handle handle, int type,
				  unsigned long long sta)
{
	int result;
	struct acpi_device *device;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };

	device = kzalloc(sizeof(struct acpi_device), GFP_KERNEL);
	if (!device) {
		printk(KERN_ERR PREFIX "Memory allocation error\n");
		return -ENOMEM;
	}

	acpi_init_device_object(device, handle, type, sta);
	acpi_bus_get_power_flags(device);
	acpi_bus_get_wakeup_device_flags(device);

	result = acpi_device_add(device, acpi_device_release);
	if (result) {
		acpi_device_release(&device->dev);
		return result;
	}

	acpi_power_add_remove_device(device, true);
	acpi_device_add_finalize(device);
	acpi_get_name(handle, ACPI_FULL_PATHNAME, &buffer);
	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Added %s [%s] parent %s\n",
		dev_name(&device->dev), (char *) buffer.pointer,
		device->parent ? dev_name(&device->parent->dev) : "(null)"));
	kfree(buffer.pointer);
	*child = device;
	return 0;
}

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

static acpi_status acpi_bus_check_add(acpi_handle handle, u32 lvl_not_used,
				      void *not_used, void **return_value)
{
	struct acpi_device *device = NULL;
	int type;
	unsigned long long sta;
	acpi_status status;
	int result;

	acpi_bus_get_device(handle, &device);
	if (device)
		goto out;

	result = acpi_bus_type_and_status(handle, &type, &sta);
	if (result)
		return AE_OK;

	if (type == ACPI_BUS_TYPE_POWER) {
		acpi_add_power_resource(handle);
		return AE_OK;
	}

	if (!(sta & ACPI_STA_DEVICE_PRESENT) &&
	    !(sta & ACPI_STA_DEVICE_FUNCTIONING)) {
		struct acpi_device_wakeup wakeup;
		acpi_handle temp;

		status = acpi_get_handle(handle, "_PRW", &temp);
		if (ACPI_SUCCESS(status)) {
			acpi_bus_extract_wakeup_device_power_package(handle,
								     &wakeup);
			acpi_power_resources_list_free(&wakeup.resources);
		}
		return AE_CTRL_DEPTH;
	}

	acpi_add_single_object(&device, handle, type, sta);
	if (!device)
		return AE_CTRL_DEPTH;

 out:
	if (!*return_value)
		*return_value = device;

	return AE_OK;
}

static int acpi_scan_do_attach_handler(struct acpi_device *device, char *id)
{
	struct acpi_scan_handler *handler;

	list_for_each_entry(handler, &acpi_scan_handlers_list, list_node) {
		const struct acpi_device_id *devid;

		for (devid = handler->ids; devid->id[0]; devid++) {
			int ret;

			if (strcmp((char *)devid->id, id))
				continue;

			ret = handler->attach(device, devid);
			if (ret > 0) {
				device->handler = handler;
				return ret;
			} else if (ret < 0) {
				return ret;
			}
		}
	}
	return 0;
}

static int acpi_scan_attach_handler(struct acpi_device *device)
{
	struct acpi_hardware_id *hwid;
	int ret = 0;

	list_for_each_entry(hwid, &device->pnp.ids, list) {
		ret = acpi_scan_do_attach_handler(device, hwid->id);
		if (ret)
			break;

	}
	return ret;
}

static acpi_status acpi_bus_device_attach(acpi_handle handle, u32 lvl_not_used,
					  void *not_used, void **ret_not_used)
{
	struct acpi_device *device;
	unsigned long long sta_not_used;
	int ret;

	/*
	 * Ignore errors ignored by acpi_bus_check_add() to avoid terminating
	 * namespace walks prematurely.
	 */
	if (acpi_bus_type_and_status(handle, &ret, &sta_not_used))
		return AE_OK;

	if (acpi_bus_get_device(handle, &device))
		return AE_CTRL_DEPTH;

	ret = acpi_scan_attach_handler(device);
	if (ret)
		return ret > 0 ? AE_OK : AE_CTRL_DEPTH;

	ret = device_attach(&device->dev);
	return ret >= 0 ? AE_OK : AE_CTRL_DEPTH;
}

/**
 * acpi_bus_scan - Add ACPI device node objects in a given namespace scope.
 * @handle: Root of the namespace scope to scan.
 *
 * Scan a given ACPI tree (probably recently hot-plugged) and create and add
 * found devices.
 *
 * If no devices were found, -ENODEV is returned, but it does not mean that
 * there has been a real error.  There just have been no suitable ACPI objects
 * in the table trunk from which the kernel could create a device and add an
 * appropriate driver.
 */
int acpi_bus_scan(acpi_handle handle)
{
	void *device = NULL;
	int error = 0;

	mutex_lock(&acpi_scan_lock);

	if (ACPI_SUCCESS(acpi_bus_check_add(handle, 0, NULL, &device)))
		acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				    acpi_bus_check_add, NULL, NULL, &device);

	if (!device)
		error = -ENODEV;
	else if (ACPI_SUCCESS(acpi_bus_device_attach(handle, 0, NULL, NULL)))
		acpi_walk_namespace(ACPI_TYPE_ANY, handle, ACPI_UINT32_MAX,
				    acpi_bus_device_attach, NULL, NULL, NULL);

	mutex_unlock(&acpi_scan_lock);
	return error;
}
EXPORT_SYMBOL(acpi_bus_scan);

static acpi_status acpi_bus_device_detach(acpi_handle handle, u32 lvl_not_used,
					  void *not_used, void **ret_not_used)
{
	struct acpi_device *device = NULL;

	if (!acpi_bus_get_device(handle, &device)) {
		struct acpi_scan_handler *dev_handler = device->handler;

		device->removal_type = ACPI_BUS_REMOVAL_EJECT;
		if (dev_handler) {
			if (dev_handler->detach)
				dev_handler->detach(device);

			device->handler = NULL;
		} else {
			device_release_driver(&device->dev);
		}
	}
	return AE_OK;
}

static acpi_status acpi_bus_remove(acpi_handle handle, u32 lvl_not_used,
				   void *not_used, void **ret_not_used)
{
	struct acpi_device *device = NULL;

	if (!acpi_bus_get_device(handle, &device))
		acpi_device_unregister(device);

	return AE_OK;
}

static void __acpi_bus_trim(struct acpi_device *start)
{
	/*
	 * Execute acpi_bus_device_detach() as a post-order callback to detach
	 * all ACPI drivers from the device nodes being removed.
	 */
	acpi_walk_namespace(ACPI_TYPE_ANY, start->handle, ACPI_UINT32_MAX, NULL,
			    acpi_bus_device_detach, NULL, NULL);
	acpi_bus_device_detach(start->handle, 0, NULL, NULL);
	/*
	 * Execute acpi_bus_remove() as a post-order callback to remove device
	 * nodes in the given namespace scope.
	 */
	acpi_walk_namespace(ACPI_TYPE_ANY, start->handle, ACPI_UINT32_MAX, NULL,
			    acpi_bus_remove, NULL, NULL);
	acpi_bus_remove(start->handle, 0, NULL, NULL);
}

void acpi_bus_trim(struct acpi_device *start)
{
	mutex_lock(&acpi_scan_lock);
	__acpi_bus_trim(start);
	mutex_unlock(&acpi_scan_lock);
}
EXPORT_SYMBOL_GPL(acpi_bus_trim);

static int acpi_bus_scan_fixed(void)
{
	int result = 0;

	/*
	 * Enumerate all fixed-feature devices.
	 */
	if (!(acpi_gbl_FADT.flags & ACPI_FADT_POWER_BUTTON)) {
		struct acpi_device *device = NULL;

		result = acpi_add_single_object(&device, NULL,
						ACPI_BUS_TYPE_POWER_BUTTON,
						ACPI_STA_DEFAULT);
		if (result)
			return result;

		result = device_attach(&device->dev);
		if (result < 0)
			return result;

		device_init_wakeup(&device->dev, true);
	}

	if (!(acpi_gbl_FADT.flags & ACPI_FADT_SLEEP_BUTTON)) {
		struct acpi_device *device = NULL;

		result = acpi_add_single_object(&device, NULL,
						ACPI_BUS_TYPE_SLEEP_BUTTON,
						ACPI_STA_DEFAULT);
		if (result)
			return result;

		result = device_attach(&device->dev);
	}

	return result < 0 ? result : 0;
}

int __init acpi_scan_init(void)
{
	int result;

	result = bus_register(&acpi_bus_type);
	if (result) {
		/* We don't want to quit even if we failed to add suspend/resume */
		printk(KERN_ERR PREFIX "Could not register bus type\n");
	}

	acpi_pci_root_init();
	acpi_pci_link_init();
	acpi_platform_init();
	acpi_csrt_init();
	acpi_container_init();

	/*
	 * Enumerate devices in the ACPI namespace.
	 */
	result = acpi_bus_scan(ACPI_ROOT_OBJECT);
	if (result)
		return result;

	result = acpi_bus_get_device(ACPI_ROOT_OBJECT, &acpi_root);
	if (result)
		return result;

	result = acpi_bus_scan_fixed();
	if (result) {
		acpi_device_unregister(acpi_root);
		return result;
	}

	acpi_update_all_gpes();
	return 0;
}
