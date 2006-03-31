/*
 * ACPI PCI HotPlug dock functions to ACPI CA subsystem
 *
 * Copyright (C) 2006 Kristen Carlson Accardi (kristen.c.accardi@intel.com)
 * Copyright (C) 2006 Intel Corporation
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <kristen.c.accardi@intel.com>
 *
 */
#include <linux/init.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/smp_lock.h>
#include <linux/mutex.h>

#include "../pci.h"
#include "pci_hotplug.h"
#include "acpiphp.h"

static struct acpiphp_dock_station *ds;
#define MY_NAME "acpiphp_dock"


int is_dependent_device(acpi_handle handle)
{
	return (get_dependent_device(handle) ? 1 : 0);
}


static acpi_status
find_dependent_device(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;

	if (is_dependent_device(handle)) {
		(*count)++;
		return AE_CTRL_TERMINATE;
	} else {
		return AE_OK;
	}
}




void add_dependent_device(struct dependent_device *new_dd)
{
	list_add_tail(&new_dd->device_list, &ds->dependent_devices);
}


void add_pci_dependent_device(struct dependent_device *new_dd)
{
	list_add_tail(&new_dd->pci_list, &ds->pci_dependent_devices);
}



struct dependent_device * get_dependent_device(acpi_handle handle)
{
	struct dependent_device *dd;

	if (!ds)
		return NULL;

	list_for_each_entry(dd, &ds->dependent_devices, device_list) {
		if (handle == dd->handle)
			return dd;
	}
	return NULL;
}



struct dependent_device *alloc_dependent_device(acpi_handle handle)
{
	struct dependent_device *dd;

	dd = kzalloc(sizeof(*dd), GFP_KERNEL);
	if (dd) {
		INIT_LIST_HEAD(&dd->pci_list);
		INIT_LIST_HEAD(&dd->device_list);
		dd->handle = handle;
	}
	return dd;
}



static int is_dock(acpi_handle handle)
{
	acpi_status status;
	acpi_handle tmp;

	status = acpi_get_handle(handle, "_DCK", &tmp);
	if (ACPI_FAILURE(status)) {
		return 0;
	}
	return 1;
}



static int dock_present(void)
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



static void eject_dock(void)
{
	struct acpi_object_list arg_list;
	union acpi_object arg;

	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = 1;

	if (ACPI_FAILURE(acpi_evaluate_object(ds->handle, "_EJ0",
					&arg_list, NULL)) || dock_present())
		warn("%s: failed to eject dock!\n", __FUNCTION__);

	return;
}




static acpi_status handle_dock(int dock)
{
	acpi_status status;
	struct acpi_object_list arg_list;
	union acpi_object arg;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};

	dbg("%s: %s\n", __FUNCTION__, dock ? "docking" : "undocking");

	/* _DCK method has one argument */
	arg_list.count = 1;
	arg_list.pointer = &arg;
	arg.type = ACPI_TYPE_INTEGER;
	arg.integer.value = dock;
	status = acpi_evaluate_object(ds->handle, "_DCK",
					&arg_list, &buffer);
	if (ACPI_FAILURE(status))
		err("%s: failed to execute _DCK\n", __FUNCTION__);
	acpi_os_free(buffer.pointer);

	return status;
}



static inline void dock(void)
{
	handle_dock(1);
}



static inline void undock(void)
{
	handle_dock(0);
}



/*
 * the _DCK method can do funny things... and sometimes not
 * hah-hah funny.
 *
 * TBD - figure out a way to only call fixups for
 * systems that require them.
 */
static void post_dock_fixups(void)
{
	struct pci_bus *bus;
	u32 buses;
	struct dependent_device *dd;

	list_for_each_entry(dd, &ds->pci_dependent_devices, pci_list) {
		bus = dd->func->slot->bridge->pci_bus;

		/* fixup bad _DCK function that rewrites
	 	 * secondary bridge on slot
	 	 */
		pci_read_config_dword(bus->self,
				PCI_PRIMARY_BUS,
				&buses);

		if (((buses >> 8) & 0xff) != bus->secondary) {
			buses = (buses & 0xff000000)
	     			| ((unsigned int)(bus->primary)     <<  0)
	     			| ((unsigned int)(bus->secondary)   <<  8)
	     			| ((unsigned int)(bus->subordinate) << 16);
			pci_write_config_dword(bus->self,
					PCI_PRIMARY_BUS,
					buses);
		}
	}
}



static void hotplug_pci(u32 type)
{
	struct dependent_device *dd;

	list_for_each_entry(dd, &ds->pci_dependent_devices, pci_list)
		handle_hotplug_event_func(dd->handle, type, dd->func);
}



static inline void begin_dock(void)
{
	ds->flags |= DOCK_DOCKING;
}


