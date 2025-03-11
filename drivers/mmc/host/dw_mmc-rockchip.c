// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (c) 2014, Fuzhou Rockchip Electronics Co., Ltd
 */

#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/mmc/host.h>
#include <linux/of_address.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>

#include "dw_mmc.h"
#include "dw_mmc-pltfm.h"

#define RK3288_CLKGEN_DIV		2
#define SDMMC_TIMING_CON0		0x130
#define SDMMC_TIMING_CON1		0x134
#define ROCKCHIP_MMC_DELAY_SEL		BIT(10)
#define ROCKCHIP_MMC_DEGREE_MASK	0x3
#define ROCKCHIP_MMC_DEGREE_OFFSET	1
#define ROCKCHIP_MMC_DELAYNUM_OFFSET	2
#define ROCKCHIP_MMC_DELAYNUM_MASK	(0xff << ROCKCHIP_MMC_DELAYNUM_OFFSET)
#define ROCKCHIP_MMC_DELAY_ELEMENT_PSEC	60
#define HIWORD_UPDATE(val, mask, shift) \
		((val) << (shift) | (mask) << ((shift) + 16))

static const unsigned int freqs[] = { 100000, 200000, 300000, 400000 };

struct dw_mci_rockchip_priv_data {
	struct clk		*drv_clk;
	struct clk		*sample_clk;
	int			default_sample_phase;
	int			num_phases;
	bool			internal_phase;
};

/*
 * Each fine delay is between 44ps-77ps. Assume each fine delay is 60ps to
 * simplify calculations. So 45degs could be anywhere between 33deg and 57.8deg.
 */
static int rockchip_mmc_get_internal_phase(struct dw_mci *host, bool sample)
{
	unsigned long rate = clk_get_rate(host->ciu_clk);
	u32 raw_value;
	u16 degrees;
	u32 delay_num = 0;

	/* Constant signal, no measurable phase shift */
	if (!rate)
		return 0;

	if (sample)
		raw_value = mci_readl(host, TIMING_CON1);
	else
		raw_value = mci_readl(host, TIMING_CON0);

	raw_value >>= ROCKCHIP_MMC_DEGREE_OFFSET;
	degrees = (raw_value & ROCKCHIP_MMC_DEGREE_MASK) * 90;

	if (raw_value & ROCKCHIP_MMC_DELAY_SEL) {
		/* degrees/delaynum * 1000000 */
		unsigned long factor = (ROCKCHIP_MMC_DELAY_ELEMENT_PSEC / 10) *
					36 * (rate / 10000);

		delay_num = (raw_value & ROCKCHIP_MMC_DELAYNUM_MASK);
		delay_num >>= ROCKCHIP_MMC_DELAYNUM_OFFSET;
		degrees += DIV_ROUND_CLOSEST(delay_num * factor, 1000000);
	}

	return degrees % 360;
}

static int rockchip_mmc_get_phase(struct dw_mci *host, bool sample)
{
	struct dw_mci_rockchip_priv_data *priv = host->priv;
	struct clk *clock = sample ? priv->sample_clk : priv->drv_clk;

	if (priv->internal_phase)
		return rockchip_mmc_get_internal_phase(host, sample);
	else
		return clk_get_phase(clock);
}

