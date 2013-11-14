/*
 *  dock.c - ACPI dock station driver
 *
 *  Copyright (C) 2006 Kristen Carlson Accardi <kristen.c.accardi@intel.com>
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

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/stddef.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define PREFIX "ACPI: "

#define ACPI_DOCK_DRIVER_DESCRIPTION "ACPI Dock Station Driver"

ACPI_MODULE_NAME("dock");
MODULE_AUTHOR("Kristen Carlson Accardi");
MODULE_DESCRIPTION(ACPI_DOCK_DRIVER_DESCRIPTION);
MODULE_LICENSE("GPL");

static bool immediate_undock = 1;
module_param(immediate_undock, bool, 0644);
MODULE_PARM_DESC(immediate_undock, "1 (default) will cause the driver to "
	"undock immediately when the undock button is pressed, 0 will cause"
	" the driver to wait for userspace to write the undock sysfs file "
	" before undocking");

static const struct acpi_device_id dock_device_ids[] = {
	{"LNXDOCK", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, dock_device_ids);

struct dock_station {
	acpi_handle handle;
	unsigned long last_dock_time;
	u32 flags;
	struct list_head dependent_devices;

	struct list_head sibling;
	struct platform_device *dock_device;
};
static LIST_HEAD(dock_stations);
static int dock_station_count;
static DEFINE_MUTEX(hotplug_lock);

struct dock_dependent_device {
	struct list_head list;
	acpi_handle handle;
	const struct acpi_dock_ops *hp_ops;
	void *hp_context;
	unsigned int hp_refcount;
	void (*hp_release)(void *);
};

#define DOCK_DOCKING	0x00000001
#define DOCK_UNDOCKING  0x00000002
#define DOCK_IS_DOCK	0x00000010
#define DOCK_IS_ATA	0x00000020
#define DOCK_IS_BAT	0x00000040
#define DOCK_EVENT	3
#define UNDOCK_EVENT	2

enum dock_callback_type {
	DOCK_CALL_HANDLER,
	DOCK_CALL_FIXUP,
	DOCK_CALL_UEVENT,
};

/*****************************************************************************
 *                         Dock Dependent device functions                   *
 *****************************************************************************/
/**
 * add_dock_dependent_device - associate a device with the dock station
 * @ds: The dock station
 * @handle: handle of the dependent device
 *
 * Add the dependent device to the dock's dependent device list.
 */
static int __init
add_dock_dependent_device(struct dock_station *ds, acpi_handle handle)
{
	struct dock_dependent_device *dd;

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	dd->handle = handle;
	INIT_LIST_HEAD(&dd->list);
	list_add_tail(&dd->list, &ds->dependent_devices);

	return 0;
}

static void remove_dock_dependent_devices(struct dock_station *ds)
{
	struct dock_dependent_device *dd, *aux;

	list_for_each_entry_safe(dd, aux, &ds->dependent_devices, list) {
		list_del(&dd->list);
		kfree(dd);
	}
}

/**
 * dock_init_hotplug - Initialize a hotplug device on a docking station.
 * @dd: Dock-dependent device.
 * @ops: Dock operations to attach to the dependent device.
 * @context: Data to pass to the @ops callbacks and @release.
 * @init: Optional initialization routine to run after setting up context.
 * @release: Optional release routine to run on removal.
 */
static int dock_init_hotplug(struct dock_dependent_device *dd,
			     const struct acpi_dock_ops *ops, void *context,
			     void (*init)(void *), void (*release)(void *))
{
	int ret = 0;

	mutex_lock(&hotplug_lock);
	if (WARN_ON(dd->hp_context)) {
		ret = -EEXIST;
	} else {
		dd->hp_refcount = 1;
		dd->hp_ops = ops;
		dd->hp_context = context;
		dd->hp_release = release;
		if (init)
			init(context);
	}
	mutex_unlock(&hotplug_lock);
	return ret;
}

/**
 * dock_release_hotplug - Decrement hotplug reference counter of dock device.
 * @dd: Dock-dependent device.
 *
 * Decrement the reference counter of @dd and if 0, detach its hotplug
 * operations from it, reset its context pointer and run the optional release
 * routine if present.
 */
