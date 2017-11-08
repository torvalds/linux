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

struct bcm2835_rng_priv {
	struct hwrng rng;
	void __iomem *base;
};

static void __init nsp_rng_init(void __iomem *base)
{
	u32 val;

	/* mask the interrupt */
	val = readl(base + RNG_INT_MASK);
	val |= RNG_INT_OFF;
	writel(val, base + RNG_INT_MASK);
}

static inline struct bcm2835_rng_priv *to_rng_priv(struct hwrng *rng)
{
	return container_of(rng, struct bcm2835_rng_priv, rng);
}

static int bcm2835_rng_read(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	struct bcm2835_rng_priv *priv = to_rng_priv(rng);
	u32 max_words = max / sizeof(u32);
	u32 num_words, count;

	while ((__raw_readl(priv->base + RNG_STATUS) >> 24) == 0) {
		if (!wait)
			return 0;
		cpu_relax();
	}

	num_words = readl(priv->base + RNG_STATUS) >> 24;
	if (num_words > max_words)
		num_words = max_words;

	for (count = 0; count < num_words; count++)
		((u32 *)buf)[count] = readl(priv->base + RNG_DATA);

	return num_words * sizeof(u32);
}

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
	struct bcm2835_rng_priv *priv;
	struct resource *r;
	int err;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	platform_set_drvdata(pdev, priv);

	r = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	/* map peripheral */
	priv->base = devm_ioremap_resource(dev, r);
	if (IS_ERR(priv->base)) {
		dev_err(dev, "failed to remap rng regs");
		return PTR_ERR(priv->base);
	}

	priv->rng.name = "bcm2835-rng";
	priv->rng.read = bcm2835_rng_read;

	rng_id = of_match_node(bcm2835_rng_of_match, np);
	if (!rng_id)
		return -EINVAL;

	/* Check for rng init function, execute it */
	rng_setup = rng_id->data;
	if (rng_setup)
		rng_setup(priv->base);

	/* set warm-up count & enable */
	__raw_writel(RNG_WARMUP_COUNT, priv->base + RNG_STATUS);
	__raw_writel(RNG_RBGEN, priv->base + RNG_CTRL);

	/* register driver */
	err = hwrng_register(&priv->rng);
	if (err)
		dev_err(dev, "hwrng registration failed\n");
	else
		dev_info(dev, "hwrng registered\n");

	return err;
}

static int bcm2835_rng_remove(struct platform_device *pdev)
{
	struct bcm2835_rng_priv *priv = platform_get_drvdata(pdev);

	/* disable rng hardware */
	__raw_writel(0, priv->base + RNG_CTRL);

	/* unregister driver */
	hwrng_unregister(&priv->rng);

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
