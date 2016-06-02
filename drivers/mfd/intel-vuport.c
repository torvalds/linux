/*
 * MFD driver for Intel virtual USB port
 *
 * Copyright(c) 2016 Intel Corporation.
 * Author: Lu Baolu <baolu.lu@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/module.h>
#include <linux/gpio.h>
#include <linux/mfd/core.h>
#include <linux/property.h>
#include <linux/platform_device.h>

/* ACPI GPIO Mappings */
static const struct acpi_gpio_params id_gpio = { 0, 0, false };
static const struct acpi_gpio_params vbus_gpio = { 1, 0, false };
static const struct acpi_gpio_params mux_gpio = { 2, 0, false };
static const struct acpi_gpio_mapping acpi_usb_gpios[] = {
	{ "id-gpios", &id_gpio, 1 },
	{ "gpio-gpios", &vbus_gpio, 1 },
	{ "usb_mux-gpios", &mux_gpio, 1 },
	{ },
};

static struct property_entry reg_properties[] = {
	PROPERTY_ENTRY_STRING("supply-name", "regulator-usb-gpio"),
	{ },
};

static const struct mfd_cell intel_vuport_mfd_cells[] = {
	{ .name = "extcon-usb-gpio", },
	{
		.name = "reg-fixed-voltage",
		.properties = reg_properties,
	},
	{ .name = "intel-mux-gpio", },
};

static int vuport_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	int ret;

	ret = acpi_dev_add_driver_gpios(ACPI_COMPANION(dev), acpi_usb_gpios);
	if (ret)
		return ret;

	return mfd_add_devices(&pdev->dev, PLATFORM_DEVID_NONE,
				intel_vuport_mfd_cells,
				ARRAY_SIZE(intel_vuport_mfd_cells), NULL, 0,
				NULL);
}

static int vuport_remove(struct platform_device *pdev)
{
	mfd_remove_devices(&pdev->dev);
	acpi_dev_remove_driver_gpios(ACPI_COMPANION(&pdev->dev));

	return 0;
}

static struct acpi_device_id vuport_acpi_match[] = {
	{ "INT3496" },
	{ }
};
MODULE_DEVICE_TABLE(acpi, vuport_acpi_match);

static struct platform_driver vuport_driver = {
	.driver = {
		.name = "intel-vuport",
		.acpi_match_table = ACPI_PTR(vuport_acpi_match),
	},
	.probe = vuport_probe,
	.remove = vuport_remove,
};

module_platform_driver(vuport_driver);

MODULE_AUTHOR("Lu Baolu <baolu.lu@linux.intel.com>");
MODULE_DESCRIPTION("Intel virtual USB port");
MODULE_LICENSE("GPL v2");
