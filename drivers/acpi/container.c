/*
 * acpi_container.c  - ACPI Generic Container Driver
 * ($Revision: )
 *
 * Copyright (C) 2004 Anil S Keshavamurthy (anil.s.keshavamurthy@intel.com)
 * Copyright (C) 2004 Keiichiro Tokunaga (tokunaga.keiich@jp.fujitsu.com)
 * Copyright (C) 2004 Motoyuki Ito (motoyuki@soft.fujitsu.com)
 * Copyright (C) 2004 Intel Corp.
 * Copyright (C) 2004 FUJITSU LIMITED
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
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>

#define PREFIX "ACPI: "

#define _COMPONENT			ACPI_CONTAINER_COMPONENT
ACPI_MODULE_NAME("container");

static const struct acpi_device_id container_device_ids[] = {
	{"ACPI0004", 0},
	{"PNP0A05", 0},
	{"PNP0A06", 0},
	{"", 0},
};

static int container_device_attach(struct acpi_device *device,
				   const struct acpi_device_id *not_used)
{
	/*
	 * FIXME: This is necessary, so that acpi_eject_store() doesn't return
	 * -ENODEV for containers.
	 */
	return 1;
}

static struct acpi_scan_handler container_device_handler = {
	.ids = container_device_ids,
	.attach = container_device_attach,
};

static int is_device_present(acpi_handle handle)
{
	acpi_handle temp;
	acpi_status status;
	unsigned long long sta;


	status = acpi_get_handle(handle, "_STA", &temp);
	if (ACPI_FAILURE(status))
		return 1;	/* _STA not found, assume device present */

	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);
	if (ACPI_FAILURE(status))
		return 0;	/* Firmware error */

	return ((sta & ACPI_STA_DEVICE_PRESENT) == ACPI_STA_DEVICE_PRESENT);
}

static void container_notify_cb(acpi_handle handle, u32 type, void *context)
{
	struct acpi_device *device = NULL;
	int result;
	int present;
	acpi_status status;
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE; /* default */

	acpi_scan_lock_acquire();

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* Fall through */
	case ACPI_NOTIFY_DEVICE_CHECK:
		pr_debug("Container driver received %s event\n",
		       (type == ACPI_NOTIFY_BUS_CHECK) ?
		       "ACPI_NOTIFY_BUS_CHECK" : "ACPI_NOTIFY_DEVICE_CHECK");

		present = is_device_present(handle);
		status = acpi_bus_get_device(handle, &device);
		if (!present) {
			if (ACPI_SUCCESS(status)) {
				/* device exist and this is a remove request */
				device->flags.eject_pending = 1;
				kobject_uevent(&device->dev.kobj, KOBJ_OFFLINE);
				goto out;
			}
			break;
		}

		if (!ACPI_FAILURE(status) || device)
			break;

		result = acpi_bus_scan(handle);
		if (result) {
			acpi_handle_warn(handle, "Failed to add container\n");
			break;
		}
		result = acpi_bus_get_device(handle, &device);
		if (result) {
			acpi_handle_warn(handle, "Missing device object\n");
			break;
		}

		kobject_uevent(&device->dev.kobj, KOBJ_ONLINE);
		ost_code = ACPI_OST_SC_SUCCESS;
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		if (!acpi_bus_get_device(handle, &device) && device) {
			device->flags.eject_pending = 1;
			kobject_uevent(&device->dev.kobj, KOBJ_OFFLINE);
			goto out;
		}
		break;

	default:
		/* non-hotplug event; possibly handled by other handler */
		goto out;
	}

	/* Inform firmware that the hotplug operation has completed */
	(void) acpi_evaluate_hotplug_ost(handle, type, ost_code, NULL);

 out:
	acpi_scan_lock_release();
}

static bool is_container(acpi_handle handle)
{
	struct acpi_device_info *info;
	bool ret = false;

	if (ACPI_FAILURE(acpi_get_object_info(handle, &info)))
		return false;

	if (info->valid & ACPI_VALID_HID) {
		const struct acpi_device_id *id;

		for (id = container_device_ids; id->id[0]; id++) {
			ret = !strcmp((char *)id->id, info->hardware_id.string);
			if (ret)
				break;
		}
	}
	kfree(info);
	return ret;
}

static acpi_status acpi_container_register_notify_handler(acpi_handle handle,
							  u32 lvl, void *ctxt,
							  void **retv)
{
	if (is_container(handle))
		acpi_install_notify_handler(handle, ACPI_SYSTEM_NOTIFY,
					    container_notify_cb, NULL);

	return AE_OK;
}

void __init acpi_container_init(void)
{
	acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT, ACPI_UINT32_MAX,
			    acpi_container_register_notify_handler, NULL,
			    NULL, NULL);

	acpi_scan_add_handler(&container_device_handler);
}
