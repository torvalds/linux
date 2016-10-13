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
#define RNG_INT_MASK	0x10

/* enable rng */
#define RNG_RBGEN	0x1

/* the initial numbers generated are "less random" so will be discarded */
#define RNG_WARMUP_COUNT 0x40000

#define RNG_INT_OFF	0x1

static void __init nsp_rng_init(void __iomem *base)
{
	u32 val;

	/* mask the interrupt */
	val = readl(base + RNG_INT_MASK);
	val |= RNG_INT_OFF;
	writel(val, base + RNG_INT_MASK);
}

static int bcm2835_rng_read(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	void __iomem *rng_base = (void __iomem *)rng->priv;
	u32 max_words = max / sizeof(u32);
	u32 num_words, count;

	while ((__raw_readl(rng_base + RNG_STATUS) >> 24) == 0) {
		if (!wait)
			return 0;
		cpu_relax();
	}

	num_words = readl(rng_base + RNG_STATUS) >> 24;
	if (num_words > max_words)
		num_words = max_words;

	for (count = 0; count < num_words; count++)
		((u32 *)buf)[count] = readl(rng_base + RNG_DATA);

	return num_words * sizeof(u32);
}

static struct hwrng bcm2835_rng_ops = {
	.name	= "bcm2835",
	.read	= bcm2835_rng_read,
};

static const struct of_device_id bcm2835_rng_of_match[] = {
	{ .compatible = "brcm,bcm2835-rng"},
	{ .compatible = "brcm,bcm-nsp-rng", .data = nsp_rng_init},
	{ .compatible = "brcm,bcm5301x-rng", .data = nsp_rng_init},
	{},
};

static int bcm2835_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	void (*rng_setup)(void __iomem *base);
	const struct of_device_id *rng_id;
	void __iomem *rng_base;
	int err;

	/* map peripheral */
	rng_base = of_iomap(np, 0);
	if (!rng_base) {
		dev_err(dev, "failed to remap rng regs");
		return -ENODEV;
	}
	bcm2835_rng_ops.priv = (unsigned long)rng_base;

	rng_id = of_match_node(bcm2835_rng_of_match, np);
	if (!rng_id)
		return -EINVAL;

	/* Check for rng init function, execute it */
	rng_setup = rng_id->data;
	if (rng_setup)
		rng_setup(rng_base);

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

MODULE_DEVICE_TABLE(of, bcm2835_rng_of_match);

static struct platform_driver bcm2835_rng_driver = {
	.driver = {
		.name = "bcm2835-rng",
		.of_match_table = bcm2835_rng_of_match,
	},
	.probe		= bcm2835_rng_probe,
	.remove		= bcm2835_rng_remove,
};
module_platform_driver(bcm2835_rng_driver);

MODULE_AUTHOR("Lubomir Rintel <lkundrak@v3.sk>");
MODULE_DESCRIPTION("BCM2835 Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL v2");
