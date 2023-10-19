// SPDX-License-Identifier: GPL-2.0-only
/*
 * xen-acpi-pad.c - Xen pad interface
 *
 * Copyright (c) 2012, Intel Corporation.
 *    Author: Liu, Jinsong <jinsong.liu@intel.com>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <xen/xen.h>
#include <xen/interface/version.h>
#include <xen/xen-ops.h>
#include <asm/xen/hypercall.h>

#define ACPI_PROCESSOR_AGGREGATOR_CLASS	"acpi_pad"
#define ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME "Processor Aggregator"
#define ACPI_PROCESSOR_AGGREGATOR_NOTIFY 0x80
static DEFINE_MUTEX(xen_cpu_lock);

static int xen_acpi_pad_idle_cpus(unsigned int idle_nums)
{
	struct xen_platform_op op;

	op.cmd = XENPF_core_parking;
	op.u.core_parking.type = XEN_CORE_PARKING_SET;
	op.u.core_parking.idle_nums = idle_nums;

	return HYPERVISOR_platform_op(&op);
}

static int xen_acpi_pad_idle_cpus_num(void)
{
	struct xen_platform_op op;

	op.cmd = XENPF_core_parking;
	op.u.core_parking.type = XEN_CORE_PARKING_GET;

	return HYPERVISOR_platform_op(&op)
	       ?: op.u.core_parking.idle_nums;
}

/*
 * Query firmware how many CPUs should be idle
 * return -1 on failure
 */
static int acpi_pad_pur(acpi_handle handle)
{
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *package;
	int num = -1;

	if (ACPI_FAILURE(acpi_evaluate_object(handle, "_PUR", NULL, &buffer)))
		return num;

	if (!buffer.length || !buffer.pointer)
		return num;

	package = buffer.pointer;

	if (package->type == ACPI_TYPE_PACKAGE &&
		package->package.count == 2 &&
		package->package.elements[0].integer.value == 1) /* rev 1 */
		num = package->package.elements[1].integer.value;

	kfree(buffer.pointer);
	return num;
}

static void acpi_pad_handle_notify(acpi_handle handle)
{
	int idle_nums;
	struct acpi_buffer param = {
		.length = 4,
		.pointer = (void *)&idle_nums,
	};


	mutex_lock(&xen_cpu_lock);
	idle_nums = acpi_pad_pur(handle);
	if (idle_nums < 0) {
		mutex_unlock(&xen_cpu_lock);
		return;
	}

	idle_nums = xen_acpi_pad_idle_cpus(idle_nums)
		    ?: xen_acpi_pad_idle_cpus_num();
	if (idle_nums >= 0)
		acpi_evaluate_ost(handle, ACPI_PROCESSOR_AGGREGATOR_NOTIFY,
				  0, &param);
	mutex_unlock(&xen_cpu_lock);
}

static void acpi_pad_notify(acpi_handle handle, u32 event,
	void *data)
{
	switch (event) {
	case ACPI_PROCESSOR_AGGREGATOR_NOTIFY:
		acpi_pad_handle_notify(handle);
		break;
	default:
		pr_warn("Unsupported event [0x%x]\n", event);
		break;
	}
}

static int acpi_pad_add(struct acpi_device *device)
{
	acpi_status status;

	strcpy(acpi_device_name(device), ACPI_PROCESSOR_AGGREGATOR_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_PROCESSOR_AGGREGATOR_CLASS);

	status = acpi_install_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, acpi_pad_notify, device);
	if (ACPI_FAILURE(status))
		return -ENODEV;

	return 0;
}

static int acpi_pad_remove(struct acpi_device *device)
{
	mutex_lock(&xen_cpu_lock);
	xen_acpi_pad_idle_cpus(0);
	mutex_unlock(&xen_cpu_lock);

	acpi_remove_notify_handler(device->handle,
		ACPI_DEVICE_NOTIFY, acpi_pad_notify);
	return 0;
}

static const struct acpi_device_id pad_device_ids[] = {
	{"ACPI000C", 0},
	{"", 0},
};

static struct acpi_driver acpi_pad_driver = {
	.name = "processor_aggregator",
	.class = ACPI_PROCESSOR_AGGREGATOR_CLASS,
	.ids = pad_device_ids,
	.ops = {
		.add = acpi_pad_add,
		.remove = acpi_pad_remove,
	},
};

static int __init xen_acpi_pad_init(void)
{
	/* Only DOM0 is responsible for Xen acpi pad */
	if (!xen_initial_domain())
		return -ENODEV;

	/* Only Xen4.2 or later support Xen acpi pad */
	if (!xen_running_on_version_or_later(4, 2))
		return -ENODEV;

	return acpi_bus_register_driver(&acpi_pad_driver);
}
subsys_initcall(xen_acpi_pad_init);
