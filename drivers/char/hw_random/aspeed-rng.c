// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) ASPEED Technology Inc.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>

#define TRNG_CTL	0x00
#define TRNG_EN		0x0
#define TRNG_MODE	0x04
#define TRNG_RDY	0x1f
#define TRNG_ODATA	0x04

struct aspeed_trng {
	u32 ver;
	void __iomem *base;
	struct hwrng rng;
	unsigned int present: 1;
	ktime_t period;
	struct hrtimer timer;
	struct completion completion;
};

static int aspeed_trng_read(struct hwrng *rng, void *buf, size_t max,
			    bool wait)
{
	struct aspeed_trng *priv = container_of(rng, struct aspeed_trng, rng);
	u32 *data = buf;
	size_t read = 0;
	int timeout = max / 4 + 1;

	while (read < max) {
		if (!(readl(priv->base + TRNG_CTL) & (1 << TRNG_RDY))) {
			if (wait) {
				if (timeout-- == 0)
					return read;
			} else {
				return 0;
			}
		} else {
			*data = readl(priv->base + TRNG_ODATA);
			data++;
			read += 4;
		}
	}

	return read;
}

static void aspeed_trng_enable(struct aspeed_trng *priv)
{
	u32 ctl;

	ctl = readl(priv->base + TRNG_CTL);
	ctl = ctl & ~(1 << TRNG_EN); /* enable rng */
	ctl = ctl | (3 << TRNG_MODE); /* select mode */

	writel(ctl, priv->base + TRNG_CTL);
}

static void aspeed_trng_disable(struct aspeed_trng *priv)
{
	writel(1, priv->base + TRNG_CTL);
}

static int aspeed_trng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_trng *priv;
	struct resource *res;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	priv->rng.name = pdev->name;
	priv->rng.quality = 900;
	priv->rng.read = aspeed_trng_read;

	aspeed_trng_enable(priv);

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret)
		return ret;

	platform_set_drvdata(pdev, priv);

	dev_info(dev, "Aspeed Hardware RNG successfully registered\n");

	return 0;
}

static int aspeed_trng_remove(struct platform_device *pdev)
{
	struct aspeed_trng *priv = platform_get_drvdata(pdev);

	aspeed_trng_disable(priv);

	return 0;
}

static const struct of_device_id aspeed_trng_dt_ids[] = {
	{ .compatible = "aspeed,ast2600-trng" },
	{ .compatible = "aspeed,ast2700-trng" },
	{}
};
MODULE_DEVICE_TABLE(of, aspeed_trng_dt_ids);

static struct platform_driver aspeed_trng_driver = {
	.probe		= aspeed_trng_probe,
	.remove		= aspeed_trng_remove,
	.driver		= {
		.name	= "aspeed-trng",
		.of_match_table = aspeed_trng_dt_ids,
	},
};

module_platform_driver(aspeed_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Neal Liu <neal_liu@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed true random number generator driver");
