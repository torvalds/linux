// SPDX-License-Identifier: GPL-2.0
/*
 * rockchip-rng.c True Random Number Generator driver for Rockchip SoCs
 *
 * Copyright (c) 2018, Fuzhou Rockchip Electronics Co., Ltd.
 * Copyright (c) 2022, Aurelien Jarno
 * Copyright (c) 2025, Collabora Ltd.
 * Authors:
 *  Lin Jinhan <troy.lin@rock-chips.com>
 *  Aurelien Jarno <aurelien@aurel32.net>
 *  Nicolas Frattaroli <nicolas.frattaroli@collabora.com>
 */
#include <linux/clk.h>
#include <linux/hw_random.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>
#include <linux/slab.h>

#define RK_RNG_AUTOSUSPEND_DELAY	100
#define RK_RNG_MAX_BYTE			32
#define RK_RNG_POLL_PERIOD_US		100
#define RK_RNG_POLL_TIMEOUT_US		10000

/*
 * TRNG collects osc ring output bit every RK_RNG_SAMPLE_CNT time. The value is
 * a tradeoff between speed and quality and has been adjusted to get a quality
 * of ~900 (~87.5% of FIPS 140-2 successes).
 */
#define RK_RNG_SAMPLE_CNT		1000

/* after how many bytes of output TRNGv1 implementations should be reseeded */
#define RK_TRNG_V1_AUTO_RESEED_CNT	16000

/* TRNG registers from RK3568 TRM-Part2, section 5.4.1 */
#define TRNG_RST_CTL			0x0004
#define TRNG_RNG_CTL			0x0400
#define TRNG_RNG_CTL_LEN_64_BIT		(0x00 << 4)
#define TRNG_RNG_CTL_LEN_128_BIT	(0x01 << 4)
#define TRNG_RNG_CTL_LEN_192_BIT	(0x02 << 4)
#define TRNG_RNG_CTL_LEN_256_BIT	(0x03 << 4)
#define TRNG_RNG_CTL_OSC_RING_SPEED_0	(0x00 << 2)
#define TRNG_RNG_CTL_OSC_RING_SPEED_1	(0x01 << 2)
#define TRNG_RNG_CTL_OSC_RING_SPEED_2	(0x02 << 2)
#define TRNG_RNG_CTL_OSC_RING_SPEED_3	(0x03 << 2)
#define TRNG_RNG_CTL_MASK		GENMASK(15, 0)
#define TRNG_RNG_CTL_ENABLE		BIT(1)
#define TRNG_RNG_CTL_START		BIT(0)
#define TRNG_RNG_SAMPLE_CNT		0x0404
#define TRNG_RNG_DOUT			0x0410

/*
 * TRNG V1 register definitions
 * The TRNG V1 IP is a stand-alone TRNG implementation (not part of a crypto IP)
 * and can be found in the Rockchip RK3588 SoC
 */
#define TRNG_V1_CTRL				0x0000
#define TRNG_V1_CTRL_NOP			0x00
#define TRNG_V1_CTRL_RAND			0x01
#define TRNG_V1_CTRL_SEED			0x02

#define TRNG_V1_STAT				0x0004
#define TRNG_V1_STAT_SEEDED			BIT(9)
#define TRNG_V1_STAT_GENERATING			BIT(30)
#define TRNG_V1_STAT_RESEEDING			BIT(31)

#define TRNG_V1_MODE				0x0008
#define TRNG_V1_MODE_128_BIT			(0x00 << 3)
#define TRNG_V1_MODE_256_BIT			(0x01 << 3)

/* Interrupt Enable register; unused because polling is faster */
#define TRNG_V1_IE				0x0010
#define TRNG_V1_IE_GLBL_EN			BIT(31)
#define TRNG_V1_IE_SEED_DONE_EN			BIT(1)
#define TRNG_V1_IE_RAND_RDY_EN			BIT(0)

#define TRNG_V1_ISTAT				0x0014
#define TRNG_V1_ISTAT_RAND_RDY			BIT(0)

/* RAND0 ~ RAND7 */
#define TRNG_V1_RAND0				0x0020
#define TRNG_V1_RAND7				0x003C

