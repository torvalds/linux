/*
 * Copyright 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#include <linux/device.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include "spi-bcm-qspi.h"

static const struct of_device_id brcmstb_qspi_of_match[] = {
	{ .compatible = "brcm,spi-brcmstb-qspi" },
	{ .compatible = "brcm,spi-brcmstb-mspi" },
	{},
};
MODULE_DEVICE_TABLE(of, brcmstb_qspi_of_match);

static int brcmstb_qspi_probe(struct platform_device *pdev)
{
	return bcm_qspi_probe(pdev, NULL);
}

static int brcmstb_qspi_remove(struct platform_device *pdev)
{
	return bcm_qspi_remove(pdev);
}

static struct platform_driver brcmstb_qspi_driver = {
	.probe			= brcmstb_qspi_probe,
	.remove			= brcmstb_qspi_remove,
	.driver = {
		.name		= "brcmstb_qspi",
		.pm		= &bcm_qspi_pm_ops,
		.of_match_table = brcmstb_qspi_of_match,
	}
};
module_platform_driver(brcmstb_qspi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Kamal Dasu");
MODULE_DESCRIPTION("Broadcom SPI driver for settop SoC");
