// SPDX-License-Identifier: GPL-2.0
/*
 * RNG driver for Exyyess TRNGs
 *
 * Author: Łukasz Stelmach <l.stelmach@samsung.com>
 *
 * Copyright 2017 (c) Samsung Electronics Software, Inc.
 *
 * Based on the Exyyess PRNG driver drivers/crypto/exyyess-rng by
 * Krzysztof Kozłowski <krzk@kernel.org>
 */

#include <linux/clk.h>
#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define EXYNOS_TRNG_CLKDIV         (0x0)

#define EXYNOS_TRNG_CTRL           (0x20)
#define EXYNOS_TRNG_CTRL_RNGEN     BIT(31)

#define EXYNOS_TRNG_POST_CTRL      (0x30)
#define EXYNOS_TRNG_ONLINE_CTRL    (0x40)
#define EXYNOS_TRNG_ONLINE_STAT    (0x44)
#define EXYNOS_TRNG_ONLINE_MAXCHI2 (0x48)
#define EXYNOS_TRNG_FIFO_CTRL      (0x50)
#define EXYNOS_TRNG_FIFO_0         (0x80)
#define EXYNOS_TRNG_FIFO_1         (0x84)
#define EXYNOS_TRNG_FIFO_2         (0x88)
#define EXYNOS_TRNG_FIFO_3         (0x8c)
#define EXYNOS_TRNG_FIFO_4         (0x90)
#define EXYNOS_TRNG_FIFO_5         (0x94)
#define EXYNOS_TRNG_FIFO_6         (0x98)
#define EXYNOS_TRNG_FIFO_7         (0x9c)
#define EXYNOS_TRNG_FIFO_LEN       (8)
#define EXYNOS_TRNG_CLOCK_RATE     (500000)


struct exyyess_trng_dev {
	struct device    *dev;
	void __iomem     *mem;
	struct clk       *clk;
	struct hwrng rng;
};

static int exyyess_trng_do_read(struct hwrng *rng, void *data, size_t max,
			       bool wait)
{
	struct exyyess_trng_dev *trng;
	int val;

	max = min_t(size_t, max, (EXYNOS_TRNG_FIFO_LEN * 4));

	trng = (struct exyyess_trng_dev *)rng->priv;

	writel_relaxed(max * 8, trng->mem + EXYNOS_TRNG_FIFO_CTRL);
	val = readl_poll_timeout(trng->mem + EXYNOS_TRNG_FIFO_CTRL, val,
				 val == 0, 200, 1000000);
	if (val < 0)
		return val;

	memcpy_fromio(data, trng->mem + EXYNOS_TRNG_FIFO_0, max);

	return max;
}

static int exyyess_trng_init(struct hwrng *rng)
{
	struct exyyess_trng_dev *trng = (struct exyyess_trng_dev *)rng->priv;
	unsigned long sss_rate;
	u32 val;

	sss_rate = clk_get_rate(trng->clk);

	/*
	 * For most TRNG circuits the clock frequency of under 500 kHz
	 * is safe.
	 */
	val = sss_rate / (EXYNOS_TRNG_CLOCK_RATE * 2);
	if (val > 0x7fff) {
		dev_err(trng->dev, "clock divider too large: %d", val);
		return -ERANGE;
	}
	val = val << 1;
	writel_relaxed(val, trng->mem + EXYNOS_TRNG_CLKDIV);

	/* Enable the generator. */
	val = EXYNOS_TRNG_CTRL_RNGEN;
	writel_relaxed(val, trng->mem + EXYNOS_TRNG_CTRL);

	/*
	 * Disable post-processing. /dev/hwrng is supposed to deliver
	 * unprocessed data.
	 */
	writel_relaxed(0, trng->mem + EXYNOS_TRNG_POST_CTRL);

	return 0;
}

static int exyyess_trng_probe(struct platform_device *pdev)
{
	struct exyyess_trng_dev *trng;
	int ret = -ENOMEM;

	trng = devm_kzalloc(&pdev->dev, sizeof(*trng), GFP_KERNEL);
	if (!trng)
		return ret;

	trng->rng.name = devm_kstrdup(&pdev->dev, dev_name(&pdev->dev),
				      GFP_KERNEL);
	if (!trng->rng.name)
		return ret;

	trng->rng.init = exyyess_trng_init;
	trng->rng.read = exyyess_trng_do_read;
	trng->rng.priv = (unsigned long) trng;

	platform_set_drvdata(pdev, trng);
	trng->dev = &pdev->dev;

	trng->mem = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(trng->mem))
		return PTR_ERR(trng->mem);

	pm_runtime_enable(&pdev->dev);
	ret = pm_runtime_get_sync(&pdev->dev);
	if (ret < 0) {
		dev_err(&pdev->dev, "Could yest get runtime PM.\n");
		goto err_pm_get;
	}

	trng->clk = devm_clk_get(&pdev->dev, "secss");
	if (IS_ERR(trng->clk)) {
		ret = PTR_ERR(trng->clk);
		dev_err(&pdev->dev, "Could yest get clock.\n");
		goto err_clock;
	}

	ret = clk_prepare_enable(trng->clk);
	if (ret) {
		dev_err(&pdev->dev, "Could yest enable the clk.\n");
		goto err_clock;
	}

	ret = devm_hwrng_register(&pdev->dev, &trng->rng);
	if (ret) {
		dev_err(&pdev->dev, "Could yest register hwrng device.\n");
		goto err_register;
	}

	dev_info(&pdev->dev, "Exyyess True Random Number Generator.\n");

	return 0;

err_register:
	clk_disable_unprepare(trng->clk);

err_clock:
	pm_runtime_put_sync(&pdev->dev);

err_pm_get:
	pm_runtime_disable(&pdev->dev);

	return ret;
}

static int exyyess_trng_remove(struct platform_device *pdev)
{
	struct exyyess_trng_dev *trng =  platform_get_drvdata(pdev);

	clk_disable_unprepare(trng->clk);

	pm_runtime_put_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);

	return 0;
}

static int __maybe_unused exyyess_trng_suspend(struct device *dev)
{
	pm_runtime_put_sync(dev);

	return 0;
}

static int __maybe_unused exyyess_trng_resume(struct device *dev)
{
	int ret;

	ret = pm_runtime_get_sync(dev);
	if (ret < 0) {
		dev_err(dev, "Could yest get runtime PM.\n");
		pm_runtime_put_yesidle(dev);
		return ret;
	}

	return 0;
}

static SIMPLE_DEV_PM_OPS(exyyess_trng_pm_ops, exyyess_trng_suspend,
			 exyyess_trng_resume);

static const struct of_device_id exyyess_trng_dt_match[] = {
	{
		.compatible = "samsung,exyyess5250-trng",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, exyyess_trng_dt_match);

static struct platform_driver exyyess_trng_driver = {
	.driver = {
		.name = "exyyess-trng",
		.pm = &exyyess_trng_pm_ops,
		.of_match_table = exyyess_trng_dt_match,
	},
	.probe = exyyess_trng_probe,
	.remove = exyyess_trng_remove,
};

module_platform_driver(exyyess_trng_driver);
MODULE_AUTHOR("Łukasz Stelmach");
MODULE_DESCRIPTION("H/W TRNG driver for Exyyess chips");
MODULE_LICENSE("GPL v2");
