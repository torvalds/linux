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
#include <linux/mfd/syscon.h>

#define TRNG_CTL	0x00
#define   TRNG_EN	0x0
#define   TRNG_MODE	0x04
#define   TRNG_RDY	0x1f
#define TRNG_ODATA	0x04
#define TRNG_A1		0
#define TRNG_A2		1

#define ASPEED_REVISION_ID0	0x04
#define ASPEED_REVISION_ID1	0x14
#define ID0_AST2600A0		0x05000303
#define ID1_AST2600A0		0x05000303
#define ID0_AST2600A1		0x05010303
#define ID1_AST2600A1		0x05010303
#define ID0_AST2600A2		0x05010303
#define ID1_AST2600A2		0x05020303
#define ID0_AST2600A3		0x05030303
#define ID1_AST2600A3		0x05030303
#define ID0_AST2620A1		0x05010203
#define ID1_AST2620A1		0x05010203
#define ID0_AST2620A2		0x05010203
#define ID1_AST2620A2		0x05020203
#define ID0_AST2620A3		0x05030203
#define ID1_AST2620A3		0x05030203
#define ID0_AST2605A2		0x05010103
#define ID1_AST2605A2		0x05020103
#define ID0_AST2605A3		0x05030103
#define ID1_AST2605A3		0x05030103
#define ID0_AST2625A3		0x05030403
#define ID1_AST2625A3		0x05030403

struct aspeed_trng {
	u32 ver;
	void __iomem *base;
	struct hwrng rng;
	unsigned int present: 1;
	ktime_t period;
	struct hrtimer timer;
	struct completion completion;
};

static int aspeed_trng_read_A1(struct hwrng *rng, void *buf, size_t max,
			       bool wait)
{
	struct aspeed_trng *priv = container_of(rng, struct aspeed_trng, rng);
	int retval = 0;
	int period_us = ktime_to_us(priv->period);

	if (!wait && !priv->present)
		return 0;

	wait_for_completion(&priv->completion);

	do {
		if (retval > 0)
			usleep_range(period_us,
				     period_us + min(1, period_us / 100));

		*(u32 *)buf = readl(priv->base + TRNG_ODATA);
		retval += sizeof(u32);
		buf += sizeof(u32);
		max -= sizeof(u32);
	} while (wait && max > sizeof(u32));

	priv->present = 0;
	reinit_completion(&priv->completion);
	hrtimer_forward_now(&priv->timer, priv->period);
	hrtimer_restart(&priv->timer);

	return retval;
}

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
	if (priv->ver == TRNG_A2)
		ctl = ctl | (3 << TRNG_MODE); /* select mode */
	if (priv->ver == TRNG_A1)
		ctl = ctl | 2; /* select mode */

	writel(ctl, priv->base + TRNG_CTL);
}

static void aspeed_trng_disable(struct aspeed_trng *priv)
{
	writel(1, priv->base + TRNG_CTL);
}

static enum hrtimer_restart aspeed_trng_trigger(struct hrtimer *timer)
{
	struct aspeed_trng *priv = container_of(timer, struct aspeed_trng, timer);

	priv->present = 1;
	complete(&priv->completion);

	return HRTIMER_NORESTART;
}

static uint32_t chip_version(u32 revid0, u32 revid1)
{
	if (revid0 == ID0_AST2600A0 && revid1 == ID1_AST2600A0) {
		/* AST2600-A0 */
		return TRNG_A1;
	} else if (revid0 == ID0_AST2600A1 && revid1 == ID1_AST2600A1) {
		/* AST2600-A1 */
		return TRNG_A1;
	} else if (revid0 == ID0_AST2620A1 && revid1 == ID1_AST2620A1) {
		/* AST2620-A1 */
		return TRNG_A1;
	}
	return TRNG_A2;
}

static int aspeed_trng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_trng *priv;
	struct resource *res;
	struct regmap *scu;
	u32 revid0, revid1;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	priv->base = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	scu = syscon_regmap_lookup_by_phandle(dev->of_node, "aspeed,scu");
	if (IS_ERR(scu)) {
		dev_err(dev, "failed to find 2600 SCU regmap\n");
		return PTR_ERR(scu);
	}

	regmap_read(scu, ASPEED_REVISION_ID0, &revid0);
	regmap_read(scu, ASPEED_REVISION_ID1, &revid1);

	priv->ver = chip_version(revid0, revid1);

	priv->rng.name = pdev->name;
	priv->rng.quality = 900;
	if (priv->ver == TRNG_A2) {
		priv->rng.read = aspeed_trng_read;
		priv->base += 0x10;
	} else {
		priv->period = ns_to_ktime(1 * NSEC_PER_USEC);
		init_completion(&priv->completion);
		hrtimer_init(&priv->timer, CLOCK_MONOTONIC, HRTIMER_MODE_ABS);
		priv->timer.function = aspeed_trng_trigger;
		priv->rng.read = aspeed_trng_read_A1;
		priv->present = 1;
		complete(&priv->completion);
	}
	aspeed_trng_enable(priv);

	ret = devm_hwrng_register(&pdev->dev, &priv->rng);
	if (ret)
		goto err_register;

	platform_set_drvdata(pdev, priv);

	return 0;

err_register:
	return ret;
}

static int aspeed_trng_remove(struct platform_device *pdev)
{
	struct aspeed_trng *priv = platform_get_drvdata(pdev);

	aspeed_trng_disable(priv);

	return 0;
}

static const struct of_device_id aspeed_trng_dt_ids[] = {
	{ .compatible = "aspeed,ast2600-trng" },
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
MODULE_AUTHOR("Johnny Huang <johnny_huang@aspeedtech.com>");
MODULE_DESCRIPTION("Aspeed true random number generator driver");
