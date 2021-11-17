// SPDX-License-Identifier: GPL-2.0
/*
 * drivers/char/hw_random/ixp4xx-rng.c
 *
 * RNG driver for Intel IXP4xx family of NPUs
 *
 * Author: Deepak Saxena <dsaxena@plexity.net>
 *
 * Copyright 2005 (c) MontaVista Software, Inc.
 *
 * Fixes by Michael Buesch
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/init.h>
#include <linux/bitops.h>
#include <linux/hw_random.h>
#include <linux/of.h>
#include <linux/soc/ixp4xx/cpu.h>

#include <asm/io.h>

static int ixp4xx_rng_data_read(struct hwrng *rng, u32 *buffer)
{
	void __iomem * rng_base = (void __iomem *)rng->priv;

	*buffer = __raw_readl(rng_base);

	return 4;
}

static struct hwrng ixp4xx_rng_ops = {
	.name		= "ixp4xx",
	.data_read	= ixp4xx_rng_data_read,
};

static int ixp4xx_rng_probe(struct platform_device *pdev)
{
	void __iomem * rng_base;
	struct device *dev = &pdev->dev;
	struct resource *res;

	if (!cpu_is_ixp46x()) /* includes IXP455 */
		return -ENOSYS;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	rng_base = devm_ioremap_resource(dev, res);
	if (IS_ERR(rng_base))
		return PTR_ERR(rng_base);

	ixp4xx_rng_ops.priv = (unsigned long)rng_base;
	return devm_hwrng_register(dev, &ixp4xx_rng_ops);
}

static const struct of_device_id ixp4xx_rng_of_match[] = {
	{
		.compatible = "intel,ixp46x-rng",
	},
	{},
};
MODULE_DEVICE_TABLE(of, ixp4xx_rng_of_match);

static struct platform_driver ixp4xx_rng_driver = {
	.driver = {
		.name = "ixp4xx-hwrandom",
		.of_match_table = ixp4xx_rng_of_match,
	},
	.probe = ixp4xx_rng_probe,
};
module_platform_driver(ixp4xx_rng_driver);

MODULE_AUTHOR("Deepak Saxena <dsaxena@plexity.net>");
MODULE_DESCRIPTION("H/W Pseudo-Random Number Generator (RNG) driver for IXP45x/46x");
MODULE_LICENSE("GPL");
