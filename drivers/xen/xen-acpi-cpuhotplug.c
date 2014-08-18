/*
 * Copyright (C) 2012 Intel Corporation
 *    Author: Liu Jinsong <jinsong.liu@intel.com>
 *    Author: Jiang Yunhong <yunhong.jiang@intel.com>
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
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/cpu.h>
#include <linux/acpi.h>
#include <linux/uaccess.h>
#include <acpi/processor.h>
#include <xen/acpi.h>
#include <xen/interface/platform.h>
#include <asm/xen/hypercall.h>

#define PREFIX "ACPI:xen_cpu_hotplug:"

#define INSTALL_NOTIFY_HANDLER		0
#define UNINSTALL_NOTIFY_HANDLER	1

static acpi_status xen_acpi_cpu_hotadd(struct acpi_processor *pr);

/* --------------------------------------------------------------------------
				Driver Interface
-------------------------------------------------------------------------- */

static int xen_acpi_processor_enable(struct acpi_device *device)
{
	acpi_status status = 0;
	unsigned long long value;
	union acpi_object object = { 0 };
	struct acpi_buffer buffer = { sizeof(union acpi_object), &object };
	struct acpi_processor *pr;

	pr = acpi_driver_data(device);
	if (!pr) {
		pr_err(PREFIX "Cannot find driver data\n");
		return -EINVAL;
	}

	if (!strcmp(acpi_device_hid(device), ACPI_PROCESSOR_OBJECT_HID)) {
		/* Declared with "Processor" statement; match ProcessorID */
		status = acpi_evaluate_object(pr->handle, NULL, NULL, &buffer);
		if (ACPI_FAILURE(status)) {
			pr_err(PREFIX "Evaluating processor object\n");
			return -ENODEV;
		}

		pr->acpi_id = object.processor.proc_id;
	} else {
		/* Declared with "Device" statement; match _UID */
		status = acpi_evaluate_integer(pr->handle, METHOD_NAME__UID,
						NULL, &value);
		if (ACPI_FAILURE(status)) {
			pr_err(PREFIX "Evaluating processor _UID\n");
			return -ENODEV;
		}

		pr->acpi_id = value;
	}

	pr->id = xen_pcpu_id(pr->acpi_id);

	if ((int)pr->id < 0)
		/* This cpu is not presented at hypervisor, try to hotadd it */
		if (ACPI_FAILURE(xen_acpi_cpu_hotadd(pr))) {
			pr_err(PREFIX "Hotadd CPU (acpi_id = %d) failed.\n",
					pr->acpi_id);
			return -ENODEV;
		}

	return 0;
}

static int xen_acpi_processor_add(struct acpi_device *device)
{
	int ret;
	struct acpi_processor *pr;

	if (!device)
		return -EINVAL;

	pr = kzalloc(sizeof(struct acpi_processor), GFP_KERNEL);
	if (!pr)
		return -ENOMEM;

	pr->handle = device->handle;
	strcpy(acpi_device_name(device), ACPI_PROCESSOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_CLASS);
	device->driver_data = pr;

	ret = xen_acpi_processor_enable(device);
	if (ret)
		pr_err(PREFIX "Error when enabling Xen processor\n");

	return ret;
}

static int xen_acpi_processor_remove(struct acpi_device *device)
{
	struct acpi_processor *pr;

	if (!device)
		return -EINVAL;

	pr = acpi_driver_data(device);
	if (!pr)
		return -EINVAL;

	kfree(pr);
	return 0;
}

/*--------------------------------------------------------------
		Acpi processor hotplug support
--------------------------------------------------------------*/

static int is_processor_present(acpi_handle handle)
{
	acpi_status status;
	unsigned long long sta = 0;


	status = acpi_evaluate_integer(handle, "_STA", NULL, &sta);

	if (ACPI_SUCCESS(status) && (sta & ACPI_STA_DEVICE_PRESENT))
		return 1;

	/*
	 * _STA is mandatory for a processor that supports hot plug
	 */
	if (status == AE_NOT_FOUND)
		pr_info(PREFIX "Processor does not support hot plug\n");
	else
		pr_info(PREFIX "Processor Device is not present");
	return 0;
}