static int rockchip_mmc_set_internal_phase(struct dw_mci *host, bool sample, int degrees)
{
	unsigned long rate = clk_get_rate(host->ciu_clk);
	u8 nineties, remainder;
	u8 delay_num;
	u32 raw_value;
	u32 delay;

	/*
	 * The below calculation is based on the output clock from
	 * MMC host to the card, which expects the phase clock inherits
	 * the clock rate from its parent, namely the output clock
	 * provider of MMC host. However, things may go wrong if
	 * (1) It is orphan.
	 * (2) It is assigned to the wrong parent.
	 *
	 * This check help debug the case (1), which seems to be the
	 * most likely problem we often face and which makes it difficult
	 * for people to debug unstable mmc tuning results.
	 */
	if (!rate) {
		dev_err(host->dev, "%s: invalid clk rate\n", __func__);
		return -EINVAL;
	}

	nineties = degrees / 90;
	remainder = (degrees % 90);

	/*
	 * Due to the inexact nature of the "fine" delay, we might
	 * actually go non-monotonic.  We don't go _too_ monotonic
	 * though, so we should be OK.  Here are options of how we may
	 * work:
	 *
	 * Ideally we end up with:
	 *   1.0, 2.0, ..., 69.0, 70.0, ...,  89.0, 90.0
	 *
	 * On one extreme (if delay is actually 44ps):
	 *   .73, 1.5, ..., 50.6, 51.3, ...,  65.3, 90.0
	 * The other (if delay is actually 77ps):
	 *   1.3, 2.6, ..., 88.6. 89.8, ..., 114.0, 90
	 *
	 * It's possible we might make a delay that is up to 25
	 * degrees off from what we think we're making.  That's OK
	 * though because we should be REALLY far from any bad range.
	 */

	/*
	 * Convert to delay; do a little extra work to make sure we
	 * don't overflow 32-bit / 64-bit numbers.
	 */
	delay = 10000000; /* PSECS_PER_SEC / 10000 / 10 */
	delay *= remainder;
	delay = DIV_ROUND_CLOSEST(delay,
			(rate / 1000) * 36 *
				(ROCKCHIP_MMC_DELAY_ELEMENT_PSEC / 10));

	delay_num = (u8) min_t(u32, delay, 255);

	raw_value = delay_num ? ROCKCHIP_MMC_DELAY_SEL : 0;
	raw_value |= delay_num << ROCKCHIP_MMC_DELAYNUM_OFFSET;
	raw_value |= nineties;

	if (sample)
		mci_writel(host, TIMING_CON1, HIWORD_UPDATE(raw_value, 0x07ff, 1));
	else
		mci_writel(host, TIMING_CON0, HIWORD_UPDATE(raw_value, 0x07ff, 1));

	dev_dbg(host->dev, "set %s_phase(%d) delay_nums=%u actual_degrees=%d\n",
		sample ? "sample" : "drv", degrees, delay_num,
		rockchip_mmc_get_phase(host, sample)
	);

	return 0;
}

static int rockchip_mmc_set_phase(struct dw_mci *host, bool sample, int degrees)
{
	struct dw_mci_rockchip_priv_data *priv = host->priv;
	struct clk *clock = sample ? priv->sample_clk : priv->drv_clk;

	if (priv->internal_phase)
		return rockchip_mmc_set_internal_phase(host, sample, degrees);
	else
		return clk_set_phase(clock, degrees);
}

