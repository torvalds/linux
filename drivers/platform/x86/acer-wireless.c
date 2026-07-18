// SPDX-License-Identifier: GPL-2.0-only
/*
 * Acer Wireless Radio Control Driver
 *
 * Copyright (C) 2017 Endless Mobile, Inc.
 */

#include <linux/acpi.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/platform_device.h>
#include <linux/types.h>

static const struct acpi_device_id acer_wireless_acpi_ids[] = {
	{"10251229", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, acer_wireless_acpi_ids);

static void acer_wireless_notify(acpi_handle handle, u32 event, void *data)
{
	struct device *dev = data;
	struct input_dev *idev = dev_get_drvdata(dev);

	dev_dbg(dev, "event=%#x\n", event);
	if (event != 0x80) {
		dev_notice(dev, "Unknown SMKB event: %#x\n", event);
		return;
	}
	input_report_key(idev, KEY_RFKILL, 1);
	input_sync(idev);
	input_report_key(idev, KEY_RFKILL, 0);
	input_sync(idev);
}

static int acer_wireless_probe(struct platform_device *pdev)
{
	struct acpi_device *adev;
	struct input_dev *idev;
	int ret;

	adev = ACPI_COMPANION(&pdev->dev);
	if (!adev)
		return -ENODEV;

	idev = devm_input_allocate_device(&pdev->dev);
	if (!idev)
		return -ENOMEM;

	platform_set_drvdata(pdev, idev);
	idev->name = "Acer Wireless Radio Control";
	idev->phys = "acer-wireless/input0";
	idev->id.bustype = BUS_HOST;
	idev->id.vendor = PCI_VENDOR_ID_AI;
	idev->id.product = 0x1229;
	set_bit(EV_KEY, idev->evbit);
	set_bit(KEY_RFKILL, idev->keybit);

	ret = input_register_device(idev);
	if (ret)
		return ret;

	return acpi_dev_install_notify_handler(adev, ACPI_DEVICE_NOTIFY,
					       acer_wireless_notify,
					       &pdev->dev);
}

static void acer_wireless_remove(struct platform_device *pdev)
{
	acpi_dev_remove_notify_handler(ACPI_COMPANION(&pdev->dev),
				       ACPI_DEVICE_NOTIFY,
				       acer_wireless_notify);
}

static struct platform_driver acer_wireless_driver = {
	.probe = acer_wireless_probe,
	.remove = acer_wireless_remove,
	.driver = {
		.name = "Acer Wireless Radio Control Driver",
		.acpi_match_table = acer_wireless_acpi_ids,
	},
};
module_platform_driver(acer_wireless_driver);

MODULE_DESCRIPTION("Acer Wireless Radio Control Driver");
MODULE_AUTHOR("Chris Chiu <chiu@gmail.com>");
MODULE_LICENSE("GPL v2");
