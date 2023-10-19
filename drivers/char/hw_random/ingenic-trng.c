// SPDX-License-Identifier: GPL-2.0
/*
 * Ingenic True Random Number Generator driver
 * Copyright (c) 2019 漆鹏振 (Qi Pengzhen) <aric.pzqi@ingenic.com>
 * Copyright (c) 2020 周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

/* DTRNG register offsets */
#define TRNG_REG_CFG_OFFSET			0x00
#define TRNG_REG_RANDOMNUM_OFFSET	0x04
#define TRNG_REG_STATUS_OFFSET		0x08

/* bits within the CFG register */
#define CFG_RDY_CLR					BIT(12)
#define CFG_INT_MASK				BIT(11)
#define CFG_GEN_EN					BIT(0)

/* bits within the STATUS register */
#define STATUS_RANDOM_RDY			BIT(0)

struct ingenic_trng {
	void __iomem *base;
	struct clk *clk;
	struct hwrng rng;
};

static int ingenic_trng_init(struct hwrng *rng)
{
	struct ingenic_trng *trng = container_of(rng, struct ingenic_trng, rng);
	unsigned int ctrl;

	ctrl = readl(trng->base + TRNG_REG_CFG_OFFSET);
	ctrl |= CFG_GEN_EN;
	writel(ctrl, trng->base + TRNG_REG_CFG_OFFSET);

	return 0;
}

static void ingenic_trng_cleanup(struct hwrng *rng)
{
	struct ingenic_trng *trng = container_of(rng, struct ingenic_trng, rng);
	unsigned int ctrl;

	ctrl = readl(trng->base + TRNG_REG_CFG_OFFSET);
	ctrl &= ~CFG_GEN_EN;
	writel(ctrl, trng->base + TRNG_REG_CFG_OFFSET);
}

static int ingenic_trng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct ingenic_trng *trng = container_of(rng, struct ingenic_trng, rng);
	u32 *data = buf;
	u32 status;
	int ret;

	ret = readl_poll_timeout(trng->base + TRNG_REG_STATUS_OFFSET, status,
				 status & STATUS_RANDOM_RDY, 10, 1000);
	if (ret == -ETIMEDOUT) {
		pr_err("%s: Wait for DTRNG data ready timeout\n", __func__);
		return ret;
	}

	*data = readl(trng->base + TRNG_REG_RANDOMNUM_OFFSET);

	return 4;
}

static int ingenic_trng_probe(struct platform_device *pdev)
{
	struct ingenic_trng *trng;
	int ret;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return -ENOMEM;

	trng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->base)) {
		pr_err("%s: Failed to map DTRNG registers\n", __func__);
		ret = PTR_ERR(trng->base);
		return PTR_ERR(trng->base);
	}

	trng->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(trng->clk)) {
		ret = PTR_ERR(trng->clk);
		pr_crit("%s: Cannot get DTRNG clock\n", __func__);
		return PTR_ERR(trng->clk);
	}

	ret = clk_prepare_enable(trng->clk);
	if (ret) {
		pr_crit("%s: Unable to enable DTRNG clock\n", __func__);
		return ret;
	}

	trng->rng.name = pdev->name;
	trng->rng.init = ingenic_trng_init;
	trng->rng.cleanup = ingenic_trng_cleanup;
	trng->rng.read = ingenic_trng_read;

	ret = hwrng_register(&trng->rng);
	if (ret) {
		dev_err(&pdev->dev, "Failed to register hwrng\n");
		goto err_unprepare_clk;
	}

	platform_set_drvdata(pdev, trng);

	dev_info(&pdev->dev, "Ingenic DTRNG driver registered\n");
	return 0;

err_unprepare_clk:
	clk_disable_unprepare(trng->clk);
	return ret;
}

static int ingenic_trng_remove(struct platform_device *pdev)
{
	struct ingenic_trng *trng = platform_get_drvdata(pdev);
	unsigned int ctrl;

	hwrng_unregister(&trng->rng);

	ctrl = readl(trng->base + TRNG_REG_CFG_OFFSET);
	ctrl &= ~CFG_GEN_EN;
	writel(ctrl, trng->base + TRNG_REG_CFG_OFFSET);

	clk_disable_unprepare(trng->clk);

	return 0;
}

static const struct of_device_id ingenic_trng_of_match[] = {
	{ .compatible = "ingenic,x1830-dtrng" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ingenic_trng_of_match);

static struct platform_driver ingenic_trng_driver = {
	.probe		= ingenic_trng_probe,
	.remove		= ingenic_trng_remove,
	.driver		= {
		.name	= "ingenic-trng",
		.of_match_table = ingenic_trng_of_match,
	},
};

module_platform_driver(ingenic_trng_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("漆鹏振 (Qi Pengzhen) <aric.pzqi@ingenic.com>");
MODULE_AUTHOR("周琰杰 (Zhou Yanjie) <zhouyanjie@wanyeetech.com>");
MODULE_DESCRIPTION("Ingenic True Random Number Generator driver");
