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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/notifier.h>
#include <linux/jiffies.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define ACPI_DOCK_DRIVER_NAME "ACPI Dock Station Driver"

ACPI_MODULE_NAME("dock")
MODULE_AUTHOR("Kristen Carlson Accardi");
MODULE_DESCRIPTION(ACPI_DOCK_DRIVER_NAME);
MODULE_LICENSE("GPL");

static struct atomic_notifier_head dock_notifier_list;

struct dock_station {
	acpi_handle handle;
	unsigned long last_dock_time;
	u32 flags;
	spinlock_t dd_lock;
	spinlock_t hp_lock;
	struct list_head dependent_devices;
	struct list_head hotplug_devices;
};

struct dock_dependent_device {
	struct list_head list;
	struct list_head hotplug_list;
	acpi_handle handle;
	acpi_notify_handler handler;
	void *context;
};

#define DOCK_DOCKING	0x00000001
#define DOCK_EVENT	3
#define UNDOCK_EVENT	2

static struct dock_station *dock_station;

/*****************************************************************************
 *                         Dock Dependent device functions                   *
 *****************************************************************************/
/**
 *  alloc_dock_dependent_device - allocate and init a dependent device
 *  @handle: the acpi_handle of the dependent device
 *
 *  Allocate memory for a dependent device structure for a device referenced
 *  by the acpi handle
 */
static struct dock_dependent_device *
alloc_dock_dependent_device(acpi_handle handle)
{
	struct dock_dependent_device *dd;

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (dd) {
		dd->handle = handle;
		INIT_LIST_HEAD(&dd->list);
		INIT_LIST_HEAD(&dd->hotplug_list);
	}
	return dd;
}

/**
 * add_dock_dependent_device - associate a device with the dock station
 * @ds: The dock station
 * @dd: The dependent device
 *
 * Add the dependent device to the dock's dependent device list.
 */
static void
add_dock_dependent_device(struct dock_station *ds,
			  struct dock_dependent_device *dd)
{
	spin_lock(&ds->dd_lock);
	list_add_tail(&dd->list, &ds->dependent_devices);
	spin_unlock(&ds->dd_lock);
}

/**
 * dock_add_hotplug_device - associate a hotplug handler with the dock station
 * @ds: The dock station
 * @dd: The dependent device struct
 *
 * Add the dependent device to the dock's hotplug device list
 */
static void
dock_add_hotplug_device(struct dock_station *ds,
			struct dock_dependent_device *dd)
{
	spin_lock(&ds->hp_lock);
	list_add_tail(&dd->hotplug_list, &ds->hotplug_devices);
	spin_unlock(&ds->hp_lock);
}

/**
 * dock_del_hotplug_device - remove a hotplug handler from the dock station
 * @ds: The dock station
 * @dd: the dependent device struct
 *
 * Delete the dependent device from the dock's hotplug device list
 */
static void
dock_del_hotplug_device(struct dock_station *ds,
			struct dock_dependent_device *dd)
{
	spin_lock(&ds->hp_lock);
	list_del(&dd->hotplug_list);
	spin_unlock(&ds->hp_lock);
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

	spin_lock(&ds->dd_lock);
	list_for_each_entry(dd, &ds->dependent_devices, list) {
		if (handle == dd->handle) {
			spin_unlock(&ds->dd_lock);
			return dd;
		}
	}
	spin_unlock(&ds->dd_lock);
	return NULL;
}

/*****************************************************************************
 *                         Dock functions                                    *
 *****************************************************************************/
/**
 * is_dock - see if a device is a dock station
 * @handle: acpi handle of the device
 *
 * If an acpi object has a _DCK method, then it is by definition a dock
 * station, so return true.
 */