static void dock_release_hotplug(struct dock_dependent_device *dd)
{
	mutex_lock(&hotplug_lock);
	if (dd->hp_context && !--dd->hp_refcount) {
		void (*release)(void *) = dd->hp_release;
		void *context = dd->hp_context;

		dd->hp_ops = NULL;
		dd->hp_context = NULL;
		dd->hp_release = NULL;
		if (release)
			release(context);
	}
	mutex_unlock(&hotplug_lock);
}

static void dock_hotplug_event(struct dock_dependent_device *dd, u32 event,
			       enum dock_callback_type cb_type)
{
	acpi_notify_handler cb = NULL;
	bool run = false;

	mutex_lock(&hotplug_lock);

	if (dd->hp_context) {
		run = true;
		dd->hp_refcount++;
		if (dd->hp_ops) {
			switch (cb_type) {
			case DOCK_CALL_FIXUP:
				cb = dd->hp_ops->fixup;
				break;
			case DOCK_CALL_UEVENT:
				cb = dd->hp_ops->uevent;
				break;
			default:
				cb = dd->hp_ops->handler;
			}
		}
	}

	mutex_unlock(&hotplug_lock);

	if (!run)
		return;

	if (cb)
		cb(dd->handle, event, dd->hp_context);

	dock_release_hotplug(dd);
}

/**
 * find_dock_dependent_device - get a device dependent on this dock
 * @ds: the dock station
 * @handle: the acpi_handle of the device we want
 *
 * iterate over the dependent device list for this dock.  If the
 * dependent device matches the handle, return.
 */
static struct dock_dependent_device *
find_dock_dependent_device(struct dock_station *ds, acpi_handle handle)
{
	struct dock_dependent_device *dd;

	list_for_each_entry(dd, &ds->dependent_devices, list)
		if (handle == dd->handle)
			return dd;

	return NULL;
}

/*****************************************************************************
 *                         Dock functions                                    *
 *****************************************************************************/
static int __init is_battery(acpi_handle handle)
{
	struct acpi_device_info *info;
	int ret = 1;

	if (!ACPI_SUCCESS(acpi_get_object_info(handle, &info)))
		return 0;
	if (!(info->valid & ACPI_VALID_HID))
		ret = 0;
	else
		ret = !strcmp("PNP0C0A", info->hardware_id.string);

	kfree(info);
	return ret;
}

/* Check whether ACPI object is an ejectable battery or disk bay */
static bool __init is_ejectable_bay(acpi_handle handle)
{
	if (acpi_has_method(handle, "_EJ0") && is_battery(handle))
		return true;

	return acpi_bay_match(handle);
}

/**
 * is_dock_device - see if a device is on a dock station
 * @handle: acpi handle of the device
 *
 * If this device is either the dock station itself,
 * or is a device dependent on the dock station, then it
 * is a dock device
 */
int is_dock_device(acpi_handle handle)
{
	struct dock_station *dock_station;

	if (!dock_station_count)
		return 0;

	if (acpi_dock_match(handle))
		return 1;

	list_for_each_entry(dock_station, &dock_stations, sibling)
		if (find_dock_dependent_device(dock_station, handle))
			return 1;

	return 0;
}
EXPORT_SYMBOL_GPL(is_dock_device);

/**
 * dock_present - see if the dock station is present.
 * @ds: the dock station
 *
 * execute the _STA method.  note that present does not
 * imply that we are docked.
 */
static int dock_present(struct dock_station *ds)
{
	unsigned long long sta;
	acpi_status status;

	if (ds) {
		status = acpi_evaluate_integer(ds->handle, "_STA", NULL, &sta);
		if (ACPI_SUCCESS(status) && sta)
			return 1;
	}
	return 0;
}

/**
 * dock_create_acpi_device - add new devices to acpi
 * @handle - handle of the device to add
 *
 *  This function will create a new acpi_device for the given
 *  handle if one does not exist already.  This should cause
 *  acpi to scan for drivers for the given devices, and call
 *  matching driver's add routine.
 */
static void dock_create_acpi_device(acpi_handle handle)
{
	struct acpi_device *device;
	int ret;

	if (acpi_bus_get_device(handle, &device)) {
		/*
		 * no device created for this object,
		 * so we should create one.
		 */
		ret = acpi_bus_scan(handle);
		if (ret)
			pr_debug("error adding bus, %x\n", -ret);
	}
}

/**
 * dock_remove_acpi_device - remove the acpi_device struct from acpi
 * @handle - the handle of the device to remove
 *
 *  Tell acpi to remove the acpi_device.  This should cause any loaded
 *  driver to have it's remove routine called.
 */
