/*
 * Atmel SSC driver
 *
 * Copyright (C) 2007 Atmel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/platform_device.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/spinlock.h>
#include <linux/atmel-ssc.h>
#include <linux/slab.h>
#include <linux/module.h>

/* Serialize access to ssc_list and user count */
static DEFINE_SPINLOCK(user_lock);
static LIST_HEAD(ssc_list);

struct ssc_device *ssc_request(unsigned int ssc_num)
{
	int ssc_valid = 0;
	struct ssc_device *ssc;

	spin_lock(&user_lock);
	list_for_each_entry(ssc, &ssc_list, list) {
		if (ssc->pdev->id == ssc_num) {
			ssc_valid = 1;
			break;
		}
	}

	if (!ssc_valid) {
		spin_unlock(&user_lock);
		pr_err("ssc: ssc%d platform device is missing\n", ssc_num);
		return ERR_PTR(-ENODEV);
	}

	if (ssc->user) {
		spin_unlock(&user_lock);
		dev_dbg(&ssc->pdev->dev, "module busy\n");
		return ERR_PTR(-EBUSY);
	}
	ssc->user++;
	spin_unlock(&user_lock);

	clk_enable(ssc->clk);

	return ssc;
}
EXPORT_SYMBOL(ssc_request);

void ssc_free(struct ssc_device *ssc)
{
	spin_lock(&user_lock);
	if (ssc->user) {
		ssc->user--;
		clk_disable(ssc->clk);
	} else {
		dev_dbg(&ssc->pdev->dev, "device already free\n");
	}
	spin_unlock(&user_lock);
}
EXPORT_SYMBOL(ssc_free);

static int __init ssc_probe(struct platform_device *pdev)
{
	struct resource *regs;
	struct ssc_device *ssc;

	ssc = devm_kzalloc(&pdev->dev, sizeof(struct ssc_device), GFP_KERNEL);
	if (!ssc) {
		dev_dbg(&pdev->dev, "out of memory\n");
		return -ENOMEM;
	}

	ssc->pdev = pdev;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_dbg(&pdev->dev, "no mmio resource defined\n");
		return -ENXIO;
	}

	ssc->regs = devm_request_and_ioremap(&pdev->dev, regs);
	if (!ssc->regs) {
		dev_dbg(&pdev->dev, "ioremap failed\n");
		return -EINVAL;
	}

	ssc->clk = devm_clk_get(&pdev->dev, "pclk");
	if (IS_ERR(ssc->clk)) {
		dev_dbg(&pdev->dev, "no pclk clock defined\n");
		return -ENXIO;
	}

	/* disable all interrupts */
	clk_enable(ssc->clk);
	ssc_writel(ssc->regs, IDR, ~0UL);
	ssc_readl(ssc->regs, SR);
	clk_disable(ssc->clk);

	ssc->irq = platform_get_irq(pdev, 0);
	if (!ssc->irq) {
		dev_dbg(&pdev->dev, "could not get irq\n");
		return -ENXIO;
	}

	spin_lock(&user_lock);
	list_add_tail(&ssc->list, &ssc_list);
	spin_unlock(&user_lock);

	platform_set_drvdata(pdev, ssc);

	dev_info(&pdev->dev, "Atmel SSC device at 0x%p (irq %d)\n",
			ssc->regs, ssc->irq);

	return 0;
}

static int __devexit ssc_remove(struct platform_device *pdev)
{
	struct ssc_device *ssc = platform_get_drvdata(pdev);

	spin_lock(&user_lock);
	list_del(&ssc->list);
	spin_unlock(&user_lock);

	return 0;
}

static struct platform_driver ssc_driver = {
	.remove		= __devexit_p(ssc_remove),
	.driver		= {
		.name		= "ssc",
		.owner		= THIS_MODULE,
	},
};

static int __init ssc_init(void)
{
	return platform_driver_probe(&ssc_driver, ssc_probe);
}
module_init(ssc_init);

static void __exit ssc_exit(void)
{
	platform_driver_unregister(&ssc_driver);
}
module_exit(ssc_exit);

MODULE_AUTHOR("Hans-Christian Egtvedt <hcegtvedt@atmel.com>");
MODULE_DESCRIPTION("SSC driver for Atmel AVR32 and AT91");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:ssc");
