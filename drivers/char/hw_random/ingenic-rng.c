// SPDX-License-Identifier: GPL-2.0
/*
 * Ingenic Random Number Generator driver
 * Copyright (c) 2017 PrasannaKumar Muralidharan <prasannatsmkumar@gmail.com>
 * Copyright (c) 2020 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* RNG register offsets */
#define RNG_REG_ERNG_OFFSET		0x0
#define RNG_REG_RNG_OFFSET		0x4

/* bits within the ERND register */
#define ERNG_READY				BIT(31)
#define ERNG_ENABLE				BIT(0)

enum ingenic_rng_version {
	ID_JZ4780,
	ID_X1000,
};

/* Device associated memory */
struct ingenic_rng {
	enum ingenic_rng_version version;

	void __iomem *base;
	struct hwrng rng;
};

static int ingenic_rng_init(struct hwrng *rng)
{
	struct ingenic_rng *priv = container_of(rng, struct ingenic_rng, rng);

	writel(ERNG_ENABLE, priv->base + RNG_REG_ERNG_OFFSET);

	return 0;
}

static void ingenic_rng_cleanup(struct hwrng *rng)
{
	struct ingenic_rng *priv = container_of(rng, struct ingenic_rng, rng);

	writel(0, priv->base + RNG_REG_ERNG_OFFSET);
}

static int ingenic_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct ingenic_rng *priv = container_of(rng, struct ingenic_rng, rng);
	u32 *data = buf;
	u32 status;
	int ret;

	if (priv->version >= ID_X1000) {
		ret = readl_poll_timeout(priv->base + RNG_REG_ERNG_OFFSET, status,
					 status & ERNG_READY, 10, 1000);
		if (ret == -ETIMEDOUT) {
			pr_err("%s: Wait for RNG data ready timeout\n", __func__);
			return ret;
		}
	} else {
		/*
		 * A delay is required so that the current RNG data is not bit shifted
		 * version of previous RNG data which could happen if random data is
		 * read continuously from this device.
		 */
		udelay(20);
	}

	*data = readl(priv->base + RNG_REG_RNG_OFFSET);

	return 4;
}

static int ingenic_rng_probe(struct platform_device *pdev)
{
	struct ingenic_rng *priv;
	int ret;

	priv = devm_kzalloc(&pdev->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(priv->base)) {
		pr_err("%s: Failed to map RNG registers\n", __func__);
		return PTR_ERR(priv->base);
	}

	priv->version = (enum ingenic_rng_version)(uintptr_t)of_device_get_match_data(&pdev->dev);

	priv->rng.name = pdev->name;
	priv->rng.init = ingenic_rng_init;
	priv->rng.cleanup = ingenic_rng_cleanup;
	priv->rng.read = ingenic_rng_read;

	ret = hwrng_register(&priv->rng);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register hwrng\n");
		return ret;
	}

	platform_set_drvdata(pdev, priv);

	dev_info(&pdev->dev, "Ingenic RNG driver registered\n");
	return 0;
}

static void ingenic_rng_remove(struct platform_device *pdev)
{
	struct ingenic_rng *priv = platform_get_drvdata(pdev);

	hwrng_unregister(&priv->rng);

	writel(0, priv->base + RNG_REG_ERNG_OFFSET);
}

static const struct of_device_id ingenic_rng_of_match[] = {
	{ .compatible = "ingenic,jz4780-rng", .data = (void *) ID_JZ4780 },
	{ .compatible = "ingenic,x1000-rng", .data = (void *) ID_X1000 },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ingenic_rng_of_match);

static struct platform_driver ingenic_rng_driver = {
	.probe		= ingenic_rng_probe,
	.remove_new	= ingenic_rng_remove,
	.driver		= {
		.name	= "ingenic-rng",
		.of_match_table = ingenic_rng_of_match,
	},
};

module_platform_driver(ingenic_rng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("PrasannaKumar Muralidharan <prasannatsmkumar@gmail.com>");
MODULE_AUTHOR("周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>");
MODULE_DESCRIPTION("Ingenic Random Number Generator driver");