/* Auto Reseed Register */
#define TRNG_V1_AUTO_RQSTS			0x0060

#define TRNG_V1_VERSION				0x00F0
#define TRNG_v1_VERSION_CODE			0x46bc
/* end of TRNG_V1 register definitions */

/* Before removing this assert, give rk3588_rng_read an upper bound of 32 */
static_assert(RK_RNG_MAX_BYTE <= (TRNG_V1_RAND7 + 4 - TRNG_V1_RAND0),
	      "You raised RK_RNG_MAX_BYTE and broke rk3588-rng, congrats.");

struct rk_rng {
	struct hwrng rng;
	void __iomem *base;
	int clk_num;
	struct clk_bulk_data *clk_bulks;
	const struct rk_rng_soc_data *soc_data;
	struct device *dev;
};

struct rk_rng_soc_data {
	int (*rk_rng_init)(struct hwrng *rng);
	int (*rk_rng_read)(struct hwrng *rng, void *buf, size_t max, bool wait);
	void (*rk_rng_cleanup)(struct hwrng *rng);
	unsigned short quality;
	bool reset_optional;
};

/* The mask in the upper 16 bits determines the bits that are updated */
static void rk_rng_write_ctl(struct rk_rng *rng, u32 val, u32 mask)
{
	writel((mask << 16) | val, rng->base + TRNG_RNG_CTL);
}

static inline void rk_rng_writel(struct rk_rng *rng, u32 val, u32 offset)
{
	writel(val, rng->base + offset);
}

static inline u32 rk_rng_readl(struct rk_rng *rng, u32 offset)
{
	return readl(rng->base + offset);
}

static int rk_rng_enable_clks(struct rk_rng *rk_rng)
{
	int ret;
	/* start clocks */
	ret = clk_bulk_prepare_enable(rk_rng->clk_num, rk_rng->clk_bulks);
	if (ret < 0) {
		dev_err(rk_rng->dev, "Failed to enable clocks: %d\n", ret);
		return ret;
	}

	return 0;
}

static int rk3568_rng_init(struct hwrng *rng)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);
	int ret;

	ret = rk_rng_enable_clks(rk_rng);
	if (ret < 0)
		return ret;

	/* set the sample period */
	writel(RK_RNG_SAMPLE_CNT, rk_rng->base + TRNG_RNG_SAMPLE_CNT);

	/* set osc ring speed and enable it */
	rk_rng_write_ctl(rk_rng, TRNG_RNG_CTL_LEN_256_BIT |
				 TRNG_RNG_CTL_OSC_RING_SPEED_0 |
				 TRNG_RNG_CTL_ENABLE,
			 TRNG_RNG_CTL_MASK);

	return 0;
}

static void rk3568_rng_cleanup(struct hwrng *rng)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);

	/* stop TRNG */
	rk_rng_write_ctl(rk_rng, 0, TRNG_RNG_CTL_MASK);

	/* stop clocks */
	clk_bulk_disable_unprepare(rk_rng->clk_num, rk_rng->clk_bulks);
}

static int rk3568_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);
	size_t to_read = min_t(size_t, max, RK_RNG_MAX_BYTE);
	u32 reg;
	int ret = 0;

	ret = pm_runtime_resume_and_get(rk_rng->dev);
	if (ret < 0)
		return ret;

	/* Start collecting random data */
	rk_rng_write_ctl(rk_rng, TRNG_RNG_CTL_START, TRNG_RNG_CTL_START);

	ret = readl_poll_timeout(rk_rng->base + TRNG_RNG_CTL, reg,
				 !(reg & TRNG_RNG_CTL_START),
				 RK_RNG_POLL_PERIOD_US,
				 RK_RNG_POLL_TIMEOUT_US);
	if (ret < 0)
		goto out;

	/* Read random data stored in the registers */
	memcpy_fromio(buf, rk_rng->base + TRNG_RNG_DOUT, to_read);
out:
	pm_runtime_mark_last_busy(rk_rng->dev);
	pm_runtime_put_sync_autosuspend(rk_rng->dev);

	return (ret < 0) ? ret : to_read;
}