static void dock_remove_acpi_device(acpi_handle handle)
{
	struct acpi_device *device;

	if (!acpi_bus_get_device(handle, &device))
		acpi_bus_trim(device);
}

/**
 * hot_remove_dock_devices - Remove dock station devices.
 * @ds: Dock station.
 */
static void hot_remove_dock_devices(struct dock_station *ds)
{
	struct dock_dependent_device *dd;

	/*
	 * Walk the list in reverse order so that devices that have been added
	 * last are removed first (in case there are some indirect dependencies
	 * between them).
	 */
	list_for_each_entry_reverse(dd, &ds->dependent_devices, list)
		dock_hotplug_event(dd, ACPI_NOTIFY_EJECT_REQUEST, false);

	list_for_each_entry_reverse(dd, &ds->dependent_devices, list)
		dock_remove_acpi_device(dd->handle);
}

/**
 * hotplug_dock_devices - Insert devices on a dock station.
 * @ds: the dock station
 * @event: either bus check or device check request
 *
 * Some devices on the dock station need to have drivers called
 * to perform hotplug operations after a dock event has occurred.
 * Traverse the list of dock devices that have registered a
 * hotplug handler, and call the handler.
 */
static void hotplug_dock_devices(struct dock_station *ds, u32 event)
{
	struct dock_dependent_device *dd;

	/* Call driver specific post-dock fixups. */
	list_for_each_entry(dd, &ds->dependent_devices, list)
		dock_hotplug_event(dd, event, DOCK_CALL_FIXUP);

	/* Call driver specific hotplug functions. */
	list_for_each_entry(dd, &ds->dependent_devices, list)
		dock_hotplug_event(dd, event, DOCK_CALL_HANDLER);

	/*
	 * Now make sure that an acpi_device is created for each dependent
	 * device.  That will cause scan handlers to be attached to device
	 * objects or acpi_drivers to be stopped/started if they are present.
	 */
	list_for_each_entry(dd, &ds->dependent_devices, list)
		dock_create_acpi_device(dd->handle);
}

static void dock_event(struct dock_station *ds, u32 event, int num)
{
	struct device *dev = &ds->dock_device->dev;
	char event_string[13];
	char *envp[] = { event_string, NULL };
	struct dock_dependent_device *dd;

	if (num == UNDOCK_EVENT)
		sprintf(event_string, "EVENT=undock");
	else
		sprintf(event_string, "EVENT=dock");

	/*
	 * Indicate that the status of the dock station has
	 * changed.
	 */
	if (num == DOCK_EVENT)
		kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);

	list_for_each_entry(dd, &ds->dependent_devices, list)
		dock_hotplug_event(dd, event, DOCK_CALL_UEVENT);

	if (num != DOCK_EVENT)
		kobject_uevent_env(&dev->kobj, KOBJ_CHANGE, envp);
}

/**
 * handle_dock - handle a dock event
 * @ds: the dock station
 * @dock: to dock, or undock - that is the question
 *
 * Execute the _DCK method in response to an acpi event
 */
static void handle_dock(struct dock_station *ds, int dock)
{
	acpi_status status;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	unsigned long long value;

	acpi_handle_info(ds->handle, "%s\n", dock ? "docking" : "undocking");

	/* _DCK method has one argument */
	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = dock;
	status = acpi_evaluate_integer(ds->handle, "_DCK", &arg_list, &value);
	if (ACPI_FAILURE(status) && status != AE_NOT_FOUND)
		acpi_handle_err(ds->handle, "Failed to execute _DCK (0x%x)\n",
				status);
}

static inline void dock(struct dock_station *ds)
{
	handle_dock(ds, 1);
}

static inline void undock(struct dock_station *ds)
{
	handle_dock(ds, 0);
}

static inline void begin_dock(struct dock_station *ds)
{
	ds->flags |= DOCK_DOCKING;
}

static inline void complete_dock(struct dock_station *ds)
{
	ds->flags &= ~(DOCK_DOCKING);
	ds->last_dock_time = jiffies;
}

static inline void begin_undock(struct dock_station *ds)
{
	ds->flags |= DOCK_UNDOCKING;
}

static inline void complete_undock(struct dock_station *ds)
{
	ds->flags &= ~(DOCK_UNDOCKING);
}