static void dw_mci_rk3288_set_ios(struct dw_mci *host, struct mmc_ios *ios)
{
	struct dw_mci_rockchip_priv_data *priv = host->priv;
	int ret;
	unsigned int cclkin;
	u32 bus_hz;

	if (ios->clock == 0)
		return;

	/*
	 * cclkin: source clock of mmc controller
	 * bus_hz: card interface clock generated by CLKGEN
	 * bus_hz = cclkin / RK3288_CLKGEN_DIV
	 * ios->clock = (div == 0) ? bus_hz : (bus_hz / (2 * div))
	 *
	 * Note: div can only be 0 or 1, but div must be set to 1 for eMMC
	 * DDR52 8-bit mode.
	 */
	if (ios->bus_width == MMC_BUS_WIDTH_8 &&
	    ios->timing == MMC_TIMING_MMC_DDR52)
		cclkin = 2 * ios->clock * RK3288_CLKGEN_DIV;
	else
		cclkin = ios->clock * RK3288_CLKGEN_DIV;

	ret = clk_set_rate(host->ciu_clk, cclkin);
	if (ret)
		dev_warn(host->dev, "failed to set rate %uHz err: %d\n", cclkin, ret);

	bus_hz = clk_get_rate(host->ciu_clk) / RK3288_CLKGEN_DIV;
	if (bus_hz != host->bus_hz) {
		host->bus_hz = bus_hz;
		/* force dw_mci_setup_bus() */
		host->current_speed = 0;
	}

	/* Make sure we use phases which we can enumerate with */
	if (!IS_ERR(priv->sample_clk) && ios->timing <= MMC_TIMING_SD_HS)
		rockchip_mmc_set_phase(host, true, priv->default_sample_phase);

	/*
	 * Set the drive phase offset based on speed mode to achieve hold times.
	 *
	 * NOTE: this is _not_ a value that is dynamically tuned and is also
	 * _not_ a value that will vary from board to board.  It is a value
	 * that could vary between different SoC models if they had massively
	 * different output clock delays inside their dw_mmc IP block (delay_o),
	 * but since it's OK to overshoot a little we don't need to do complex
	 * calculations and can pick values that will just work for everyone.
	 *
	 * When picking values we'll stick with picking 0/90/180/270 since
	 * those can be made very accurately on all known Rockchip SoCs.
	 *
	 * Note that these values match values from the DesignWare Databook
	 * tables for the most part except for SDR12 and "ID mode".  For those
	 * two modes the databook calculations assume a clock in of 50MHz.  As
	 * seen above, we always use a clock in rate that is exactly the
	 * card's input clock (times RK3288_CLKGEN_DIV, but that gets divided
	 * back out before the controller sees it).
	 *
	 * From measurement of a single device, it appears that delay_o is
	 * about .5 ns.  Since we try to leave a bit of margin, it's expected
	 * that numbers here will be fine even with much larger delay_o
	 * (the 1.4 ns assumed by the DesignWare Databook would result in the
	 * same results, for instance).
	 */
	if (!IS_ERR(priv->drv_clk)) {
		int phase;

		/*
		 * In almost all cases a 90 degree phase offset will provide
		 * sufficient hold times across all valid input clock rates
		 * assuming delay_o is not absurd for a given SoC.  We'll use
		 * that as a default.
		 */
		phase = 90;

		switch (ios->timing) {
		case MMC_TIMING_MMC_DDR52:
			/*
			 * Since clock in rate with MMC_DDR52 is doubled when
			 * bus width is 8 we need to double the phase offset
			 * to get the same timings.
			 */
			if (ios->bus_width == MMC_BUS_WIDTH_8)
				phase = 180;
			break;
		case MMC_TIMING_UHS_SDR104:
		case MMC_TIMING_MMC_HS200:
			/*
			 * In the case of 150 MHz clock (typical max for
			 * Rockchip SoCs), 90 degree offset will add a delay
			 * of 1.67 ns.  That will meet min hold time of .8 ns
			 * as long as clock output delay is < .87 ns.  On
			 * SoCs measured this seems to be OK, but it doesn't
			 * hurt to give margin here, so we use 180.
			 */
			phase = 180;
			break;
		}

		rockchip_mmc_set_phase(host, false, phase);
	}
}

#define TUNING_ITERATION_TO_PHASE(i, num_phases) \
		(DIV_ROUND_UP((i) * 360, num_phases))

