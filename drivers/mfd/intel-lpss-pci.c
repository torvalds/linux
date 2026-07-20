// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel LPSS PCI support.
 *
 * Copyright (C) 2015, Intel Corporation
 *
 * Authors: Andy Shevchenko <andriy.shevchenko@linux.intel.com>
 *          Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/device.h>
#include <linux/gfp_types.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>

#include <linux/pxa2xx_ssp.h>

#include <asm/errno.h>

#include "intel-lpss.h"

static const struct pci_device_id quirk_ids[] = {
	{
		/* Microsoft Surface Go (version 1) I2C4 */
		PCI_DEVICE_SUB(PCI_VENDOR_ID_INTEL, 0x9d64, 0x152d, 0x1182),
		.driver_data = QUIRK_IGNORE_RESOURCE_CONFLICTS,
	},
	{
		/* Microsoft Surface Go 2 I2C4 */
		PCI_DEVICE_SUB(PCI_VENDOR_ID_INTEL, 0x9d64, 0x152d, 0x1237),
		.driver_data = QUIRK_IGNORE_RESOURCE_CONFLICTS,
	},
	{
		/* Dell XPS 9530 (2023) */
		PCI_DEVICE_SUB(PCI_VENDOR_ID_INTEL, 0x51fb, 0x1028, 0x0beb),
		.driver_data = QUIRK_CLOCK_DIVIDER_UNITY,
	},
	{ }
};

static int intel_lpss_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *id)
{
	const struct intel_lpss_platform_info *data = (void *)id->driver_data;
	const struct pci_device_id *quirk_pci_info;
	struct intel_lpss_platform_info *info;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return ret;

	ret = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_ALL_TYPES);
	if (ret < 0)
		return ret;

	info = devm_kmemdup(&pdev->dev, data, sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;

	/* No need to check mem and irq here as intel_lpss_probe() does it for us */
	info->mem = pci_resource_n(pdev, 0);
	info->irq = pci_irq_vector(pdev, 0);

	quirk_pci_info = pci_match_id(quirk_ids, pdev);
	if (quirk_pci_info)
		info->quirks = quirk_pci_info->driver_data;

	pdev->d3cold_delay = 0;

	/* Probably it is enough to set this for iDMA capable devices only */
	pci_set_master(pdev);
	pci_try_set_mwi(pdev);

	ret = intel_lpss_probe(&pdev->dev, info);
	if (ret)
		return ret;

	pm_runtime_put(&pdev->dev);
	pm_runtime_allow(&pdev->dev);

	return 0;
}