/**
 * dock_in_progress - see if we are in the middle of handling a dock event
 * @ds: the dock station
 *
 * Sometimes while docking, false dock events can be sent to the driver
 * because good connections aren't made or some other reason.  Ignore these
 * if we are in the middle of doing something.
 */
static int dock_in_progress(struct dock_station *ds)
{
	if ((ds->flags & DOCK_DOCKING) ||
	    time_before(jiffies, (ds->last_dock_time + HZ)))
		return 1;
	return 0;
}

/**
 * register_hotplug_dock_device - register a hotplug function
 * @handle: the handle of the device
 * @ops: handlers to call after docking
 * @context: device specific data
 * @init: Optional initialization routine to run after registration
 * @release: Optional release routine to run on unregistration
 *
 * If a driver would like to perform a hotplug operation after a dock
 * event, they can register an acpi_notifiy_handler to be called by
 * the dock driver after _DCK is executed.
 */
int register_hotplug_dock_device(acpi_handle handle,
				 const struct acpi_dock_ops *ops, void *context,
				 void (*init)(void *), void (*release)(void *))
{
	struct dock_dependent_device *dd;
	struct dock_station *dock_station;
	int ret = -EINVAL;

	if (WARN_ON(!context))
		return -EINVAL;

	if (!dock_station_count)
		return -ENODEV;

	/*
	 * make sure this handle is for a device dependent on the dock,
	 * this would include the dock station itself
	 */
	list_for_each_entry(dock_station, &dock_stations, sibling) {
		/*
		 * An ATA bay can be in a dock and itself can be ejected
		 * separately, so there are two 'dock stations' which need the
		 * ops
		 */
		dd = find_dock_dependent_device(dock_station, handle);
		if (dd && !dock_init_hotplug(dd, ops, context, init, release))
			ret = 0;
	}

	return ret;
}
EXPORT_SYMBOL_GPL(register_hotplug_dock_device);

/**
 * unregister_hotplug_dock_device - remove yourself from the hotplug list
 * @handle: the acpi handle of the device
 */
void unregister_hotplug_dock_device(acpi_handle handle)
{
	struct dock_dependent_device *dd;
	struct dock_station *dock_station;

	if (!dock_station_count)
		return;

	list_for_each_entry(dock_station, &dock_stations, sibling) {
		dd = find_dock_dependent_device(dock_station, handle);
		if (dd)
			dock_release_hotplug(dd);
	}
}
EXPORT_SYMBOL_GPL(unregister_hotplug_dock_device);

/**
 * handle_eject_request - handle an undock request checking for error conditions
 *
 * Check to make sure the dock device is still present, then undock and
 * hotremove all the devices that may need removing.
 */
static int handle_eject_request(struct dock_station *ds, u32 event)
{
	if (dock_in_progress(ds))
		return -EBUSY;

	/*
	 * here we need to generate the undock
	 * event prior to actually doing the undock
	 * so that the device struct still exists.
	 * Also, even send the dock event if the
	 * device is not present anymore
	 */
	dock_event(ds, event, UNDOCK_EVENT);

	hot_remove_dock_devices(ds);
	undock(ds);
	acpi_evaluate_lck(ds->handle, 0);
	acpi_evaluate_ej0(ds->handle);
	if (dock_present(ds)) {
		acpi_handle_err(ds->handle, "Unable to undock!\n");
		return -EBUSY;
	}
	complete_undock(ds);
	return 0;
}

/**
 * dock_notify - act upon an acpi dock notification
 * @ds: dock station
 * @event: the acpi event
 *
 * If we are notified to dock, then check to see if the dock is
 * present and then dock.  Notify all drivers of the dock event,
 * and then hotplug and devices that may need hotplugging.
 */
