/**
 * Copyright (c) 2010-2012 Broadcom. All rights reserved.
 * Copyright (c) 2013 Lubomir Rintel
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License ("GPL")
 * version 2, as published by the Free Software Foundation.
 */

#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/printk.h>

#define RNG_CTRL	0x0
#define RNG_STATUS	0x4
#define RNG_DATA	0x8

/* enable rng */
#define RNG_RBGEN	0x1

/* the initial numbers generated are "less random" so will be discarded */
#define RNG_WARMUP_COUNT 0x40000

static int bcm2835_rng_read(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	void __iomem *rng_base = (void __iomem *)rng->priv;

	while ((__raw_readl(rng_base + RNG_STATUS) >> 24) == 0) {
		if (!wait)
			return 0;
		cpu_relax();
	}

	*(u32 *)buf = __raw_readl(rng_base + RNG_DATA);
	return sizeof(u32);
}

static struct hwrng bcm2835_rng_ops = {
	.name	= "bcm2835",
	.read	= bcm2835_rng_read,
};

static int bcm2835_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void __iomem *rng_base;
	int err;

	/* map peripheral */
	rng_base = of_iomap(np, 0);
	if (!rng_base) {
		dev_err(dev, "failed to remap rng regs");
		return -ENODEV;
	}
	bcm2835_rng_ops.priv = (unsigned long)rng_base;

	/* set warm-up count & enable */
	__raw_writel(RNG_WARMUP_COUNT, rng_base + RNG_STATUS);
	__raw_writel(RNG_RBGEN, rng_base + RNG_CTRL);

	/* register driver */
	err = hwrng_register(&bcm2835_rng_ops);
	if (err) {
		dev_err(dev, "hwrng registration failed\n");
		iounmap(rng_base);
	} else
		dev_info(dev, "hwrng registered\n");

	return err;
}

static int bcm2835_rng_remove(struct platform_device *pdev)
{
	void __iomem *rng_base = (void __iomem *)bcm2835_rng_ops.priv;

	/* disable rng hardware */
	__raw_writel(0, rng_base + RNG_CTRL);

	/* unregister driver */
	hwrng_unregister(&bcm2835_rng_ops);
	iounmap(rng_base);

	return 0;
}

static const struct of_device_id bcm2835_rng_of_match[] = {
	{ .compatible = "brcm,bcm2835-rng", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm2835_rng_of_match);

static struct platform_driver bcm2835_rng_driver = {
	.driver = {
		.name = "bcm2835-rng",
		.owner = THIS_MODULE,
		.of_match_table = bcm2835_rng_of_match,
	},
	.probe		= bcm2835_rng_probe,
	.remove		= bcm2835_rng_remove,
};
module_platform_driver(bcm2835_rng_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("BCM2835 Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL v2");
