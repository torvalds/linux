// SPDX-License-Identifier: GPL-2.0-only
/*
 * ACPI Hardware Error Device (PNP0C33) Driver
 *
 * Copyright (C) 2010, Intel Corp.
 *	Author: Huang Ying <ying.huang@intel.com>
 *
 * ACPI Hardware Error Device is used to report some hardware errors
 * notified via SCI, mainly the corrected errors.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/acpi.h>
#include <linux/platform_device.h>
#include <acpi/hed.h>

static const struct acpi_device_id acpi_hed_ids[] = {
	{"PNP0C33", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, acpi_hed_ids);

static bool hed_present;

static BLOCKING_NOTIFIER_HEAD(acpi_hed_notify_list);

int register_acpi_hed_notifier(struct notifier_block *nb)
{
	return blocking_notifier_chain_register(&acpi_hed_notify_list, nb);
}
EXPORT_SYMBOL_GPL(register_acpi_hed_notifier);

void unregister_acpi_hed_notifier(struct notifier_block *nb)
{
	blocking_notifier_chain_unregister(&acpi_hed_notify_list, nb);
}
EXPORT_SYMBOL_GPL(unregister_acpi_hed_notifier);

/*
 * SCI to report hardware error is forwarded to the listeners of HED,
 * it is used by HEST Generic Hardware Error Source with notify type
 * SCI.
 */
static void acpi_hed_notify(acpi_handle handle, u32 event, void *data)
{
	blocking_notifier_call_chain(&acpi_hed_notify_list, 0, NULL);
}

static int acpi_hed_probe(struct platform_device *pdev)
{
	int err;

	/* Only one hardware error device */
	if (hed_present)
		return -EINVAL;

	err = devm_acpi_install_notify_handler(&pdev->dev, ACPI_DEVICE_NOTIFY,
					       acpi_hed_notify, NULL);
	if (err)
		return err;

	hed_present = true;
	return 0;
}

static void acpi_hed_remove(struct platform_device *pdev)
{
	hed_present = false;
}

static struct platform_driver acpi_hed_driver = {
	.probe = acpi_hed_probe,
	.remove = acpi_hed_remove,
	.driver = {
		.name = "acpi-hardware-error-device",
		.acpi_match_table = acpi_hed_ids,
	},
};

static int __init acpi_hed_driver_init(void)
{
	return platform_driver_register(&acpi_hed_driver);
}
subsys_initcall(acpi_hed_driver_init);

MODULE_AUTHOR("Huang Ying");
MODULE_DESCRIPTION("ACPI Hardware Error Device Driver");
MODULE_LICENSE("GPL");