static void dock_notify(struct dock_station *ds, u32 event)
{
	acpi_handle handle = ds->handle;
	struct acpi_device *ad;
	int surprise_removal = 0;

	/*
	 * According to acpi spec 3.0a, if a DEVICE_CHECK notification
	 * is sent and _DCK is present, it is assumed to mean an undock
	 * request.
	 */
	if ((ds->flags & DOCK_IS_DOCK) && event == ACPI_NOTIFY_DEVICE_CHECK)
		event = ACPI_NOTIFY_EJECT_REQUEST;

	/*
	 * dock station: BUS_CHECK - docked or surprise removal
	 *		 DEVICE_CHECK - undocked
	 * other device: BUS_CHECK/DEVICE_CHECK - added or surprise removal
	 *
	 * To simplify event handling, dock dependent device handler always
	 * get ACPI_NOTIFY_BUS_CHECK/ACPI_NOTIFY_DEVICE_CHECK for add and
	 * ACPI_NOTIFY_EJECT_REQUEST for removal
	 */
	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		if (!dock_in_progress(ds) && acpi_bus_get_device(handle, &ad)) {
			begin_dock(ds);
			dock(ds);
			if (!dock_present(ds)) {
				acpi_handle_err(handle, "Unable to dock!\n");
				complete_dock(ds);
				break;
			}
			hotplug_dock_devices(ds, event);
			complete_dock(ds);
			dock_event(ds, event, DOCK_EVENT);
			acpi_evaluate_lck(ds->handle, 1);
			acpi_update_all_gpes();
			break;
		}
		if (dock_present(ds) || dock_in_progress(ds))
			break;
		/* This is a surprise removal */
		surprise_removal = 1;
		event = ACPI_NOTIFY_EJECT_REQUEST;
		/* Fall back */
	case ACPI_NOTIFY_EJECT_REQUEST:
		begin_undock(ds);
		if ((immediate_undock && !(ds->flags & DOCK_IS_ATA))
		   || surprise_removal)
			handle_eject_request(ds, event);
		else
			dock_event(ds, event, UNDOCK_EVENT);
		break;
	default:
		acpi_handle_err(handle, "Unknown dock event %d\n", event);
	}
}

static void acpi_dock_deferred_cb(void *data, u32 event)
{
	acpi_scan_lock_acquire();
	dock_notify(data, event);
	acpi_scan_lock_release();
}

static void dock_notify_handler(acpi_handle handle, u32 event, void *data)
{
	if (event != ACPI_NOTIFY_BUS_CHECK && event != ACPI_NOTIFY_DEVICE_CHECK
	   && event != ACPI_NOTIFY_EJECT_REQUEST)
		return;

	acpi_hotplug_execute(acpi_dock_deferred_cb, data, event);
}

/**
 * find_dock_devices - find devices on the dock station
 * @handle: the handle of the device we are examining
 * @lvl: unused
 * @context: the dock station private data
 * @rv: unused
 *
 * This function is called by acpi_walk_namespace.  It will
 * check to see if an object has an _EJD method.  If it does, then it
 * will see if it is dependent on the dock station.
 */
static acpi_status __init find_dock_devices(acpi_handle handle, u32 lvl,
					    void *context, void **rv)
{
	struct dock_station *ds = context;
	acpi_handle ejd = NULL;

	acpi_bus_get_ejd(handle, &ejd);
	if (ejd == ds->handle)
		add_dock_dependent_device(ds, handle);

	return AE_OK;
}

/*
 * show_docked - read method for "docked" file in sysfs
 */
static ssize_t show_docked(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct acpi_device *tmp;

	struct dock_station *dock_station = dev->platform_data;

	if (!acpi_bus_get_device(dock_station->handle, &tmp))
		return snprintf(buf, PAGE_SIZE, "1\n");
	return snprintf(buf, PAGE_SIZE, "0\n");
}
static DEVICE_ATTR(docked, S_IRUGO, show_docked, NULL);

/*
 * show_flags - read method for flags file in sysfs
 */
static ssize_t show_flags(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct dock_station *dock_station = dev->platform_data;
	return snprintf(buf, PAGE_SIZE, "%d\n", dock_station->flags);

}
static DEVICE_ATTR(flags, S_IRUGO, show_flags, NULL);

/*
 * write_undock - write method for "undock" file in sysfs
 */