static int xen_apic_id(acpi_handle handle)
{
	struct acpi_buffer buffer = { ACPI_ALLOCATE_BUFFER, NULL };
	union acpi_object *obj;
	struct acpi_madt_local_apic *lapic;
	int apic_id;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_MAT", NULL, &buffer)))
		return -EINVAL;

	if (!buffer.length || !buffer.pointer)
		return -EINVAL;

	obj = buffer.pointer;
	if (obj->type != ACPI_TYPE_BUFFER ||
	    obj->buffer.length < sizeof(*lapic)) {
		kfree(buffer.pointer);
		return -EINVAL;
	}

	lapic = (struct acpi_madt_local_apic *)obj->buffer.pointer;

	if (lapic->header.type != ACPI_MADT_TYPE_LOCAL_APIC ||
	    !(lapic->lapic_flags & ACPI_MADT_ENABLED)) {
		kfree(buffer.pointer);
		return -EINVAL;
	}

	apic_id = (uint32_t)lapic->id;
	kfree(buffer.pointer);
	buffer.length = ACPI_ALLOCATE_BUFFER;
	buffer.pointer = NULL;

	return apic_id;
}

static int xen_hotadd_cpu(struct acpi_processor *pr)
{
	int cpu_id, apic_id, pxm;
	struct xen_platform_op op;

	apic_id = xen_apic_id(pr->handle);
	if (apic_id < 0) {
		pr_err(PREFIX "Failed to get apic_id for acpi_id %d\n",
				pr->acpi_id);
		return -ENODEV;
	}

	pxm = xen_acpi_get_pxm(pr->handle);
	if (pxm < 0) {
		pr_err(PREFIX "Failed to get _PXM for acpi_id %d\n",
				pr->acpi_id);
		return pxm;
	}

	op.cmd = XENPF_cpu_hotadd;
	op.u.cpu_add.apic_id = apic_id;
	op.u.cpu_add.acpi_id = pr->acpi_id;
	op.u.cpu_add.pxm = pxm;

	cpu_id = HYPERVISOR_dom0_op(&op);
	if (cpu_id < 0)
		pr_err(PREFIX "Failed to hotadd CPU for acpi_id %d\n",
				pr->acpi_id);

	return cpu_id;
}

static acpi_status xen_acpi_cpu_hotadd(struct acpi_processor *pr)
{
	if (!is_processor_present(pr->handle))
		return AE_ERROR;

	pr->id = xen_hotadd_cpu(pr);
	if ((int)pr->id < 0)
		return AE_ERROR;

	/*
	 * Sync with Xen hypervisor, providing new /sys/.../xen_cpuX
	 * interface after cpu hotadded.
	 */
	xen_pcpu_hotplug_sync();

	return AE_OK;
}

static int acpi_processor_device_remove(struct acpi_device *device)
{
	pr_debug(PREFIX "Xen does not support CPU hotremove\n");

	return -ENOSYS;
}

static void acpi_processor_hotplug_notify(acpi_handle handle,
					  u32 event, void *data)
{
	struct acpi_processor *pr;
	struct acpi_device *device = NULL;
	u32 ost_code = ACPI_OST_SC_NON_SPECIFIC_FAILURE; /* default */
	int result;

	acpi_scan_lock_acquire();

	switch (event) {
	case ACPI_NOTIFY_BUS_CHECK:
	case ACPI_NOTIFY_DEVICE_CHECK:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
			"Processor driver received %s event\n",
			(event == ACPI_NOTIFY_BUS_CHECK) ?
			"ACPI_NOTIFY_BUS_CHECK" : "ACPI_NOTIFY_DEVICE_CHECK"));

		if (!is_processor_present(handle))
			break;

		acpi_bus_get_device(handle, &device);
		if (acpi_device_enumerated(device))
			break;

		result = acpi_bus_scan(handle);
		if (result) {
			pr_err(PREFIX "Unable to add the device\n");
			break;
		}
		device = NULL;
		acpi_bus_get_device(handle, &device);
		if (!acpi_device_enumerated(device)) {
			pr_err(PREFIX "Missing device object\n");
			break;
		}
		ost_code = ACPI_OST_SC_SUCCESS;
		break;

	case ACPI_NOTIFY_EJECT_REQUEST:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "received ACPI_NOTIFY_EJECT_REQUEST\n"));

		if (acpi_bus_get_device(handle, &device)) {
			pr_err(PREFIX "Device don't exist, dropping EJECT\n");
			break;
		}
		pr = acpi_driver_data(device);
		if (!pr) {
			pr_err(PREFIX "Driver data is NULL, dropping EJECT\n");
			break;
		}

		/*
		 * TBD: implement acpi_processor_device_remove if Xen support
		 * CPU hotremove in the future.
		 */
		acpi_processor_device_remove(device);
		break;

	default:
		ACPI_DEBUG_PRINT((ACPI_DB_INFO,
				  "Unsupported event [0x%x]\n", event));

		/* non-hotplug event; possibly handled by other handler */
		goto out;
	}

	(void) acpi_evaluate_ost(handle, event, ost_code, NULL);

