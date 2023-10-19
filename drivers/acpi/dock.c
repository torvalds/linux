// SPDX-License-Identifier: GPL-2.0-or-later
/*
 *  dock.c - ACPI dock station driver
 *
 *  Copyright (C) 2006, 2014, Intel Corp.
 *  Author: Kristen Carlson Accardi <kristen.c.accardi@intel.com>
 *          Rafael J. Wysocki <rafael.j.wysocki@intel.com>
 */

#include <linux/kernel.h>
#include <linux/moduleparam.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/platform_device.h>
#include <linux/jiffies.h>
#include <linux/stddef.h>
#include <linux/acpi.h>

#include "internal.h"

static bool immediate_undock = 1;
module_param(immediate_undock, bool, 0644);
MODULE_PARM_DESC(immediate_undock, "1 (default) will cause the driver to "
	"undock immediately when the undock button is pressed, 0 will cause"
	" the driver to wait for userspace to write the undock sysfs file "
	" before undocking");

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

struct dock_dependent_device {
	struct list_head list;
	struct acpi_device *adev;
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
 * @ds: Dock station.
 * @adev: Dependent ACPI device object.
 *
 * Add the dependent device to the dock's dependent device list.
 */
static int add_dock_dependent_device(struct dock_station *ds,
				     struct acpi_device *adev)
{
	struct dock_dependent_device *dd;

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (!dd)
		return -ENOMEM;

	dd->adev = adev;
	INIT_LIST_HEAD(&dd->list);
	list_add_tail(&dd->list, &ds->dependent_devices);

	return 0;
}

static void dock_hotplug_event(struct dock_dependent_device *dd, u32 event,
			       enum dock_callback_type cb_type)
{
	struct acpi_device *adev = dd->adev;

	acpi_lock_hp_context();

	if (!adev->hp)
		goto out;

	if (cb_type == DOCK_CALL_FIXUP) {
		void (*fixup)(struct acpi_device *);

		fixup = adev->hp->fixup;
		if (fixup) {
			acpi_unlock_hp_context();
			fixup(adev);
			return;
		}
	} else if (cb_type == DOCK_CALL_UEVENT) {
		void (*uevent)(struct acpi_device *, u32);

		uevent = adev->hp->uevent;
		if (uevent) {
			acpi_unlock_hp_context();
			uevent(adev, event);
			return;
		}
	} else {
		int (*notify)(struct acpi_device *, u32);

		notify = adev->hp->notify;
		if (notify) {
			acpi_unlock_hp_context();
			notify(adev, event);
			return;
		}
	}

 out:
	acpi_unlock_hp_context();
}

static struct dock_station *find_dock_station(acpi_handle handle)
{
	struct dock_station *ds;

	list_for_each_entry(ds, &dock_stations, sibling)
		if (ds->handle == handle)
			return ds;