static int is_dock(acpi_handle handle)
{
	acpi_status status;
	acpi_handle tmp;

	status = acpi_get_handle(handle, "_DCK", &tmp);
	if (ACPI_FAILURE(status))
		return 0;
	return 1;
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
	if (!dock_station)
		return 0;

	if (is_dock(handle) || find_dock_dependent_device(dock_station, handle))
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
	unsigned long sta;
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
 *
 *  Returns a pointer to the acpi_device corresponding to the handle.
 */
static struct acpi_device * dock_create_acpi_device(acpi_handle handle)
{
	struct acpi_device *device = NULL;
	struct acpi_device *parent_device;
	acpi_handle parent;
	int ret;

	if (acpi_bus_get_device(handle, &device)) {
		/*
		 * no device created for this object,
		 * so we should create one.
		 */
		acpi_get_parent(handle, &parent);
		if (acpi_bus_get_device(parent, &parent_device))
			parent_device = NULL;

		ret = acpi_bus_add(&device, parent_device, handle,
			ACPI_BUS_TYPE_DEVICE);
		if (ret) {
			pr_debug("error adding bus, %x\n",
				-ret);
			return NULL;
		}
	}
	return device;
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
	int ret;

	if (!acpi_bus_get_device(handle, &device)) {
		ret = acpi_bus_trim(device, 1);
		if (ret)
			pr_debug("error removing bus, %x\n", -ret);
	}
}


/**
 * hotplug_dock_devices - insert or remove devices on the dock station
 * @ds: the dock station
 * @event: either bus check or eject request
 *
 * Some devices on the dock station need to have drivers called
 * to perform hotplug operations after a dock event has occurred.
 * Traverse the list of dock devices that have registered a
 * hotplug handler, and call the handler.
 */
static void hotplug_dock_devices(struct dock_station *ds, u32 event)
{
	struct dock_dependent_device *dd;

	spin_lock(&ds->hp_lock);

	/*
	 * First call driver specific hotplug functions
	 */
	list_for_each_entry(dd, &ds->hotplug_devices, hotplug_list) {
		if (dd->handler)
			dd->handler(dd->handle, event, dd->context);
	}

	/*
	 * Now make sure that an acpi_device is created for each
	 * dependent device, or removed if this is an eject request.
	 * This will cause acpi_drivers to be stopped/started if they
	 * exist
	 */
	list_for_each_entry(dd, &ds->dependent_devices, list) {
		if (event == ACPI_NOTIFY_EJECT_REQUEST)
			dock_remove_acpi_device(dd->handle);
		else
			dock_create_acpi_device(dd->handle);
	}
	spin_unlock(&ds->hp_lock);
}

static void dock_event(struct dock_station *ds, u32 event, int num)
{
	/*
	 * we don't do events until someone tells me that
	 * they would like to have them.
	 */
}

/**
 * eject_dock - respond to a dock eject request
 * @ds: the dock station
 *
 * This is called after _DCK is called, to execute the dock station's
 * _EJ0 method.
 */
static void eject_dock(struct dock_station *ds)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;
	acpi_status status;
	acpi_handle tmp;

	/* all dock devices should have _EJ0, but check anyway */
	status = acpi_get_handle(ds->handle, "_EJ0", &tmp);
	if (ACPI_FAILURE(status)) {
		pr_debug("No _EJ0 support for dock device\n");
		return;
	}

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = 1;

	if (ACPI_FAILURE(acpi_evaluate_object(ds->handle, "_EJ0",
					      &arg_list, NULL)))
		pr_debug("Failed to evaluate _EJ0!\n");
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
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_buffer name_buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;

	acpi_get_name(ds->handle, ACPI_FULL_PATHNAME, &name_buffer);
	obj = name_buffer.pointer;

	printk(KERN_INFO PREFIX "%s\n", dock ? "docking" : "undocking");

	/* _DCK method has one argument */
	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = dock;
	status = acpi_evaluate_object(ds->handle, "_DCK", &arg_list, &buffer);
	if (ACPI_FAILURE(status))
		pr_debug("%s: failed to execute _DCK\n", obj->string.pointer);
	kfree(buffer.pointer);
	kfree(name_buffer.pointer);
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
 * register_dock_notifier - add yourself to the dock notifier list
 * @nb: the callers notifier block
 *
 * If a driver wishes to be notified about dock events, they can
 * use this function to put a notifier block on the dock notifier list.
 * this notifier call chain will be called after a dock event, but
 * before hotplugging any new devices.
 */
int register_dock_notifier(struct notifier_block *nb)
{
	return atomic_notifier_chain_register(&dock_notifier_list, nb);
}

EXPORT_SYMBOL_GPL(register_dock_notifier);

/**
 * unregister_dock_notifier - remove yourself from the dock notifier list
 * @nb: the callers notifier block
 */
void unregister_dock_notifier(struct notifier_block *nb)
{
	atomic_notifier_chain_unregister(&dock_notifier_list, nb);
}

EXPORT_SYMBOL_GPL(unregister_dock_notifier);

/**
 * register_hotplug_dock_device - register a hotplug function
 * @handle: the handle of the device
 * @handler: the acpi_notifier_handler to call after docking
 * @context: device specific data
 *
 * If a driver would like to perform a hotplug operation after a dock
 * event, they can register an acpi_notifiy_handler to be called by
 * the dock driver after _DCK is executed.
 */
int
register_hotplug_dock_device(acpi_handle handle, acpi_notify_handler handler,
			     void *context)
{
	struct dock_dependent_device *dd;

	if (!dock_station)
		return -ENODEV;

	/*
	 * make sure this handle is for a device dependent on the dock,
	 * this would include the dock station itself
	 */
	dd = find_dock_dependent_device(dock_station, handle);
	if (dd) {
		dd->handler = handler;
		dd->context = context;
		dock_add_hotplug_device(dock_station, dd);
		return 0;
	}

	return -EINVAL;
}

EXPORT_SYMBOL_GPL(register_hotplug_dock_device);

/**
 * unregister_hotplug_dock_device - remove yourself from the hotplug list
 * @handle: the acpi handle of the device
 */
void unregister_hotplug_dock_device(acpi_handle handle)
{
	struct dock_dependent_device *dd;

	if (!dock_station)
		return;

	dd = find_dock_dependent_device(dock_station, handle);
	if (dd)
		dock_del_hotplug_device(dock_station, dd);
}

EXPORT_SYMBOL_GPL(unregister_hotplug_dock_device);

/**
 * dock_notify - act upon an acpi dock notification
 * @handle: the dock station handle
 * @event: the acpi event
 * @data: our driver data struct
 *
 * If we are notified to dock, then check to see if the dock is
 * present and then dock.  Notify all drivers of the dock event,
 * and then hotplug and devices that may need hotplugging.  For undock
 * check to make sure the dock device is still present, then undock
 * and hotremove all the devices that may need removing.
 */
static void dock_notify(acpi_handle handle, u32 event, void *data)
{
	struct dock_station *ds = (struct dock_station *)data;

	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
		if (!dock_in_progress(ds) && dock_present(ds)) {
			begin_dock(ds);
			dock(ds);
			if (!dock_present(ds)) {
				printk(KERN_ERR PREFIX "Unable to dock!\n");
				break;
			}
			atomic_notifier_call_chain(&dock_notifier_list,
						   event, NULL);
			hotplug_dock_devices(ds, event);
			complete_dock(ds);
			dock_event(ds, event, DOCK_EVENT);
		}
		break;
	case ACPI_NOTIFY_DEVICE_CHECK:
	/*
         * According to acpi spec 3.0a, if a DEVICE_CHECK notification
         * is sent and _DCK is present, it is assumed to mean an
         * undock request.  This notify routine will only be called
         * for objects defining _DCK, so we will fall through to eject
         * request here.  However, we will pass an eject request through
	 * to the driver who wish to hotplug.
         */
	case ACPI_NOTIFY_EJECT_REQUEST:
		if (!dock_in_progress(ds) && dock_present(ds)) {
			/*
			 * here we need to generate the undock
			 * event prior to actually doing the undock
			 * so that the device struct still exists.
			 */
			dock_event(ds, event, UNDOCK_EVENT);
			hotplug_dock_devices(ds, ACPI_NOTIFY_EJECT_REQUEST);
			undock(ds);
			eject_dock(ds);
			if (dock_present(ds))
				printk(KERN_ERR PREFIX "Unable to undock!\n");
		}
		break;
	default:
		printk(KERN_ERR PREFIX "Unknown dock event %d\n", event);
	}
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
static acpi_status
find_dock_devices(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	acpi_handle tmp;
	struct dock_station *ds = (struct dock_station *)context;
	struct dock_dependent_device *dd;

	status = acpi_bus_get_ejd(handle, &tmp);
	if (ACPI_FAILURE(status))
		return AE_OK;

	if (tmp == ds->handle) {
		dd = alloc_dock_dependent_device(handle);
		if (dd)
			add_dock_dependent_device(ds, dd);
	}

	return AE_OK;
}

/**
 * dock_add - add a new dock station
 * @handle: the dock station handle
 *
 * allocated and initialize a new dock station device.  Find all devices
 * that are on the dock station, and register for dock event notifications.
 */
static int dock_add(acpi_handle handle)
{
	int ret;
	acpi_status status;
	struct dock_dependent_device *dd;

	/* allocate & initialize the dock_station private data */
	dock_station = kzalloc(sizeof(*dock_station), GFP_KERNEL);
	if (!dock_station)
		return -ENOMEM;
	dock_station->handle = handle;
	dock_station->last_dock_time = jiffies - HZ;
	INIT_LIST_HEAD(&dock_station->dependent_devices);
	INIT_LIST_HEAD(&dock_station->hotplug_devices);
	spin_lock_init(&dock_station->dd_lock);
	spin_lock_init(&dock_station->hp_lock);
	ATOMIC_INIT_NOTIFIER_HEAD(&dock_notifier_list);

	/* Find dependent devices */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX, find_dock_devices, dock_station,
			    NULL);

	/* add the dock station as a device dependent on itself */
	dd = alloc_dock_dependent_device(handle);
	if (!dd) {
		kfree(dock_station);
		return -ENOMEM;
	}
	add_dock_dependent_device(dock_station, dd);

	/* register for dock events */
	status = acpi_install_notify_handler(dock_station->handle,
					     ACPI_SYSTEM_NOTIFY,
					     dock_notify, dock_station);

	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "Error installing notify handler\n");
		ret = -ENODEV;
		goto dock_add_err;
	}

	printk(KERN_INFO PREFIX "%s \n", ACPI_DOCK_DRIVER_NAME);

	return 0;