static inline void complete_dock(void)
{
	ds->flags &= ~(DOCK_DOCKING);
	ds->last_dock_time = jiffies;
}


static int dock_in_progress(void)
{
	if (ds->flags & DOCK_DOCKING ||
		ds->last_dock_time == jiffies) {
		dbg("dock in progress\n");
		return 1;
	}
	return 0;
}



static void
handle_hotplug_event_dock(acpi_handle handle, u32 type, void *context)
{
	dbg("%s: enter\n", __FUNCTION__);

	switch (type) {
		case ACPI_NOTIFY_BUS_CHECK:
			dbg("BUS Check\n");
			if (!dock_in_progress() && dock_present()) {
				begin_dock();
				dock();
				if (!dock_present()) {
					err("Unable to dock!\n");
					break;
				}
				post_dock_fixups();
				hotplug_pci(type);
				complete_dock();
			}
			break;
		case ACPI_NOTIFY_EJECT_REQUEST:
			dbg("EJECT request\n");
			if (!dock_in_progress() && dock_present()) {
				hotplug_pci(type);
				undock();
				eject_dock();
				if (dock_present())
					err("Unable to undock!\n");
			}
			break;
	}
}




static acpi_status
find_dock_ejd(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	acpi_status status;
	acpi_handle tmp;
	acpi_handle dck_handle = (acpi_handle) context;
	char objname[64];
	struct acpi_buffer buffer = { .length = sizeof(objname),
				      .pointer = objname };
	struct acpi_buffer ejd_buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *ejd_obj;

	status = acpi_get_handle(handle, "_EJD", &tmp);
	if (ACPI_FAILURE(status))
		return AE_OK;

	/* make sure we are dependent on the dock device,
	 * by executing the _EJD method, then getting a handle
	 * to the device referenced by that name.  If that
	 * device handle is the same handle as the dock station
	 * handle, then we are a device dependent on the dock station
	 */
	acpi_get_name(dck_handle, ACPI_FULL_PATHNAME, &buffer);
	status = acpi_evaluate_object(handle, "_EJD", NULL, &ejd_buffer);
	if (ACPI_FAILURE(status)) {
		err("Unable to execute _EJD!\n");
		goto find_ejd_out;
	}
	ejd_obj = ejd_buffer.pointer;
	status = acpi_get_handle(NULL, ejd_obj->string.pointer, &tmp);
	if (ACPI_FAILURE(status))
		goto find_ejd_out;

	if (tmp == dck_handle) {
		struct dependent_device *dd;
		dbg("%s: found device dependent on dock\n", __FUNCTION__);
		dd = alloc_dependent_device(handle);
		if (!dd) {
			err("Can't allocate memory for dependent device!\n");
			goto find_ejd_out;
		}
		add_dependent_device(dd);
	}

find_ejd_out:
	acpi_os_free(ejd_buffer.pointer);
	return AE_OK;
}



int detect_dependent_devices(acpi_handle *bridge_handle)
{
	acpi_status status;
	int count;

	count = 0;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, bridge_handle,
					(u32)1, find_dependent_device,
					(void *)&count, NULL);

	return count;
}





static acpi_status
find_dock(acpi_handle handle, u32 lvl, void *context, void **rv)
{
	int *count = (int *)context;

	if (is_dock(handle)) {
		dbg("%s: found dock\n", __FUNCTION__);
		ds = kzalloc(sizeof(*ds), GFP_KERNEL);
		ds->handle = handle;
		INIT_LIST_HEAD(&ds->dependent_devices);
		INIT_LIST_HEAD(&ds->pci_dependent_devices);

		/* look for devices dependent on dock station */
		acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			ACPI_UINT32_MAX, find_dock_ejd, handle, NULL);

		acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
			handle_hotplug_event_dock, ds);
		(*count)++;
	}

	return AE_OK;
}




int find_dock_station(void)
{
	int num = 0;

	ds = NULL;

	/* start from the root object, because some laptops define
	 * _DCK methods outside the scope of PCI (IBM x-series laptop)
	 */
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
			ACPI_UINT32_MAX, find_dock, &num, NULL);

	return num;
}



void remove_dock_station(void)
{
	struct dependent_device *dd, *tmp;
	if (ds) {
		if (ACPI_FAILURE(acpi_remove_notify_handler(ds->handle,
			ACPI_SYSTEM_NOTIFY, handle_hotplug_event_dock)))
			err("failed to remove dock notify handler\n");

		/* free all dependent devices */
		list_for_each_entry_safe(dd, tmp, &ds->dependent_devices,
				device_list)
			kfree(dd);

		/* no need to touch the pci_dependent_device list,
		 * cause all memory was freed above
		 */
		kfree(ds);
	}
}


