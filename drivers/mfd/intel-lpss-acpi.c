// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel LPSS ACPI support.
 *
 * Copyright (C) 2015, Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/acpi.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/pxa2xx_ssp.h>

#include "intel-lpss.h"

static const struct property_entry spt_spi_properties[] = {
	PROPERTY_ENTRY_U32("intel,spi-pxa2xx-type", LPSS_SPT_SSP),
	{ }
};

static const struct software_node spt_spi_node = {
	.properties = spt_spi_properties,
};

static const struct intel_lpss_platform_info spt_info = {
	.clk_rate = 120000000,
	.swnode = &spt_spi_node,
};

static const struct property_entry spt_i2c_properties[] = {
	PROPERTY_ENTRY_U32("i2c-sda-hold-time-ns", 230),
	{ },
};

static const struct software_node spt_i2c_node = {
	.properties = spt_i2c_properties,
};

static const struct intel_lpss_platform_info spt_i2c_info = {
	.clk_rate = 120000000,
	.swnode = &spt_i2c_node,
};

static const struct property_entry uart_properties[] = {
	PROPERTY_ENTRY_U32("reg-io-width", 4),
	PROPERTY_ENTRY_U32("reg-shift", 2),
	PROPERTY_ENTRY_BOOL("snps,uart-16550-compatible"),
	{ },
};

static const struct software_node uart_node = {
	.properties = uart_properties,
};

static const struct intel_lpss_platform_info spt_uart_info = {
	.clk_rate = 120000000,
	.clk_con_id = "baudclk",
	.swnode = &uart_node,
};

static const struct property_entry bxt_spi_properties[] = {
	PROPERTY_ENTRY_U32("intel,spi-pxa2xx-type", LPSS_BXT_SSP),
	{ }
};

static const struct software_node bxt_spi_node = {
	.properties = bxt_spi_properties,
};

static const struct intel_lpss_platform_info bxt_info = {
	.clk_rate = 100000000,
	.swnode = &bxt_spi_node,
};

static const struct property_entry bxt_i2c_properties[] = {
	PROPERTY_ENTRY_U32("i2c-sda-hold-time-ns", 42),
	PROPERTY_ENTRY_U32("i2c-sda-falling-time-ns", 171),
	PROPERTY_ENTRY_U32("i2c-scl-falling-time-ns", 208),
	{ },
};

static const struct software_node bxt_i2c_node = {
	.properties = bxt_i2c_properties,
};

static const struct intel_lpss_platform_info bxt_i2c_info = {
	.clk_rate = 133000000,
	.swnode = &bxt_i2c_node,
};

static const struct property_entry apl_i2c_properties[] = {
	PROPERTY_ENTRY_U32("i2c-sda-hold-time-ns", 207),
	PROPERTY_ENTRY_U32("i2c-sda-falling-time-ns", 171),
	PROPERTY_ENTRY_U32("i2c-scl-falling-time-ns", 208),
	{ },
};

static const struct software_node apl_i2c_node = {
	.properties = apl_i2c_properties,
};

static const struct intel_lpss_platform_info apl_i2c_info = {
	.clk_rate = 133000000,
	.swnode = &apl_i2c_node,
};

static const struct property_entry cnl_spi_properties[] = {
	PROPERTY_ENTRY_U32("intel,spi-pxa2xx-type", LPSS_CNL_SSP),
	{ }
};

static const struct software_node cnl_spi_node = {
	.properties = cnl_spi_properties,
};

static const struct intel_lpss_platform_info cnl_info = {
	.clk_rate = 120000000,
	.swnode = &cnl_spi_node,
};

static const struct intel_lpss_platform_info cnl_i2c_info = {
	.clk_rate = 216000000,
	.swnode = &spt_i2c_node,
};

static const struct acpi_device_id intel_lpss_acpi_ids[] = {
	/* SPT */
	{ "INT3440", (kernel_ulong_t)&spt_info },
	{ "INT3441", (kernel_ulong_t)&spt_info },
	{ "INT3442", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3443", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3444", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3445", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3446", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3447", (kernel_ulong_t)&spt_i2c_info },
	{ "INT3448", (kernel_ulong_t)&spt_uart_info },
	{ "INT3449", (kernel_ulong_t)&spt_uart_info },
	{ "INT344A", (kernel_ulong_t)&spt_uart_info },
	/* CNL */
	{ "INT34B0", (kernel_ulong_t)&cnl_info },
	{ "INT34B1", (kernel_ulong_t)&cnl_info },
	{ "INT34B2", (kernel_ulong_t)&cnl_i2c_info },
	{ "INT34B3", (kernel_ulong_t)&cnl_i2c_info },
	{ "INT34B4", (kernel_ulong_t)&cnl_i2c_info },
	{ "INT34B5", (kernel_ulong_t)&cnl_i2c_info },
	{ "INT34B6", (kernel_ulong_t)&cnl_i2c_info },
	{ "INT34B7", (kernel_ulong_t)&cnl_i2c_info },
	{ "INT34B8", (kernel_ulong_t)&spt_uart_info },
	{ "INT34B9", (kernel_ulong_t)&spt_uart_info },
	{ "INT34BA", (kernel_ulong_t)&spt_uart_info },
	{ "INT34BC", (kernel_ulong_t)&cnl_info },
	/* BXT */
	{ "80860AAC", (kernel_ulong_t)&bxt_i2c_info },
	{ "80860ABC", (kernel_ulong_t)&bxt_info },
	{ "80860AC2", (kernel_ulong_t)&bxt_info },
	/* APL */
	{ "80865AAC", (kernel_ulong_t)&apl_i2c_info },
	{ "80865ABC", (kernel_ulong_t)&bxt_info },
	{ "80865AC2", (kernel_ulong_t)&bxt_info },
	{ }
};
MODULE_DEVICE_TABLE(acpi, intel_lpss_acpi_ids);

static int intel_lpss_acpi_probe(struct platform_device *pdev)
{
	struct intel_lpss_platform_info *info;
	const struct acpi_device_id *id;
	int ret;

	id = acpi_match_device(intel_lpss_acpi_ids, &pdev->dev);
	if (!id)
		return -ENODEV;

	info = devm_kmemdup(&pdev->dev, (void *)id->driver_data, sizeof(*info),
			    GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	info->mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->irq = platform_get_irq(pdev, 0);

	ret = intel_lpss_probe(&pdev->dev, info);
	if (ret)
		return ret;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	return 0;
}

static int intel_lpss_acpi_remove(struct platform_device *pdev)
{
	intel_lpss_remove(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static INTEL_LPSS_PM_OPS(intel_lpss_acpi_pm_ops);

static struct platform_driver intel_lpss_acpi_driver = {
	.probe = intel_lpss_acpi_probe,
	.remove = intel_lpss_acpi_remove,
	.driver = {
		.name = "intel-lpss",
		.acpi_match_table = intel_lpss_acpi_ids,
		.pm = &intel_lpss_acpi_pm_ops,
	},
};

module_platform_driver(intel_lpss_acpi_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel LPSS ACPI driver");
MODULE_LICENSE("GPL v2");
