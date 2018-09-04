// SPDX-License-Identifier: GPL-2.0
/*
 * rockchip-rng.c Random Number Generator driver for the Rockchip
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd.
 * Author: Lin Jinhan <troy.lin@rock-chips.com>
 *
 */
#include <linux/clk.h>
#include <linux/hw_random.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>

#define _SBF(s, v)	((v) << (s))
#define HIWORD_UPDATE(val, mask, shift) \
			((val) << (shift) | (mask) << ((shift) + 16))

#define CRYPTO_CLK_NUM	(4)
#define ROCKCHIP_AUTOSUSPEND_DELAY	100
#define ROCKCHIP_POLL_PERIOD_US		100
#define ROCKCHIP_POLL_TIMEOUT_US	10000

#define RK_MAX_RNG_BYTE			(32)
#define CRYPTO_RNG_CTL			0x0400
#define CRYPTO_RNG_64_BIT_LEN		_SBF(4, 0x00)
#define CRYPTO_RNG_128_BIT_LEN		_SBF(4, 0x01)
#define CRYPTO_RNG_192_BIT_LEN		_SBF(4, 0x02)
#define CRYPTO_RNG_256_BIT_LEN		_SBF(4, 0x03)
#define CRYPTO_RNG_FATESY_SOC_RING	_SBF(2, 0x00)
#define CRYPTO_RNG_SLOWER_SOC_RING_0	_SBF(2, 0x01)
#define CRYPTO_RNG_SLOWER_SOC_RING_1	_SBF(2, 0x02)
#define CRYPTO_RNG_SLOWEST_SOC_RING	_SBF(2, 0x03)
#define CRYPTO_RNG_ENABLE		BIT(1)
#define CRYPTO_RNG_START		BIT(0)

#define CRYPTO_RNG_SAMPLE_CNT		0x0404
#define CRYPTO_RNG_DOUT_0		0x0410

struct rk_rng {
	struct device		*dev;
	struct hwrng		rng;
	void __iomem		*mem;
	struct clk_bulk_data	clk_bulk[CRYPTO_CLK_NUM];
};

static void rk_rng_writel(struct rk_rng *rng, u32 val, u32 offset)
{
	__raw_writel(val, rng->mem + offset);
}

static int rk_rng_init(struct hwrng *rng)
{
	int ret;
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);

	dev_dbg(rk_rng->dev, "clk_bulk_prepare_enable.\n");

	ret = clk_bulk_prepare_enable(CRYPTO_CLK_NUM, &rk_rng->clk_bulk[0]);
	if (ret < 0) {
		dev_err(rk_rng->dev, "failed to enable clks %d\n", ret);
		return ret;
	}

	return 0;
}

static void rk_rng_cleanup(struct hwrng *rng)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);

	dev_dbg(rk_rng->dev, "clk_bulk_disable_unprepare.\n");
	clk_bulk_disable_unprepare(CRYPTO_CLK_NUM, &rk_rng->clk_bulk[0]);
}

static int rk_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	int ret = 0;
	u32 reg_ctrl = 0;
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);

	pm_runtime_get_sync(rk_rng->dev);

	/* enable osc_ring to get entropy, sample period is set as 100 */
	rk_rng_writel(rk_rng, 100, CRYPTO_RNG_SAMPLE_CNT);

	reg_ctrl |= CRYPTO_RNG_256_BIT_LEN;
	reg_ctrl |= CRYPTO_RNG_SLOWER_SOC_RING_0;
	reg_ctrl |= CRYPTO_RNG_ENABLE;
	reg_ctrl |= CRYPTO_RNG_START;

	rk_rng_writel(rk_rng, HIWORD_UPDATE(reg_ctrl, 0xffff, 0),
			CRYPTO_RNG_CTL);

	ret = readl_poll_timeout(rk_rng->mem + CRYPTO_RNG_CTL, reg_ctrl,
				!(reg_ctrl & CRYPTO_RNG_START),
				ROCKCHIP_POLL_PERIOD_US,
				ROCKCHIP_POLL_TIMEOUT_US);
	if (ret < 0)
		goto out;

	ret = min_t(size_t, max, RK_MAX_RNG_BYTE);

	memcpy_fromio(buf, rk_rng->mem + CRYPTO_RNG_DOUT_0, ret);