dock_add_err:
	kfree(dock_station);
	kfree(dd);
	return ret;
}

/**
 * dock_remove - free up resources related to the dock station
 */
static int dock_remove(void)
{
	struct dock_dependent_device *dd, *tmp;
	acpi_status status;

	if (!dock_station)
		return 0;

	/* remove dependent devices */
	list_for_each_entry_safe(dd, tmp, &dock_station->dependent_devices,
				 list)
	    kfree(dd);

	/* remove dock notify handler */
	status = acpi_remove_notify_handler(dock_station->handle,
					    ACPI_SYSTEM_NOTIFY,
					    dock_notify);
	if (ACPI_FAILURE(status))
		printk(KERN_ERR "Error removing notify handler\n");

	/* free dock station memory */
	kfree(dock_station);
	return 0;
}

/**
 * find_dock - look for a dock station
 * @handle: acpi handle of a device
 * @lvl: unused
 * @context: counter of dock stations found
 * @rv: unused
 *
 * This is called by acpi_walk_namespace to look for dock stations.
 */
static acpi_status
find_dock(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;
	acpi_status status = AE_OK;

	if (is_dock(handle)) {
		if (dock_add(handle) >= 0) {
			(*count)++;
			status = AE_CTRL_TERMINATE;
		}
	}
	return status;
}

static int __init dock_init(void)
{
	int num = 0;

	dock_station = NULL;

	/* look for a dock station */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX, find_dock, &num, NULL);

	if (!num)
		return -ENODEV;

	return 0;
}

static void __exit dock_exit(void)
{
	dock_remove();
}

postcore_initcall(dock_init);
module_exit(dock_exit);