static int rk3588_rng_init(struct hwrng *rng)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);
	u32 version, status, mask, istat;
	int ret;

	ret = rk_rng_enable_clks(rk_rng);
	if (ret < 0)
		return ret;

	version = rk_rng_readl(rk_rng, TRNG_V1_VERSION);
	if (version != TRNG_v1_VERSION_CODE) {
		dev_err(rk_rng->dev,
			"wrong trng version, expected = %08x, actual = %08x\n",
			TRNG_V1_VERSION, version);
		ret = -EFAULT;
		goto err_disable_clk;
	}

	mask = TRNG_V1_STAT_SEEDED | TRNG_V1_STAT_GENERATING |
	       TRNG_V1_STAT_RESEEDING;
	if (readl_poll_timeout(rk_rng->base + TRNG_V1_STAT, status,
			       (status & mask) == TRNG_V1_STAT_SEEDED,
			       RK_RNG_POLL_PERIOD_US, RK_RNG_POLL_TIMEOUT_US) < 0) {
		dev_err(rk_rng->dev, "timed out waiting for hwrng to reseed\n");
		ret = -ETIMEDOUT;
		goto err_disable_clk;
	}

	/*
	 * clear ISTAT flag, downstream advises to do this to avoid
	 * auto-reseeding "on power on"
	 */
	istat = rk_rng_readl(rk_rng, TRNG_V1_ISTAT);
	rk_rng_writel(rk_rng, istat, TRNG_V1_ISTAT);

	/* auto reseed after RK_TRNG_V1_AUTO_RESEED_CNT bytes */
	rk_rng_writel(rk_rng, RK_TRNG_V1_AUTO_RESEED_CNT / 16, TRNG_V1_AUTO_RQSTS);

	return 0;
err_disable_clk:
	clk_bulk_disable_unprepare(rk_rng->clk_num, rk_rng->clk_bulks);
	return ret;
}

static void rk3588_rng_cleanup(struct hwrng *rng)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);

	clk_bulk_disable_unprepare(rk_rng->clk_num, rk_rng->clk_bulks);
}

static int rk3588_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct rk_rng *rk_rng = container_of(rng, struct rk_rng, rng);
	size_t to_read = min_t(size_t, max, RK_RNG_MAX_BYTE);
	int ret = 0;
	u32 reg;

	ret = pm_runtime_resume_and_get(rk_rng->dev);
	if (ret < 0)
		return ret;

	/* Clear ISTAT, even without interrupts enabled, this will be updated */
	reg = rk_rng_readl(rk_rng, TRNG_V1_ISTAT);
	rk_rng_writel(rk_rng, reg, TRNG_V1_ISTAT);

	/* generate 256 bits of random data */
	rk_rng_writel(rk_rng, TRNG_V1_MODE_256_BIT, TRNG_V1_MODE);
	rk_rng_writel(rk_rng, TRNG_V1_CTRL_RAND, TRNG_V1_CTRL);

	ret = readl_poll_timeout_atomic(rk_rng->base + TRNG_V1_ISTAT, reg,
					(reg & TRNG_V1_ISTAT_RAND_RDY), 0,
					RK_RNG_POLL_TIMEOUT_US);
	if (ret < 0)
		goto out;

	/* Read random data that's in registers TRNG_V1_RAND0 through RAND7 */
	memcpy_fromio(buf, rk_rng->base + TRNG_V1_RAND0, to_read);

out:
	/* Clear ISTAT */
	rk_rng_writel(rk_rng, reg, TRNG_V1_ISTAT);
	/* close the TRNG */
	rk_rng_writel(rk_rng, TRNG_V1_CTRL_NOP, TRNG_V1_CTRL);

	pm_runtime_mark_last_busy(rk_rng->dev);
	pm_runtime_put_sync_autosuspend(rk_rng->dev);

	return (ret < 0) ? ret : to_read;
}

static const struct rk_rng_soc_data rk3568_soc_data = {
	.rk_rng_init = rk3568_rng_init,
	.rk_rng_read = rk3568_rng_read,
	.rk_rng_cleanup = rk3568_rng_cleanup,
	.quality = 900,
	.reset_optional = false,
};

