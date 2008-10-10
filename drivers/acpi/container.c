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
#include <linux/types.h>
#include <linux/acpi.h>
#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/container.h>

#define ACPI_CONTAINER_DEVICE_NAME	"ACPI container device"
#define ACPI_CONTAINER_CLASS		"container"

#define INSTALL_NOTIFY_HANDLER		1
#define UNINSTALL_NOTIFY_HANDLER	2

#define ACPI_CONTAINER_COMPONENT	0x01000000
#define _COMPONENT			ACPI_CONTAINER_COMPONENT
ACPI_MODULE_NAME("container");

MODULE_AUTHOR("Anil S Keshavamurthy");
MODULE_DESCRIPTION("ACPI container driver");
MODULE_LICENSE("GPL");

static int acpi_container_add(struct acpi_device *device);
static int acpi_container_remove(struct acpi_device *device, int type);

static const struct acpi_device_id container_device_ids[] = {
	{"ACPI0004", 0},
	{"PNP0A05", 0},
	{"PNP0A06", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, container_device_ids);

static struct acpi_driver acpi_container_driver = {
	.name = "container",
	.class = ACPI_CONTAINER_CLASS,
	.ids = container_device_ids,
	.ops = {
		.add = acpi_container_add,
		.remove = acpi_container_remove,
		},
};

/*******************************************************************/

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

/*******************************************************************/
static int acpi_container_add(struct acpi_device *device)
{
	struct acpi_container *container;


	if (!device) {
		printk(KERN_ERR PREFIX "device is NULL\n");
		return -EINVAL;
	}

	container = kzalloc(sizeof(struct acpi_container), GFP_KERNEL);
	if (!container)
		return -ENOMEM;

	container->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_CONTAINER_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_CONTAINER_CLASS);
	acpi_driver_data(device) = container;

	ACPI_DEBUG_PRINT((ACPI_DB_INFO, "Device <%s> bid <%s>\n",
			  acpi_device_name(device), acpi_device_bid(device)));

	return 0;
}

static int acpi_container_remove(struct acpi_device *device, int type)
{
	acpi_status status = AE_OK;
	struct acpi_container *pc = NULL;

	pc = acpi_driver_data(device);
	kfree(pc);
	return status;
}

static int container_device_add(struct acpi_device **device, acpi_handle handle)
{
	acpi_handle phandle;
	struct acpi_device *pdev;
	int result;


	if (acpi_get_parent(handle, &phandle)) {
		return -ENODEV;
	}

	if (acpi_bus_get_device(phandle, &pdev)) {
		return -ENODEV;
	}

	if (acpi_bus_add(device, pdev, handle, ACPI_BUS_TYPE_DEVICE)) {
		return -ENODEV;
	}

	result = acpi_bus_start(*device);

	return result;
}

static void container_notify_cb(acpi_handle handle, u32 type, void *context)
{
	struct acpi_device *device = NULL;
	int result;
	int present;
	acpi_status status;


	present = is_device_present(handle);

	switch (type) {
	case ACPI_NOTIFY_BUS_CHECK:
		/* Fall through */
	case ACPI_NOTIFY_DEVICE_CHECK:
		printk("Container driver received %s event\n",
		       (type == ACPI_NOTIFY_BUS_CHECK) ?
		       "ACPI_NOTIFY_BUS_CHECK" : "ACPI_NOTIFY_DEVICE_CHECK");
		status = acpi_bus_get_device(handle, &device);
		if (present) {
			if (ACPI_FAILURE(status) || !device) {
				result = container_device_add(&device, handle);
				if (!result)
					kobject_uevent(&device->dev.kobj,
						       KOBJ_ONLINE);
				else
					printk("Failed to add container\n");
			}
		} else {
			if (ACPI_SUCCESS(status)) {
				/* device exist and this is a remove request */
				kobject_uevent(&device->dev.kobj, KOBJ_OFFLINE);
			}
		}
		break;
	case ACPI_NOTIFY_EJECT_REQUEST:
		if (!acpi_bus_get_device(handle, &device) && device) {
			kobject_uevent(&device->dev.kobj, KOBJ_OFFLINE);
		}
		break;
	default:
		break;
	}
	return;
}

static acpi_status
container_walk_namespace_cb(acpi_handle handle,
			    u32 lvl, void *context, void **rv)
{
	char *hid = NULL;
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	struct acpi_device_info *info;
	acpi_status status;
	int *action = context;


	status = acpi_get_object_info(handle, &buffer);
	if (ACPI_FAILURE(status) || !buffer.pointer) {
		return AE_OK;
	}

	info = buffer.pointer;
	if (info->valid & ACPI_VALID_HID)
		hid = info->hardware_id.value;

	if (hid == NULL) {
		goto end;
	}

	if (strcmp(hid, "ACPI0004") && strcmp(hid, "PNP0A05") &&
	    strcmp(hid, "PNP0A06")) {
		goto end;
	}

	switch (*action) {
	case INSTALL_NOTIFY_HANDLER:
		acpi_install_notify_handler(handle,
					    ACPI_SYSTEM_NOTIFY,
					    container_notify_cb, NULL);
		break;
	case UNINSTALL_NOTIFY_HANDLER:
		acpi_remove_notify_handler(handle,
					   ACPI_SYSTEM_NOTIFY,
					   container_notify_cb);
		break;
	default:
		break;
	}

      end:
	kfree(buffer.pointer);

	return AE_OK;
}

static int __init acpi_container_init(void)
{
	int result = 0;
	int action = INSTALL_NOTIFY_HANDLER;

	result = acpi_bus_register_driver(&acpi_container_driver);
	if (result < 0) {
		return (result);
	}

	/* register notify handler to every container device */
	acpi_walk_namespace(ACPI_TYPE_DEVICE,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    container_walk_namespace_cb, &action, NULL);

	return (0);
}

static void __exit acpi_container_exit(void)
{
	int action = UNINSTALL_NOTIFY_HANDLER;


	acpi_walk_namespace(ACPI_TYPE_DEVICE,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    container_walk_namespace_cb, &action, NULL);

	acpi_bus_unregister_driver(&acpi_container_driver);

	return;
}

module_init(acpi_container_init);
module_exit(acpi_container_exit);