out:
	/* close TRNG */
	rk_rng_writel(rk_rng, HIWORD_UPDATE(0, 0xffff, 0), CRYPTO_RNG_CTL);

	pm_runtime_mark_last_busy(rk_rng->dev);
	pm_runtime_put_sync_autosuspend(rk_rng->dev);

	return ret;
}

static int rk_rng_probe(struct platform_device *pdev)
{
	struct rk_rng *rk_rng;
	int ret;

	dev_dbg(&pdev->dev, "probing...\n");
	rk_rng = devm_kzalloc(&pdev->dev, sizeof(struct rk_rng), GFP_KERNEL);
	if (!rk_rng)
		return -ENOMEM;

	rk_rng->dev = &pdev->dev;
	rk_rng->rng.name    = "rockchip";
#ifndef CONFIG_PM
	rk_rng->rng.init    = rk_rng_init;
	rk_rng->rng.cleanup = rk_rng_cleanup,
#endif
	rk_rng->rng.read    = rk_rng_read;
	rk_rng->rng.quality = 999;

	rk_rng->clk_bulk[0].id = "hclk_crypto";
	rk_rng->clk_bulk[1].id = "aclk_crypto";
	rk_rng->clk_bulk[2].id = "clk_crypto";
	rk_rng->clk_bulk[3].id = "clk_crypto_apk";

	rk_rng->mem = devm_of_iomap(&pdev->dev, pdev->dev.of_node, 0, NULL);
	if (IS_ERR(rk_rng->mem))
		return PTR_ERR(rk_rng->mem);

	ret = devm_clk_bulk_get(&pdev->dev, CRYPTO_CLK_NUM,
				&rk_rng->clk_bulk[0]);
	if (ret) {
		dev_err(&pdev->dev, "failed to get clks property\n");
		return ret;
	}

	platform_set_drvdata(pdev, rk_rng);

	pm_runtime_set_autosuspend_delay(&pdev->dev,
					ROCKCHIP_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = devm_hwrng_register(&pdev->dev, &rk_rng->rng);
	if (ret) {
		pm_runtime_dont_use_autosuspend(&pdev->dev);
		pm_runtime_disable(&pdev->dev);
	}

	return ret;
}

#ifdef CONFIG_PM
static int rk_rng_runtime_suspend(struct device *dev)
{
	struct rk_rng *rk_rng = dev_get_drvdata(dev);

	rk_rng_cleanup(&rk_rng->rng);

	return 0;
}

static int rk_rng_runtime_resume(struct device *dev)
{
	struct rk_rng *rk_rng = dev_get_drvdata(dev);

	return rk_rng_init(&rk_rng->rng);
}

static const struct dev_pm_ops rk_rng_pm_ops = {
	SET_RUNTIME_PM_OPS(rk_rng_runtime_suspend,
				rk_rng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

#endif

static const struct of_device_id rk_rng_dt_match[] = {
	{
		.compatible = "rockchip,cryptov2-rng",
	},
	{ },
};
MODULE_DEVICE_TABLE(of, rk_rng_dt_match);

static struct platform_driver rk_rng_driver = {
	.driver	= {
		.name	= "rockchip-rng",
#ifdef CONFIG_PM
		.pm	= &rk_rng_pm_ops,
#endif
		.of_match_table = rk_rng_dt_match,
	},
	.probe	= rk_rng_probe,
};

module_platform_driver(rk_rng_driver);

MODULE_DESCRIPTION("ROCKCHIP H/W Random Number Generator driver");
MODULE_AUTHOR("Lin Jinhan <troy.lin@rock-chips.com>");
MODULE_LICENSE("GPL v2");
