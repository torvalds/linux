/*
 * Intel Lynxpoint LPSS clocks.
 *
 * Copyright (C) 2013, Intel Corporation
 * Authors: Mika Westerberg <mika.westerberg@linux.intel.com>
 *	    Heikki Krogerus <heikki.krogerus@linux.intel.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "clk-lpss.h"

#define PRV_CLOCK_PARAMS 0x800

static int lpt_clk_probe(struct platform_device *pdev)
{
	struct clk *clk;

	/* LPSS free running clock */
	clk = clk_register_fixed_rate(&pdev->dev, "lpss_clk", NULL, CLK_IS_ROOT,
				      100000000);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	/* Shared DMA clock */
	clk_register_clkdev(clk, "hclk", "INTL9C60.0.auto");

	/* SPI clocks */
	clk = clk_register_lpss_gate("spi0_clk", "lpss_clk", "INT33C0", NULL,
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, NULL, "INT33C0:00");

	clk = clk_register_lpss_gate("spi1_clk", "lpss_clk", "INT33C1", NULL,
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, NULL, "INT33C1:00");

	/* I2C clocks */
	clk = clk_register_lpss_gate("i2c0_clk", "lpss_clk", "INT33C2", NULL,
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, NULL, "INT33C2:00");

	clk = clk_register_lpss_gate("i2c1_clk", "lpss_clk", "INT33C3", NULL,
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, NULL, "INT33C3:00");

	/* UART clocks */
	clk = clk_register_lpss_gate("uart0_clk", "lpss_clk", "INT33C4", NULL,
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, NULL, "INT33C4:00");

	clk = clk_register_lpss_gate("uart1_clk", "lpss_clk", "INT33C5", NULL,
				     PRV_CLOCK_PARAMS);
	if (!IS_ERR(clk))
		clk_register_clkdev(clk, NULL, "INT33C5:00");

	return 0;
}

static struct platform_driver lpt_clk_driver = {
	.driver = {
		.name = "clk-lpt",
		.owner = THIS_MODULE,
	},
	.probe = lpt_clk_probe,
};

static int __init lpt_clk_init(void)
{
	return platform_driver_register(&lpt_clk_driver);
}
arch_initcall(lpt_clk_init);
