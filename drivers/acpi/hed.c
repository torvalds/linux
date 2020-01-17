// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI Hardware Error Device (PNP0C33) Driver
 *
 * Copyright (C) 2010, Intel Corp.
 *	Author: Huang Ying <ying.huang@intel.com>
 *
 * ACPI Hardware Error Device is used to report some hardware errors
 * yestified via SCI, mainly the corrected errors.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <acpi/hed.h>

static const struct acpi_device_id acpi_hed_ids[] = {
	{"PNP0C33", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, acpi_hed_ids);

static acpi_handle hed_handle;

static BLOCKING_NOTIFIER_HEAD(acpi_hed_yestify_list);

int register_acpi_hed_yestifier(struct yestifier_block *nb)
{
	return blocking_yestifier_chain_register(&acpi_hed_yestify_list, nb);
}
EXPORT_SYMBOL_GPL(register_acpi_hed_yestifier);

void unregister_acpi_hed_yestifier(struct yestifier_block *nb)
{
	blocking_yestifier_chain_unregister(&acpi_hed_yestify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_acpi_hed_yestifier);

/*
 * SCI to report hardware error is forwarded to the listeners of HED,
 * it is used by HEST Generic Hardware Error Source with yestify type
 * SCI.
 */
static void acpi_hed_yestify(struct acpi_device *device, u32 event)
{
	blocking_yestifier_call_chain(&acpi_hed_yestify_list, 0, NULL);
}

static int acpi_hed_add(struct acpi_device *device)
{
	/* Only one hardware error device */
	if (hed_handle)
		return -EINVAL;
	hed_handle = device->handle;
	return 0;
}

static int acpi_hed_remove(struct acpi_device *device)
{
	hed_handle = NULL;
	return 0;
}

static struct acpi_driver acpi_hed_driver = {
	.name = "hardware_error_device",
	.class = "hardware_error",
	.ids = acpi_hed_ids,
	.ops = {
		.add = acpi_hed_add,
		.remove = acpi_hed_remove,
		.yestify = acpi_hed_yestify,
	},
};
module_acpi_driver(acpi_hed_driver);

ACPI_MODULE_NAME("hed");
MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("ACPI Hardware Error Device Driver");
MODULE_LICENSE("GPL");