out:
	acpi_scan_lock_release();
}

static acpi_status is_processor_device(acpi_handle handle)
{
	struct acpi_device_info *info;
	char *hid;
	acpi_status status;

	status = acpi_get_object_info(handle, &info);
	if (ACPI_FAILURE(status))
		return status;

	if (info->type == ACPI_TYPE_PROCESSOR) {
		kfree(info);
		return AE_OK;	/* found a processor object */
	}

	if (!(info->valid & ACPI_VALID_HID)) {
		kfree(info);
		return AE_ERROR;
	}

	hid = info->hardware_id.string;
	if ((hid == NULL) || strcmp(hid, ACPI_PROCESSOR_DEVICE_HID)) {
		kfree(info);
		return AE_ERROR;
	}

	kfree(info);
	return AE_OK;	/* found a processor device object */
}

static acpi_status
processor_walk_namespace_cb(acpi_handle handle,
			    u32 lvl, void *context, void **rv)
{
	acpi_status status;
	int *action = context;

	status = is_processor_device(handle);
	if (ACPI_FAILURE(status))
		return AE_OK;	/* not a processor; continue to walk */

	switch (*action) {
	case INSTALL_NOTIFY_HANDLER:
		acpi_install_notify_handler(handle,
					    ACPI_SYSTEM_NOTIFY,
					    acpi_processor_hotplug_notify,
					    NULL);
		break;
	case UNINSTALL_NOTIFY_HANDLER:
		acpi_remove_notify_handler(handle,
					   ACPI_SYSTEM_NOTIFY,
					   acpi_processor_hotplug_notify);
		break;
	default:
		break;
	}

	/* found a processor; skip walking underneath */
	return AE_CTRL_DEPTH;
}

static
void acpi_processor_install_hotplug_notify(void)
{
	int action = INSTALL_NOTIFY_HANDLER;
	acpi_walk_namespace(ACPI_TYPE_ANY,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    processor_walk_namespace_cb, NULL, &action, NULL);
}

static
void acpi_processor_uninstall_hotplug_notify(void)
{
	int action = UNINSTALL_NOTIFY_HANDLER;
	acpi_walk_namespace(ACPI_TYPE_ANY,
			    ACPI_ROOT_OBJECT,
			    ACPI_UINT32_MAX,
			    processor_walk_namespace_cb, NULL, &action, NULL);
}

static const struct acpi_device_id processor_device_ids[] = {
	{ACPI_PROCESSOR_OBJECT_HID, 0},
	{ACPI_PROCESSOR_DEVICE_HID, 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, processor_device_ids);

static struct acpi_driver xen_acpi_processor_driver = {
	.name = "processor",
	.class = ACPI_PROCESSOR_CLASS,
	.ids = processor_device_ids,
	.ops = {
		.add = xen_acpi_processor_add,
		.remove = xen_acpi_processor_remove,
		},
};

static int __init xen_acpi_processor_init(void)
{
	int result = 0;

	if (!xen_initial_domain())
		return -ENODEV;

	/* unregister the stub which only used to reserve driver space */
	xen_stub_processor_exit();

	result = acpi_bus_register_driver(&xen_acpi_processor_driver);
	if (result < 0) {
		xen_stub_processor_init();
		return result;
	}

	acpi_processor_install_hotplug_notify();
	return 0;
}

static void __exit xen_acpi_processor_exit(void)
{
	if (!xen_initial_domain())
		return;

	acpi_processor_uninstall_hotplug_notify();

	acpi_bus_unregister_driver(&xen_acpi_processor_driver);

	/*
	 * stub reserve space again to prevent any chance of native
	 * driver loading.
	 */
	xen_stub_processor_init();
	return;
}

module_init(xen_acpi_processor_init);
module_exit(xen_acpi_processor_exit);
ACPI_MODULE_NAME("xen-acpi-cpuhotplug");
MODULE_AUTHOR("Liu Jinsong <jinsong.liu@intel.com>");
MODULE_DESCRIPTION("Xen Hotplug CPU Driver");
MODULE_LICENSE("GPL");