static int dw_mci_rk3288_execute_tuning(struct dw_mci_slot *slot, u32 opcode)
{
	struct dw_mci *host = slot->host;
	struct dw_mci_rockchip_priv_data *priv = host->priv;
	struct mmc_host *mmc = slot->mmc;
	int ret = 0;
	int i;
	bool v, prev_v = 0, first_v;
	struct range_t {
		int start;
		int end; /* inclusive */
	};
	struct range_t *ranges;
	unsigned int range_count = 0;
	int longest_range_len = -1;
	int longest_range = -1;
	int middle_phase;
	int phase;

	if (IS_ERR(priv->sample_clk)) {
		dev_err(host->dev, "Tuning clock (sample_clk) not defined.\n");
		return -EIO;
	}

	ranges = kmalloc_array(priv->num_phases / 2 + 1,
			       sizeof(*ranges), GFP_KERNEL);
	if (!ranges)
		return -ENOMEM;

	/* Try each phase and extract good ranges */
	for (i = 0; i < priv->num_phases; ) {
		rockchip_mmc_set_phase(host, true,
				       TUNING_ITERATION_TO_PHASE(
						i,
						priv->num_phases));

		v = !mmc_send_tuning(mmc, opcode, NULL);

		if (i == 0)
			first_v = v;

		if ((!prev_v) && v) {
			range_count++;
			ranges[range_count-1].start = i;
		}
		if (v) {
			ranges[range_count-1].end = i;
			i++;
		} else if (i == priv->num_phases - 1) {
			/* No extra skipping rules if we're at the end */
			i++;
		} else {
			/*
			 * No need to check too close to an invalid
			 * one since testing bad phases is slow.  Skip
			 * 20 degrees.
			 */
			i += DIV_ROUND_UP(20 * priv->num_phases, 360);

			/* Always test the last one */
			if (i >= priv->num_phases)
				i = priv->num_phases - 1;
		}

		prev_v = v;
	}

	if (range_count == 0) {
		dev_warn(host->dev, "All phases bad!");
		ret = -EIO;
		goto free;
	}

	/* wrap around case, merge the end points */
	if ((range_count > 1) && first_v && v) {
		ranges[0].start = ranges[range_count-1].start;
		range_count--;
	}

	if (ranges[0].start == 0 && ranges[0].end == priv->num_phases - 1) {
		rockchip_mmc_set_phase(host, true, priv->default_sample_phase);

		dev_info(host->dev, "All phases work, using default phase %d.",
			 priv->default_sample_phase);
		goto free;
	}

	/* Find the longest range */
	for (i = 0; i < range_count; i++) {
		int len = (ranges[i].end - ranges[i].start + 1);

		if (len < 0)
			len += priv->num_phases;

		if (longest_range_len < len) {
			longest_range_len = len;
			longest_range = i;
		}

		dev_dbg(host->dev, "Good phase range %d-%d (%d len)\n",
			TUNING_ITERATION_TO_PHASE(ranges[i].start,
						  priv->num_phases),
			TUNING_ITERATION_TO_PHASE(ranges[i].end,
						  priv->num_phases),
			len
		);
	}

	dev_dbg(host->dev, "Best phase range %d-%d (%d len)\n",
		TUNING_ITERATION_TO_PHASE(ranges[longest_range].start,
					  priv->num_phases),
		TUNING_ITERATION_TO_PHASE(ranges[longest_range].end,
					  priv->num_phases),
		longest_range_len
	);

	middle_phase = ranges[longest_range].start + longest_range_len / 2;
	middle_phase %= priv->num_phases;
	phase = TUNING_ITERATION_TO_PHASE(middle_phase, priv->num_phases);
	dev_info(host->dev, "Successfully tuned phase to %d\n", phase);

	rockchip_mmc_set_phase(host, true, phase);

free:
	kfree(ranges);
	return ret;
}

