/*
 * omap-rng.c - RNG driver for TI OMAP CPU family
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Mostly based on original driver:
 *
 * Copyright (C) 2005 Nokia Corporation
 * Author: Juha Yrjölä <juha.yrjola@nokia.com>
 *
 * This file is licensed under  the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/random.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/hw_random.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>

#include <asm/io.h>

#define RNG_OUT_REG		0x00		/* Output register */
#define RNG_STAT_REG		0x04		/* Status register
							[0] = STAT_BUSY */
#define RNG_ALARM_REG		0x24		/* Alarm register
							[7:0] = ALARM_COUNTER */
#define RNG_CONFIG_REG		0x28		/* Configuration register
							[11:6] = RESET_COUNT
							[5:3]  = RING2_DELAY
							[2:0]  = RING1_DELAY */
#define RNG_REV_REG		0x3c		/* Revision register
							[7:0] = REV_NB */
#define RNG_MASK_REG		0x40		/* Mask and reset register
							[2] = IT_EN
							[1] = SOFTRESET
							[0] = AUTOIDLE */
#define RNG_SYSSTATUS		0x44		/* System status
							[0] = RESETDONE */

/**
 * struct omap_rng_private_data - RNG IP block-specific data
 * @base: virtual address of the beginning of the RNG IP block registers
 * @mem_res: struct resource * for the IP block registers physical memory
 */
struct omap_rng_private_data {
	void __iomem *base;
	struct resource *mem_res;
};

static inline u32 omap_rng_read_reg(struct omap_rng_private_data *priv, int reg)
{
	return __raw_readl(priv->base + reg);
}

static inline void omap_rng_write_reg(struct omap_rng_private_data *priv,
				      int reg, u32 val)
{
	__raw_writel(val, priv->base + reg);
}

static int omap_rng_data_present(struct hwrng *rng, int wait)
{
	struct omap_rng_private_data *priv;
	int data, i;

	priv = (struct omap_rng_private_data *)rng->priv;

	for (i = 0; i < 20; i++) {
		data = omap_rng_read_reg(priv, RNG_STAT_REG) ? 0 : 1;
		if (data || !wait)
			break;
		/* RNG produces data fast enough (2+ MBit/sec, even
		 * during "rngtest" loads, that these delays don't
		 * seem to trigger.  We *could* use the RNG IRQ, but
		 * that'd be higher overhead ... so why bother?
		 */
		udelay(10);
	}
	return data;
}

static int omap_rng_data_read(struct hwrng *rng, u32 *data)
{
	struct omap_rng_private_data *priv;

	priv = (struct omap_rng_private_data *)rng->priv;

	*data = omap_rng_read_reg(priv, RNG_OUT_REG);

	return sizeof(u32);
}

static struct hwrng omap_rng_ops = {
	.name		= "omap",
	.data_present	= omap_rng_data_present,
	.data_read	= omap_rng_data_read,
};

static int __devinit omap_rng_probe(struct platform_device *pdev)
{
	struct omap_rng_private_data *priv;
	int ret;

	priv = kzalloc(sizeof(struct omap_rng_private_data), GFP_KERNEL);
	if (!priv) {
		dev_err(&pdev->dev, "could not allocate memory\n");
		return -ENOMEM;
	};

	omap_rng_ops.priv = (unsigned long)priv;
	dev_set_drvdata(&pdev->dev, priv);

	priv->mem_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!priv->mem_res) {
		ret = -ENOENT;
		goto err_ioremap;
	}

	priv->base = devm_request_and_ioremap(&pdev->dev, priv->mem_res);
	if (!priv->base) {
		ret = -ENOMEM;
		goto err_ioremap;
	}
	dev_set_drvdata(&pdev->dev, priv);

	pm_runtime_enable(&pdev->dev);
	pm_runtime_get_sync(&pdev->dev);

	ret = hwrng_register(&omap_rng_ops);
	if (ret)
		goto err_register;

	dev_info(&pdev->dev, "OMAP Random Number Generator ver. %02x\n",
		 omap_rng_read_reg(priv, RNG_REV_REG));

	omap_rng_write_reg(priv, RNG_MASK_REG, 0x1);

	return 0;

err_register:
	priv->base = NULL;
	pm_runtime_disable(&pdev->dev);
err_ioremap:
	kfree(priv);

	return ret;
}

static int __exit omap_rng_remove(struct platform_device *pdev)
{
	struct omap_rng_private_data *priv = dev_get_drvdata(&pdev->dev);

	hwrng_unregister(&omap_rng_ops);

	omap_rng_write_reg(priv, RNG_MASK_REG, 0x0);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	release_mem_region(priv->mem_res->start, resource_size(priv->mem_res));

	kfree(priv);

	return 0;
}

#ifdef CONFIG_PM_SLEEP

static int omap_rng_suspend(struct device *dev)
{
	struct omap_rng_private_data *priv = dev_get_drvdata(dev);

	omap_rng_write_reg(priv, RNG_MASK_REG, 0x0);
	pm_runtime_put_sync(dev);

	return 0;
}

static int omap_rng_resume(struct device *dev)
{
	struct omap_rng_private_data *priv = dev_get_drvdata(dev);

	pm_runtime_get_sync(dev);
	omap_rng_write_reg(priv, RNG_MASK_REG, 0x1);

	return 0;
}

static SIMPLE_DEV_PM_OPS(omap_rng_pm, omap_rng_suspend, omap_rng_resume);
#define	OMAP_RNG_PM	(&omap_rng_pm)

#else

#define	OMAP_RNG_PM	NULL

#endif

/* work with hotplug and coldplug */
MODULE_ALIAS("platform:omap_rng");

static struct platform_driver omap_rng_driver = {
	.driver = {
		.name		= "omap_rng",
		.owner		= THIS_MODULE,
		.pm		= OMAP_RNG_PM,
	},
	.probe		= omap_rng_probe,
	.remove		= __exit_p(omap_rng_remove),
};

static int __init omap_rng_init(void)
{
	return platform_driver_register(&omap_rng_driver);
}

static void __exit omap_rng_exit(void)
{
	platform_driver_unregister(&omap_rng_driver);
}

module_init(omap_rng_init);
module_exit(omap_rng_exit);

MODULE_AUTHOR("Deepak Saxena (and others)");
MODULE_LICENSE("GPL");
