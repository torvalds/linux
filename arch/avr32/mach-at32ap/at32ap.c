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

#include <asm/io.h>

#include <asm/arch/init.h>
#include <asm/arch/sm.h>

struct at32_sm system_manager;

static int __init at32_sm_init(void)
{
	struct resource *regs;
	struct at32_sm *sm = &system_manager;
	int ret = -ENXIO;

	regs = platform_get_resource(&at32_sm_device, IORESOURCE_MEM, 0);
	if (!regs)
		goto fail;

	spin_lock_init(&sm->lock);
	sm->pdev = &at32_sm_device;

	ret = -ENOMEM;
	sm->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!sm->regs)
		goto fail;

	return 0;

fail:
	printk(KERN_ERR "Failed to initialize System Manager: %d\n", ret);
	return ret;
}

void __init setup_platform(void)
{
	at32_sm_init();
	at32_clock_init();
	at32_portmux_init();
}

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
	.probe		= pdc_probe,
	.driver		= {
		.name	= "pdc",
	},
};

static int __init pdc_init(void)
{
	return platform_driver_register(&pdc_driver);
}
arch_initcall(pdc_init);