	return NULL;
}

/**
 * find_dock_dependent_device - get a device dependent on this dock
 * @ds: the dock station
 * @adev: ACPI device object to find.
 *
 * iterate over the dependent device list for this dock.  If the
 * dependent device matches the handle, return.
 */
static struct dock_dependent_device *
find_dock_dependent_device(struct dock_station *ds, struct acpi_device *adev)
{
	struct dock_dependent_device *dd;

	list_for_each_entry(dd, &ds->dependent_devices, list)
		if (adev == dd->adev)
			return dd;

	return NULL;
}

void register_dock_dependent_device(struct acpi_device *adev,
				    acpi_handle dshandle)
{
	struct dock_station *ds = find_dock_station(dshandle);

	if (ds && !find_dock_dependent_device(ds, adev))
		add_dock_dependent_device(ds, adev);
}

/*****************************************************************************
 *                         Dock functions                                    *
 *****************************************************************************/

/**
 * is_dock_device - see if a device is on a dock station
 * @adev: ACPI device object to check.
 *
 * If this device is either the dock station itself,
 * or is a device dependent on the dock station, then it
 * is a dock device
 */
int is_dock_device(struct acpi_device *adev)
{
	struct dock_station *dock_station;

	if (!dock_station_count)
		return 0;

	if (acpi_dock_match(adev->handle))
		return 1;

	list_for_each_entry(dock_station, &dock_stations, sibling)
		if (find_dock_dependent_device(dock_station, adev))
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
		dock_hotplug_event(dd, ACPI_NOTIFY_EJECT_REQUEST,
				   DOCK_CALL_HANDLER);

	list_for_each_entry_reverse(dd, &ds->dependent_devices, list)
		acpi_bus_trim(dd->adev);
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
	 * Check if all devices have been enumerated already.  If not, run
	 * acpi_bus_scan() for them and that will cause scan handlers to be
	 * attached to device objects or acpi_drivers to be stopped/started if
	 * they are present.
	 */
	list_for_each_entry(dd, &ds->dependent_devices, list) {
		struct acpi_device *adev = dd->adev;

		if (!acpi_device_enumerated(adev)) {
			int ret = acpi_bus_scan(adev->handle);

			if (ret)
				dev_dbg(&adev->dev, "scan error %d\n", -ret);
		}
	}
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
 * dock_notify - Handle ACPI dock notification.
 * @adev: Dock station's ACPI device object.
 * @event: Event code.
 *
 * If we are notified to dock, then check to see if the dock is
 * present and then dock.  Notify all drivers of the dock event,
 * and then hotplug and devices that may need hotplugging.
 */
int dock_notify(struct acpi_device *adev, u32 event)
{
	acpi_handle handle = adev->handle;
	struct dock_station *ds = find_dock_station(handle);
	int surprise_removal = 0;

	if (!ds)
		return -ENODEV;

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
		if (!dock_in_progress(ds) && !acpi_device_enumerated(adev)) {
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
		fallthrough;
	case ACPI_NOTIFY_EJECT_REQUEST:
		begin_undock(ds);
		if ((immediate_undock && !(ds->flags & DOCK_IS_ATA))
		   || surprise_removal)
			handle_eject_request(ds, event);
		else
			dock_event(ds, event, UNDOCK_EVENT);
		break;
	}
	return 0;
}

/*
 * show_docked - read method for "docked" file in sysfs
 */
static ssize_t docked_show(struct device *dev,
			   struct device_attribute *attr, char *buf)
{
	struct dock_station *dock_station = dev->platform_data;
	struct acpi_device *adev = acpi_fetch_acpi_dev(dock_station->handle);

	return sysfs_emit(buf, "%u\n", acpi_device_enumerated(adev));
}
static DEVICE_ATTR_RO(docked);

/*
 * show_flags - read method for flags file in sysfs
 */
static ssize_t flags_show(struct device *dev,
			  struct device_attribute *attr, char *buf)
{
	struct dock_station *dock_station = dev->platform_data;

	return sysfs_emit(buf, "%d\n", dock_station->flags);

}
static DEVICE_ATTR_RO(flags);

/*
 * write_undock - write method for "undock" file in sysfs
 */
static ssize_t undock_store(struct device *dev, struct device_attribute *attr,
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
	return ret ? ret : count;
}
static DEVICE_ATTR_WO(undock);

/*
 * show_dock_uid - read method for "uid" file in sysfs
 */
static ssize_t uid_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	unsigned long long lbuf;
	struct dock_station *dock_station = dev->platform_data;

	acpi_status status = acpi_evaluate_integer(dock_station->handle,
					"_UID", NULL, &lbuf);
	if (ACPI_FAILURE(status))
		return 0;

	return sysfs_emit(buf, "%llx\n", lbuf);
}
static DEVICE_ATTR_RO(uid);

static ssize_t type_show(struct device *dev,
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

	return sysfs_emit(buf, "%s\n", type);
}
static DEVICE_ATTR_RO(type);

static struct attribute *dock_attributes[] = {
	&dev_attr_docked.attr,
	&dev_attr_flags.attr,
	&dev_attr_undock.attr,
	&dev_attr_uid.attr,
	&dev_attr_type.attr,
	NULL
};

static const struct attribute_group dock_attribute_group = {
	.attrs = dock_attributes
};

/**
 * acpi_dock_add - Add a new dock station
 * @adev: Dock station ACPI device object.
 *
 * allocated and initialize a new dock station device.
 */
void acpi_dock_add(struct acpi_device *adev)
{
	struct dock_station *dock_station, ds = { NULL, };
	struct platform_device_info pdevinfo;
	acpi_handle handle = adev->handle;
	struct platform_device *dd;
	int ret;

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.name = "dock";
	pdevinfo.id = dock_station_count;
	pdevinfo.fwnode = acpi_fwnode_handle(adev);
	pdevinfo.data = &ds;
	pdevinfo.size_data = sizeof(ds);
	dd = platform_device_register_full(&pdevinfo);
	if (IS_ERR(dd))
		return;

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
	if (acpi_device_is_battery(adev))
		dock_station->flags |= DOCK_IS_BAT;

	ret = sysfs_create_group(&dd->dev.kobj, &dock_attribute_group);
	if (ret)
		goto err_unregister;

	/* add the dock station as a device dependent on itself */
	ret = add_dock_dependent_device(dock_station, adev);
	if (ret)
		goto err_rmgroup;

	dock_station_count++;
	list_add(&dock_station->sibling, &dock_stations);
	adev->flags.is_dock_station = true;
	dev_info(&adev->dev, "ACPI dock station (docks/bays count: %d)\n",
		 dock_station_count);
	return;

err_rmgroup:
	sysfs_remove_group(&dd->dev.kobj, &dock_attribute_group);

err_unregister:
	platform_device_unregister(dd);
	acpi_handle_err(handle, "%s encountered error %d\n", __func__, ret);
}