static const struct rk_rng_soc_data rk3588_soc_data = {
	.rk_rng_init = rk3588_rng_init,
	.rk_rng_read = rk3588_rng_read,
	.rk_rng_cleanup = rk3588_rng_cleanup,
	.quality = 999,		/* as determined by actual testing */
	.reset_optional = true,
};

static int rk_rng_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct reset_control *rst;
	struct rk_rng *rk_rng;
	int ret;

	rk_rng = devm_kzalloc(dev, sizeof(*rk_rng), GFP_KERNEL);
	if (!rk_rng)
		return -ENOMEM;

	rk_rng->soc_data = of_device_get_match_data(dev);
	rk_rng->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(rk_rng->base))
		return PTR_ERR(rk_rng->base);

	rk_rng->clk_num = devm_clk_bulk_get_all(dev, &rk_rng->clk_bulks);
	if (rk_rng->clk_num < 0)
		return dev_err_probe(dev, rk_rng->clk_num,
				     "Failed to get clks property\n");

	if (rk_rng->soc_data->reset_optional)
		rst = devm_reset_control_array_get_optional_exclusive(dev);
	else
		rst = devm_reset_control_array_get_exclusive(dev);

	if (rst) {
		if (IS_ERR(rst))
			return dev_err_probe(dev, PTR_ERR(rst), "Failed to get reset property\n");

		reset_control_assert(rst);
		udelay(2);
		reset_control_deassert(rst);
	}

	platform_set_drvdata(pdev, rk_rng);

	rk_rng->rng.name = dev_driver_string(dev);
	if (!IS_ENABLED(CONFIG_PM)) {
		rk_rng->rng.init = rk_rng->soc_data->rk_rng_init;
		rk_rng->rng.cleanup = rk_rng->soc_data->rk_rng_cleanup;
	}
	rk_rng->rng.read = rk_rng->soc_data->rk_rng_read;
	rk_rng->dev = dev;
	rk_rng->rng.quality = rk_rng->soc_data->quality;

	pm_runtime_set_autosuspend_delay(dev, RK_RNG_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(dev);
	ret = devm_pm_runtime_enable(dev);
	if (ret)
		return dev_err_probe(dev, ret, "Runtime pm activation failed.\n");

	ret = devm_hwrng_register(dev, &rk_rng->rng);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to register Rockchip hwrng\n");

	return 0;
}

static int __maybe_unused rk_rng_runtime_suspend(struct device *dev)
{
	struct rk_rng *rk_rng = dev_get_drvdata(dev);

	rk_rng->soc_data->rk_rng_cleanup(&rk_rng->rng);

	return 0;
}

static int __maybe_unused rk_rng_runtime_resume(struct device *dev)
{
	struct rk_rng *rk_rng = dev_get_drvdata(dev);

	return rk_rng->soc_data->rk_rng_init(&rk_rng->rng);
}

static const struct dev_pm_ops rk_rng_pm_ops = {
	SET_RUNTIME_PM_OPS(rk_rng_runtime_suspend,
				rk_rng_runtime_resume, NULL)
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
};

static const struct of_device_id rk_rng_dt_match[] = {
	{ .compatible = "rockchip,rk3568-rng", .data = (void *)&rk3568_soc_data },
	{ .compatible = "rockchip,rk3588-rng", .data = (void *)&rk3588_soc_data },
	{ /* sentinel */ },
};

MODULE_DEVICE_TABLE(of, rk_rng_dt_match);

static struct platform_driver rk_rng_driver = {
	.driver	= {
		.name	= "rockchip-rng",
		.pm	= &rk_rng_pm_ops,
		.of_match_table = rk_rng_dt_match,
	},
	.probe	= rk_rng_probe,
};

module_platform_driver(rk_rng_driver);

MODULE_DESCRIPTION("Rockchip True Random Number Generator driver");
MODULE_AUTHOR("Lin Jinhan <troy.lin@rock-chips.com>");
MODULE_AUTHOR("Aurelien Jarno <aurelien@aurel32.net>");
MODULE_AUTHOR("Daniel Golle <daniel@makrotopia.org>");
MODULE_AUTHOR("Nicolas Frattaroli <nicolas.frattaroli@collabora.com>");
MODULE_LICENSE("GPL");
