// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 Broadcom
 */

#define pr_fmt(fmt)	KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/random.h>
#include <linux/hw_random.h>

#define HOST_REV_ID		0x00
#define HOST_FIFO_DEPTH		0x04
#define HOST_FIFO_COUNT		0x08
#define HOST_FIFO_THRESHOLD	0x0c
#define HOST_FIFO_DATA		0x10

#define HOST_FIFO_COUNT_MASK		0xffff

/* Delay range in microseconds */
#define FIFO_DELAY_MIN_US		3
#define FIFO_DELAY_MAX_US		7
#define FIFO_DELAY_MAX_COUNT		10

struct bcm74110_priv {
	void __iomem *base;
};

static inline int bcm74110_rng_fifo_count(void __iomem *mem)
{
	return readl_relaxed(mem) & HOST_FIFO_COUNT_MASK;
}

static int bcm74110_rng_read(struct hwrng *rng, void *buf, size_t max,
			bool wait)
{
	struct bcm74110_priv *priv = (struct bcm74110_priv *)rng->priv;
	void __iomem *fc_addr = priv->base + HOST_FIFO_COUNT;
	void __iomem *fd_addr = priv->base + HOST_FIFO_DATA;
	unsigned underrun_count = 0;
	u32 max_words = max / sizeof(u32);
	u32 num_words;
	unsigned i;

	/*
	 * We need to check how many words are available in the RNG FIFO. If
	 * there aren't any, we need to wait for some to become available.
	 */
	while ((num_words = bcm74110_rng_fifo_count(fc_addr)) == 0) {
		if (!wait)
			return 0;
		/*
		 * As a precaution, limit how long we wait. If the FIFO doesn't
		 * refill within the allotted time, return 0 (=no data) to the
		 * caller.
		 */
		if (likely(underrun_count < FIFO_DELAY_MAX_COUNT))
			usleep_range(FIFO_DELAY_MIN_US, FIFO_DELAY_MAX_US);
		else
			return 0;
		underrun_count++;
	}
	if (num_words > max_words)
		num_words = max_words;

	/* Bail early if we run out of random numbers unexpectedly */
	for (i = 0; i < num_words && bcm74110_rng_fifo_count(fc_addr) > 0; i++)
		((u32 *)buf)[i] = readl_relaxed(fd_addr);

	return i * sizeof(u32);
}

static struct hwrng bcm74110_hwrng = {
	.read = bcm74110_rng_read,
};

static int bcm74110_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct bcm74110_priv *priv;
	int rc;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	bcm74110_hwrng.name = pdev->name;
	bcm74110_hwrng.priv = (unsigned long)priv;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base))
		return PTR_ERR(priv->base);

	rc = devm_hwrng_register(dev, &bcm74110_hwrng);
	if (rc)
		dev_err(dev, "hwrng registration failed (%d)\n", rc);
	else
		dev_info(dev, "hwrng registered\n");

	return rc;
}

static const struct of_device_id bcm74110_rng_match[] = {
	{ .compatible	= "brcm,bcm74110-rng", },
	{},
};
MODULE_DEVICE_TABLE(of, bcm74110_rng_match);

static struct platform_driver bcm74110_rng_driver = {
	.driver = {
		.name = KBUILD_MODNAME,
		.of_match_table = bcm74110_rng_match,
	},
	.probe	= bcm74110_rng_probe,
};
module_platform_driver(bcm74110_rng_driver);

MODULE_AUTHOR("Markus Mayer <mmayer@broadcom.com>");
MODULE_DESCRIPTION("BCM 74110 Random Number Generator (RNG) driver");
MODULE_LICENSE("GPL v2");
