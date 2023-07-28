// SPDX-License-Identifier: GPL-2.0-only
/*
 * PIC32 RNG driver
 *
 * Joshua Henderson <joshua.henderson@microchip.com>
 * Copyright (C) 2016 Microchip Technology Inc.  All rights reserved.
 */

#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#define RNGCON		0x04
#define TRNGEN		BIT(8)
#define TRNGMOD		BIT(11)
#define RNGSEED1	0x18
#define RNGSEED2	0x1C
#define RNGRCNT		0x20
#define RCNT_MASK	0x7F

struct pic32_rng {
	void __iomem	*base;
	struct hwrng	rng;
};

/*
 * The TRNG can generate up to 24Mbps. This is a timeout that should be safe
 * enough given the instructions in the loop and that the TRNG may not always
 * be at maximum rate.
 */
#define RNG_TIMEOUT 500

static int pic32_rng_init(struct hwrng *rng)
{
	struct pic32_rng *priv = container_of(rng, struct pic32_rng, rng);

	/* enable TRNG in enhanced mode */
	writel(TRNGEN | TRNGMOD, priv->base + RNGCON);
	return 0;
}

static int pic32_rng_read(struct hwrng *rng, void *buf, size_t max,
			  bool wait)
{
	struct pic32_rng *priv = container_of(rng, struct pic32_rng, rng);
	u64 *data = buf;
	u32 t;
	unsigned int timeout = RNG_TIMEOUT;

	do {
		t = readl(priv->base + RNGRCNT) & RCNT_MASK;
		if (t == 64) {
			/* TRNG value comes through the seed registers */
			*data = ((u64)readl(priv->base + RNGSEED2) << 32) +
				readl(priv->base + RNGSEED1);
			return 8;
		}
	} while (wait && --timeout);

	return -EIO;
}

static void pic32_rng_cleanup(struct hwrng *rng)
{
	struct pic32_rng *priv = container_of(rng, struct pic32_rng, rng);

	writel(0, priv->base + RNGCON);
}

static int pic32_rng_probe(struct platform_device *pdev)
{
	struct pic32_rng *priv;
	struct clk *clk;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	clk = devm_clk_get_enabled(&pdev->dev, NULL);
	if (IS_ERR(clk))
		return PTR_ERR(clk);

	priv->rng.name = pdev->name;
	priv->rng.init = pic32_rng_init;
	priv->rng.read = pic32_rng_read;
	priv->rng.cleanup = pic32_rng_cleanup;

	return devm_hwrng_register(&pdev->dev, &priv->rng);
}

static const struct of_device_id pic32_rng_of_match[] __maybe_unused = {
	{ .compatible	= "microchip,pic32mzda-rng", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, pic32_rng_of_match);

static struct platform_driver pic32_rng_driver = {
	.probe		= pic32_rng_probe,
	.driver		= {
		.name	= "pic32-rng",
		.of_match_table = pic32_rng_of_match,
	},
};

module_platform_driver(pic32_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Joshua Henderson <joshua.henderson@microchip.com>");
MODULE_DESCRIPTION("Microchip PIC32 RNG Driver");
