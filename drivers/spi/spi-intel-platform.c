// SPDX-License-Identifier: GPL-2.0-only
/*
 * Intel PCH/PCU SPI flash platform driver.
 *
 * Copyright (C) 2016 - 2022, Intel Corporation
 * Author: Mika Westerberg <mika.westerberg@linux.intel.com>
 */

#include <linux/module.h>
#include <linux/platform_device.h>

#include "spi-intel.h"

static int intel_spi_platform_probe(struct platform_device *pdev)
{
	struct intel_spi_boardinfo *info;
	struct resource *mem;

	info = dev_get_platdata(&pdev->dev);
	if (!info)
		return -EINVAL;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	return intel_spi_probe(&pdev->dev, mem, info);
}

static struct platform_driver intel_spi_platform_driver = {
	.probe = intel_spi_platform_probe,
	.driver = {
		.name = "intel-spi",
		.dev_groups = intel_spi_groups,
	},
};

module_platform_driver(intel_spi_platform_driver);

MODULE_DESCRIPTION("Intel PCH/PCU SPI flash platform driver");
MODULE_AUTHOR("Mika Westerberg <mika.westerberg@linux.intel.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:intel-spi");