static ssize_t write_undock(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
{
	int ret;
	struct dock_station *dock_station = dev->platform_data;

	if (!count)
		return -EINVAL;

	acpi_scan_lock_acquire();
	begin_undock(dock_station);
	ret = handle_eject_request(dock_station, ACPI_NOTIFY_EJECT_REQUEST);
	acpi_scan_lock_release();
	return ret ? ret: count;
}
static DEVICE_ATTR(undock, S_IWUSR, NULL, write_undock);

/*
 * show_dock_uid - read method for "uid" file in sysfs
 */
static ssize_t show_dock_uid(struct device *dev,
			     struct device_attribute *attr, char *buf)
{
	unsigned long long lbuf;
	struct dock_station *dock_station = dev->platform_data;
	acpi_status status = acpi_evaluate_integer(dock_station->handle,
					"_UID", NULL, &lbuf);
	if (ACPI_FAILURE(status))
	    return 0;

	return snprintf(buf, PAGE_SIZE, "%llx\n", lbuf);
}
static DEVICE_ATTR(uid, S_IRUGO, show_dock_uid, NULL);

static ssize_t show_dock_type(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	struct dock_station *dock_station = dev->platform_data;
	char *type;

	if (dock_station->flags & DOCK_IS_DOCK)
		type = "dock_station";
	else if (dock_station->flags & DOCK_IS_ATA)
		type = "ata_bay";
	else if (dock_station->flags & DOCK_IS_BAT)
		type = "battery_bay";
	else
		type = "unknown";

	return snprintf(buf, PAGE_SIZE, "%s\n", type);
}
static DEVICE_ATTR(type, S_IRUGO, show_dock_type, NULL);

static struct attribute *dock_attributes[] = {
	&dev_attr_docked.attr,
	&dev_attr_flags.attr,
	&dev_attr_undock.attr,
	&dev_attr_uid.attr,
	&dev_attr_type.attr,
	NULL
};

static struct attribute_group dock_attribute_group = {
	.attrs = dock_attributes
};

/**
 * dock_add - add a new dock station
 * @handle: the dock station handle
 *
 * allocated and initialize a new dock station device.  Find all devices
 * that are on the dock station, and register for dock event notifications.
 */
static int __init dock_add(acpi_handle handle)
{
	struct dock_station *dock_station, ds = { NULL, };
	struct platform_device *dd;
	acpi_status status;
	int ret;

	dd = platform_device_register_data(NULL, "dock", dock_station_count,
					   &ds, sizeof(ds));
	if (IS_ERR(dd))
		return PTR_ERR(dd);

	dock_station = dd->dev.platform_data;

	dock_station->handle = handle;
	dock_station->dock_device = dd;
	dock_station->last_dock_time = jiffies - HZ;

	INIT_LIST_HEAD(&dock_station->sibling);
	INIT_LIST_HEAD(&dock_station->dependent_devices);

	/* we want the dock device to send uevents */
	dev_set_uevent_suppress(&dd->dev, 0);

	if (acpi_dock_match(handle))
		dock_station->flags |= DOCK_IS_DOCK;
	if (acpi_ata_match(handle))
		dock_station->flags |= DOCK_IS_ATA;
	if (is_battery(handle))
		dock_station->flags |= DOCK_IS_BAT;

	ret = sysfs_create_group(&dd->dev.kobj, &dock_attribute_group);
	if (ret)
		goto err_unregister;

	/* Find dependent devices */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX, find_dock_devices, NULL,
			    dock_station, NULL);

	/* add the dock station as a device dependent on itself */
	ret = add_dock_dependent_device(dock_station, handle);
	if (ret)
		goto err_rmgroup;

	status = acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
					     dock_notify_handler, dock_station);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto err_rmgroup;
	}

	dock_station_count++;
	list_add(&dock_station->sibling, &dock_stations);
	return 0;

err_rmgroup:
	remove_dock_dependent_devices(dock_station);
	sysfs_remove_group(&dd->dev.kobj, &dock_attribute_group);
err_unregister:
	platform_device_unregister(dd);
	acpi_handle_err(handle, "%s encountered error %d\n", __func__, ret);
	return ret;
}

/**
 * find_dock_and_bay - look for dock stations and bays
 * @handle: acpi handle of a device
 * @lvl: unused
 * @context: unused
 * @rv: unused
 *
 * This is called by acpi_walk_namespace to look for dock stations and bays.
 */
static acpi_status __init
find_dock_and_bay(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	if (acpi_dock_match(handle) || is_ejectable_bay(handle))
		dock_add(handle);

	return AE_OK;
}

void __init acpi_dock_init(void)
{
	if (acpi_disabled)
		return;

	/* look for dock stations and bays */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
		ACPI_UINT32_MAX, find_dock_and_bay, NULL, NULL, NULL);

	if (!dock_station_count) {
		pr_info(PREFIX "No dock devices found.\n");
		return;
	}

	pr_info(PREFIX "%s: %d docks/bays found\n",
		ACPI_DOCK_DRIVER_DESCRIPTION, dock_station_count);
}
