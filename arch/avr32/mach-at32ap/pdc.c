/*
 * Copyright (C) 2006 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/platform_device.h>

static int __init pdc_probe(struct platform_device *pdev)
{
	struct clk *pclk, *hclk;

	pclk = clk_get(&pdev->dev, "pclk");
	if (IS_ERR(pclk)) {
		dev_err(&pdev->dev, "no pclk defined\n");
		return PTR_ERR(pclk);
	}
	hclk = clk_get(&pdev->dev, "hclk");
	if (IS_ERR(hclk)) {
		dev_err(&pdev->dev, "no hclk defined\n");
		clk_put(pclk);
		return PTR_ERR(hclk);
	}

	clk_enable(pclk);
	clk_enable(hclk);

	dev_info(&pdev->dev, "Atmel Peripheral DMA Controller enabled\n");
	return 0;
}

static struct platform_driver pdc_driver = {
	.driver		= {
		.name	= "pdc",
	},
};

static int __init pdc_init(void)
{
	return platform_driver_probe(&pdc_driver, pdc_probe);
}
arch_initcall(pdc_init);