static int dw_mci_common_parse_dt(struct dw_mci *host)
{
	struct device_node *np = host->dev->of_node;
	struct dw_mci_rockchip_priv_data *priv;

	priv = devm_kzalloc(host->dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (of_property_read_u32(np, "rockchip,desired-num-phases",
				 &priv->num_phases))
		priv->num_phases = 360;

	if (of_property_read_u32(np, "rockchip,default-sample-phase",
				 &priv->default_sample_phase))
		priv->default_sample_phase = 0;

	host->priv = priv;

	return 0;
}

static int dw_mci_rk3288_parse_dt(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv;
	int err;

	err = dw_mci_common_parse_dt(host);
	if (err)
		return err;

	priv = host->priv;

	priv->drv_clk = devm_clk_get(host->dev, "ciu-drive");
	if (IS_ERR(priv->drv_clk))
		dev_dbg(host->dev, "ciu-drive not available\n");

	priv->sample_clk = devm_clk_get(host->dev, "ciu-sample");
	if (IS_ERR(priv->sample_clk))
		dev_dbg(host->dev, "ciu-sample not available\n");

	priv->internal_phase = false;

	return 0;
}

static int dw_mci_rk3576_parse_dt(struct dw_mci *host)
{
	struct dw_mci_rockchip_priv_data *priv;
	int err = dw_mci_common_parse_dt(host);
	if (err)
		return err;

	priv = host->priv;

	priv->internal_phase = true;

	return 0;
}

static int dw_mci_rockchip_init(struct dw_mci *host)
{
	int ret, i;

	/* It is slot 8 on Rockchip SoCs */
	host->sdio_id0 = 8;

	if (of_device_is_compatible(host->dev->of_node, "rockchip,rk3288-dw-mshc")) {
		host->bus_hz /= RK3288_CLKGEN_DIV;

		/* clock driver will fail if the clock is less than the lowest source clock
		 * divided by the internal clock divider. Test for the lowest available
		 * clock and set the minimum freq to clock / clock divider.
		 */

		for (i = 0; i < ARRAY_SIZE(freqs); i++) {
			ret = clk_round_rate(host->ciu_clk, freqs[i] * RK3288_CLKGEN_DIV);
			if (ret > 0) {
				host->minimum_speed = ret / RK3288_CLKGEN_DIV;
				break;
			}
		}
		if (ret < 0)
			dev_warn(host->dev, "no valid minimum freq: %d\n", ret);
	}

	return 0;
}

static const struct dw_mci_drv_data rk2928_drv_data = {
	.init			= dw_mci_rockchip_init,
};

static const struct dw_mci_drv_data rk3288_drv_data = {
	.common_caps		= MMC_CAP_CMD23,
	.set_ios		= dw_mci_rk3288_set_ios,
	.execute_tuning		= dw_mci_rk3288_execute_tuning,
	.parse_dt		= dw_mci_rk3288_parse_dt,
	.init			= dw_mci_rockchip_init,
};

static const struct dw_mci_drv_data rk3576_drv_data = {
	.common_caps		= MMC_CAP_CMD23,
	.set_ios		= dw_mci_rk3288_set_ios,
	.execute_tuning		= dw_mci_rk3288_execute_tuning,
	.parse_dt		= dw_mci_rk3576_parse_dt,
	.init			= dw_mci_rockchip_init,
};

static const struct of_device_id dw_mci_rockchip_match[] = {
	{ .compatible = "rockchip,rk2928-dw-mshc",
		.data = &rk2928_drv_data },
	{ .compatible = "rockchip,rk3288-dw-mshc",
		.data = &rk3288_drv_data },
	{ .compatible = "rockchip,rk3576-dw-mshc",
		.data = &rk3576_drv_data },
	{},
};
MODULE_DEVICE_TABLE(of, dw_mci_rockchip_match);

static int dw_mci_rockchip_probe(struct platform_device *pdev)
{
	const struct dw_mci_drv_data *drv_data;
	const struct of_device_id *match;
	int ret;

	if (!pdev->dev.of_node)
		return -ENODEV;

	match = of_match_node(dw_mci_rockchip_match, pdev->dev.of_node);
	drv_data = match->data;

	pm_runtime_get_noresume(&pdev->dev);
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);

	ret = dw_mci_pltfm_register(pdev, drv_data);
	if (ret) {
		pm_runtime_disable(&pdev->dev);
		pm_runtime_set_suspended(&pdev->dev);
		pm_runtime_put_noidle(&pdev->dev);
		return ret;
	}

	pm_runtime_put_autosuspend(&pdev->dev);

	return 0;
}

static void dw_mci_rockchip_remove(struct platform_device *pdev)
{
	pm_runtime_get_sync(&pdev->dev);
	pm_runtime_disable(&pdev->dev);
	pm_runtime_put_noidle(&pdev->dev);

	dw_mci_pltfm_remove(pdev);
}

static const struct dev_pm_ops dw_mci_rockchip_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(dw_mci_runtime_suspend,
			   dw_mci_runtime_resume,
			   NULL)
};

static struct platform_driver dw_mci_rockchip_pltfm_driver = {
	.probe		= dw_mci_rockchip_probe,
	.remove		= dw_mci_rockchip_remove,
	.driver		= {
		.name		= "dwmmc_rockchip",
		.probe_type	= PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table	= dw_mci_rockchip_match,
		.pm		= &dw_mci_rockchip_dev_pm_ops,
	},
};

module_platform_driver(dw_mci_rockchip_pltfm_driver);

MODULE_AUTHOR("Addy Ke <addy.ke@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip Specific DW-MSHC Driver Extension");
MODULE_ALIAS("platform:dwmmc_rockchip");
MODULE_LICENSE("GPL v2");