static void intel_lpss_pci_remove(struct pci_dev *pdev)
{
	pm_runtime_forbid(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	intel_lpss_remove(&pdev->dev);
}

static const struct property_entry spt_spi_properties[] = {
	PROPERTY_ENTRY_U32("intel,spi-pxa2xx-type", LPSS_SPT_SSP),
	{ }
};

static const struct software_node spt_spi_node = {
	.properties = spt_spi_properties,
};

static const struct intel_lpss_platform_info spt_spi_info = {
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

static const struct intel_lpss_platform_info bxt_spi_info = {
	.clk_rate = 100000000,
	.swnode = &bxt_spi_node,
};

static const struct intel_lpss_platform_info bxt_uart_info = {
	.clk_rate = 100000000,
	.clk_con_id = "baudclk",
	.swnode = &uart_node,
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

static const struct property_entry glk_i2c_properties[] = {
	PROPERTY_ENTRY_U32("i2c-sda-hold-time-ns", 313),
	PROPERTY_ENTRY_U32("i2c-sda-falling-time-ns", 171),
	PROPERTY_ENTRY_U32("i2c-scl-falling-time-ns", 290),
	{ },
};

static const struct software_node glk_i2c_node = {
	.properties = glk_i2c_properties,
};

static const struct intel_lpss_platform_info glk_i2c_info = {
	.clk_rate = 133000000,
	.swnode = &glk_i2c_node,
};

static const struct property_entry cnl_spi_properties[] = {
	PROPERTY_ENTRY_U32("intel,spi-pxa2xx-type", LPSS_CNL_SSP),
	{ }
};

static const struct software_node cnl_spi_node = {
	.properties = cnl_spi_properties,
};

static const struct intel_lpss_platform_info cnl_spi_info = {
	.clk_rate = 120000000,
	.swnode = &cnl_spi_node,
};

static const struct intel_lpss_platform_info cnl_i2c_info = {
	.clk_rate = 216000000,
	.swnode = &spt_i2c_node,
};

static const struct intel_lpss_platform_info ehl_i2c_info = {
	.clk_rate = 100000000,
	.swnode = &bxt_i2c_node,
};

static const struct property_entry tgl_spi_properties[] = {
	PROPERTY_ENTRY_U32("intel,spi-pxa2xx-type", LPSS_CNL_SSP),
	{ }
};

static const struct software_node tgl_spi_node = {
	.properties = tgl_spi_properties,
};

static const struct intel_lpss_platform_info tgl_spi_info = {
	.clk_rate = 100000000,
	.swnode = &tgl_spi_node,
};

static const struct pci_device_id intel_lpss_pci_ids[] = {
	/* CML-LP */
	{ PCI_VDEVICE(INTEL, 0x02a8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x02a9), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x02aa), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x02ab), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x02c5), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x02c6), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x02c7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x02e8), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x02e9), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x02ea), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x02eb), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x02fb), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* CML-H */
	{ PCI_VDEVICE(INTEL, 0x06a8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x06a9), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x06aa), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x06ab), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x06c7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x06e8), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x06e9), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x06ea), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x06eb), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x06fb), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* BXT A-Step */
	{ PCI_VDEVICE(INTEL, 0x0aac), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0aae), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0ab0), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0ab2), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0ab4), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0ab6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0ab8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0aba), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x0abc), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x0abe), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x0ac0), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x0ac2), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x0ac4), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x0ac6), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x0aee), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* BXT B-Step */
	{ PCI_VDEVICE(INTEL, 0x1aac), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1aae), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1ab0), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1ab2), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1ab4), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1ab6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1ab8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1aba), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x1abc), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x1abe), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x1ac0), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x1ac2), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x1ac4), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x1ac6), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x1aee), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* EBG */
	{ PCI_VDEVICE(INTEL, 0x1bad), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x1bae), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* GLK */
	{ PCI_VDEVICE(INTEL, 0x31ac), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31ae), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31b0), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31b2), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31b4), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31b6), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31b8), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31ba), .driver_data = (kernel_ulong_t)&glk_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x31bc), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x31be), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x31c0), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x31c2), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x31c4), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x31c6), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x31ee), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* ICL-LP */
	{ PCI_VDEVICE(INTEL, 0x34a8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x34a9), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x34aa), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x34ab), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x34c5), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x34c6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x34c7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x34e8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x34e9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x34ea), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x34eb), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x34fb), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* ICL-N */
	{ PCI_VDEVICE(INTEL, 0x38a8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	/* TGL-H */
	{ PCI_VDEVICE(INTEL, 0x43a7), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x43a8), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x43a9), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x43aa), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x43ab), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x43ad), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43ae), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43d8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43da), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x43e8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43e9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43ea), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43eb), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x43fb), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x43fd), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	/* EHL */
	{ PCI_VDEVICE(INTEL, 0x4b28), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4b29), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4b2a), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4b2b), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4b37), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4b44), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b45), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b4b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b4c), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b4d), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4b78), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b79), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b7a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4b7b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	/* WCL */
	{ PCI_VDEVICE(INTEL, 0x4d25), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4d26), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4d27), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4d30), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4d46), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4d50), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4d51), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4d52), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4d78), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4d79), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4d7a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4d7b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	/* JSL */
	{ PCI_VDEVICE(INTEL, 0x4da8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4da9), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4daa), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4dab), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x4dc5), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4dc6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4dc7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x4de8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4de9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4dea), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4deb), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x4dfb), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* ADL-P */
	{ PCI_VDEVICE(INTEL, 0x51a8), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x51a9), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x51aa), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x51ab), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x51c5), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51c6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51c7), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x51d8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51d9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51e8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51e9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51ea), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51eb), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x51fb), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	/* ADL-M */
	{ PCI_VDEVICE(INTEL, 0x54a8), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x54a9), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x54aa), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x54ab), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x54c5), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x54c6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x54c7), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x54e8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x54e9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x54ea), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x54eb), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x54fb), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	/* APL */
	{ PCI_VDEVICE(INTEL, 0x5aac), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5aae), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5ab0), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5ab2), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5ab4), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5ab6), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5ab8), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5aba), .driver_data = (kernel_ulong_t)&apl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x5abc), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x5abe), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x5ac0), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x5ac2), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x5ac4), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x5ac6), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x5aee), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* NVL-S */
	{ PCI_VDEVICE(INTEL, 0x6e28), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x6e29), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x6e2a), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x6e2b), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x6e4c), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x6e4d), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x6e4e), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x6e4f), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x6e5c), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x6e5e), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x6e7a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x6e7b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	/* ARL-H */
	{ PCI_VDEVICE(INTEL, 0x7725), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7726), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7727), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7730), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7746), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7750), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7751), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7752), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7778), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7779), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x777a), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x777b), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	/* RPL-S */
	{ PCI_VDEVICE(INTEL, 0x7a28), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7a29), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7a2a), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7a2b), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7a4c), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7a4d), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7a4e), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7a4f), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7a5c), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7a79), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7a7b), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7a7c), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7a7d), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7a7e), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* ADL-S */
	{ PCI_VDEVICE(INTEL, 0x7aa8), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7aa9), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7aaa), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7aab), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7acc), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7acd), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7ace), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7acf), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7adc), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7af9), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7afb), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7afc), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7afd), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7afe), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	/* MTL-P */
	{ PCI_VDEVICE(INTEL, 0x7e25), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7e26), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7e27), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7e30), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7e46), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7e50), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7e51), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7e52), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7e78), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7e79), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7e7a), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7e7b), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	/* MTP-S */
	{ PCI_VDEVICE(INTEL, 0x7f28), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7f29), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7f2a), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7f2b), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7f4c), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7f4d), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7f4e), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7f4f), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7f5c), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7f5d), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x7f5e), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7f5f), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x7f7a), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x7f7b), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	/* LKF */
	{ PCI_VDEVICE(INTEL, 0x98a8), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x98a9), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x98aa), .driver_data = (kernel_ulong_t)&bxt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x98c5), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x98c6), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x98c7), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x98e8), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x98e9), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x98ea), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x98eb), .driver_data = (kernel_ulong_t)&bxt_i2c_info },
	/* SPT-LP */
	{ PCI_VDEVICE(INTEL, 0x9d27), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9d28), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9d29), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x9d2a), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0x9d60), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9d61), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9d62), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9d63), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9d64), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9d65), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9d66), .driver_data = (kernel_ulong_t)&spt_uart_info },
	/* CNL-LP */
	{ PCI_VDEVICE(INTEL, 0x9da8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9da9), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9daa), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x9dab), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0x9dc5), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9dc6), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9dc7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0x9de8), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9de9), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9dea), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9deb), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0x9dfb), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* TGL-LP */
	{ PCI_VDEVICE(INTEL, 0xa0a8), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0a9), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0aa), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa0ab), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa0c5), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0c6), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0c7), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0d8), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0d9), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0da), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0db), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0dc), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0dd), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa0de), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa0df), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa0e8), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0e9), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0ea), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0eb), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa0fb), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa0fd), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa0fe), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* SPT-H */
	{ PCI_VDEVICE(INTEL, 0xa127), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa128), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa129), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa12a), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa160), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa161), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa162), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa166), .driver_data = (kernel_ulong_t)&spt_uart_info },
	/* KBL-H */
	{ PCI_VDEVICE(INTEL, 0xa2a7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa2a8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa2a9), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa2aa), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa2e0), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa2e1), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa2e2), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa2e3), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa2e6), .driver_data = (kernel_ulong_t)&spt_uart_info },
	/* CNL-H */
	{ PCI_VDEVICE(INTEL, 0xa328), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa329), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa32a), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa32b), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa347), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa368), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa369), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa36a), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa36b), .driver_data = (kernel_ulong_t)&cnl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa37b), .driver_data = (kernel_ulong_t)&cnl_spi_info },
	/* CML-V */
	{ PCI_VDEVICE(INTEL, 0xa3a7), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa3a8), .driver_data = (kernel_ulong_t)&spt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa3a9), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa3aa), .driver_data = (kernel_ulong_t)&spt_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa3e0), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa3e1), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa3e2), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa3e3), .driver_data = (kernel_ulong_t)&spt_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa3e6), .driver_data = (kernel_ulong_t)&spt_uart_info },
	/* LNL-M */
	{ PCI_VDEVICE(INTEL, 0xa825), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa826), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa827), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa830), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa846), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xa850), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa851), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa852), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xa878), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa879), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa87a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xa87b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	/* NVL-H */
	{ PCI_VDEVICE(INTEL, 0xd325), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xd326), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xd327), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xd330), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xd347), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xd350), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xd351), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xd352), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xd378), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xd379), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xd37a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xd37b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	/* PTL-H */
	{ PCI_VDEVICE(INTEL, 0xe325), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xe326), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xe327), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xe330), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xe346), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xe350), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe351), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe352), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xe378), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe379), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe37a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe37b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	/* PTL-P */
	{ PCI_VDEVICE(INTEL, 0xe425), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xe426), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xe427), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xe430), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xe446), .driver_data = (kernel_ulong_t)&tgl_spi_info },
	{ PCI_VDEVICE(INTEL, 0xe450), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe451), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe452), .driver_data = (kernel_ulong_t)&bxt_uart_info },
	{ PCI_VDEVICE(INTEL, 0xe478), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe479), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe47a), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ PCI_VDEVICE(INTEL, 0xe47b), .driver_data = (kernel_ulong_t)&ehl_i2c_info },
	{ }
};
MODULE_DEVICE_TABLE(pci, intel_lpss_pci_ids);

static struct pci_driver intel_lpss_pci_driver = {
	.name = "intel-lpss",
	.id_table = intel_lpss_pci_ids,
	.probe = intel_lpss_pci_probe,
	.remove = intel_lpss_pci_remove,
	.driver = {
		.pm = pm_ptr(&intel_lpss_pm_ops),
	},
};

module_pci_driver(intel_lpss_pci_driver);

MODULE_AUTHOR("Andy Shevchenko <andriy.shevchenko@linux.intel.com>");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_DESCRIPTION("Intel LPSS PCI driver");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS("INTEL_LPSS");
