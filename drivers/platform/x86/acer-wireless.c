/*
 * Acer Wireless Radio Control Driver
 *
 * Copyright (C) 2017 Endless Mobile, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci_ids.h>
#include <linux/types.h>

static const struct acpi_device_id acer_wireless_acpi_ids[] = {
	{"10251229", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, acer_wireless_acpi_ids);

static void acer_wireless_notify(struct acpi_device *adev, u32 event)
{
	struct input_dev *idev = acpi_driver_data(adev);

	dev_dbg(&adev->dev, "event=%#x\n", event);
	if (event != 0x80) {
		dev_notice(&adev->dev, "Unknown SMKB event: %#x\n", event);
		return;
	}
	input_report_key(idev, KEY_RFKILL, 1);
	input_report_key(idev, KEY_RFKILL, 0);
	input_sync(idev);
}

static int acer_wireless_add(struct acpi_device *adev)
{
	struct input_dev *idev;

	idev = devm_input_allocate_device(&adev->dev);
	if (!idev)
		return -ENOMEM;

	adev->driver_data = idev;
	idev->name = "Acer Wireless Radio Control";
	idev->phys = "acer-wireless/input0";
	idev->id.bustype = BUS_HOST;
	idev->id.vendor = PCI_VENDOR_ID_AI;
	idev->id.product = 0x1229;
	set_bit(EV_KEY, idev->evbit);
	set_bit(KEY_RFKILL, idev->keybit);

	return input_register_device(idev);
}

static struct acpi_driver acer_wireless_driver = {
	.name = "Acer Wireless Radio Control Driver",
	.class = "hotkey",
	.ids = acer_wireless_acpi_ids,
	.ops = {
		.add = acer_wireless_add,
		.notify = acer_wireless_notify,
	},
};
module_acpi_driver(acer_wireless_driver);

MODULE_DESCRIPTION("Acer Wireless Radio Control Driver");
MODULE_AUTHOR("Chris Chiu <chiu@gmail.com>");
MODULE_LICENSE("GPL v2");
