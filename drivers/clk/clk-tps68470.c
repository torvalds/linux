// SPDX-License-Identifier: GPL-2.0
/*
 * Clock driver for TPS68470 PMIC
 *
 * Copyright (c) 2021 Red Hat Inc.
 * Copyright (C) 2018 Intel Corporation
 *
 * Authors:
 *	Hans de Goede <hdegoede@redhat.com>
 *	Zaikuo Wang <zaikuo.wang@intel.com>
 *	Tianshu Qiu <tian.shu.qiu@intel.com>
 *	Jian Xu Zheng <jian.xu.zheng@intel.com>
 *	Yuning Pu <yuning.pu@intel.com>
 *	Antti Laakso <antti.laakso@intel.com>
 */

#include <linux/clk-provider.h>
#include <linux/clkdev.h>
#include <linux/kernel.h>
#include <linux/mfd/tps68470.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/platform_data/tps68470.h>
#include <linux/regmap.h>

#define TPS68470_CLK_NAME "tps68470-clk"

#define to_tps68470_clkdata(clkd) \
	container_of(clkd, struct tps68470_clkdata, clkout_hw)

static struct tps68470_clkout_freqs {
	unsigned long freq;
	unsigned int xtaldiv;
	unsigned int plldiv;
	unsigned int postdiv;
	unsigned int buckdiv;
	unsigned int boostdiv;
} clk_freqs[] = {
/*
 *  The PLL is used to multiply the crystal oscillator
 *  frequency range of 3 MHz to 27 MHz by a programmable
 *  factor of F = (M/N)*(1/P) such that the output
 *  available at the HCLK_A or HCLK_B pins are in the range
 *  of 4 MHz to 64 MHz in increments of 0.1 MHz.
 *
 * hclk_# = osc_in * (((plldiv*2)+320) / (xtaldiv+30)) * (1 / 2^postdiv)
 *
 * PLL_REF_CLK should be as close as possible to 100kHz
 * PLL_REF_CLK = input clk / XTALDIV[7:0] + 30)
 *
 * PLL_VCO_CLK = (PLL_REF_CLK * (plldiv*2 + 320))
 *
 * BOOST should be as close as possible to 2Mhz
 * BOOST = PLL_VCO_CLK / (BOOSTDIV[4:0] + 16) *
 *
 * BUCK should be as close as possible to 5.2Mhz
 * BUCK = PLL_VCO_CLK / (BUCKDIV[3:0] + 5)
 *
 * osc_in   xtaldiv  plldiv   postdiv   hclk_#
 * 20Mhz    170      32       1         19.2Mhz
 * 20Mhz    170      40       1         20Mhz
 * 20Mhz    170      80       1         24Mhz
 */
	{ 19200000, 170, 32, 1, 2, 3 },
	{ 20000000, 170, 40, 1, 3, 4 },
	{ 24000000, 170, 80, 1, 4, 8 },
};

struct tps68470_clkdata {
	struct clk_hw clkout_hw;
	struct regmap *regmap;
	unsigned long rate;
};

static int tps68470_clk_is_prepared(struct clk_hw *hw)
{
	struct tps68470_clkdata *clkdata = to_tps68470_clkdata(hw);
	int val;

	if (regmap_read(clkdata->regmap, TPS68470_REG_PLLCTL, &val))
		return 0;

	return val & TPS68470_PLL_EN_MASK;
}

static int tps68470_clk_prepare(struct clk_hw *hw)
{
	struct tps68470_clkdata *clkdata = to_tps68470_clkdata(hw);

	regmap_write(clkdata->regmap, TPS68470_REG_CLKCFG1,
			   (TPS68470_PLL_OUTPUT_ENABLE << TPS68470_OUTPUT_A_SHIFT) |
			   (TPS68470_PLL_OUTPUT_ENABLE << TPS68470_OUTPUT_B_SHIFT));

	regmap_update_bits(clkdata->regmap, TPS68470_REG_PLLCTL,
			   TPS68470_PLL_EN_MASK, TPS68470_PLL_EN_MASK);

	/*
	 * The PLLCTL reg lock bit is set by the PMIC after approx. 4ms and
	 * does not indicate a true lock, so just wait 4 ms.
	 */
	usleep_range(4000, 5000);

	return 0;
}

static void tps68470_clk_unprepare(struct clk_hw *hw)
{
	struct tps68470_clkdata *clkdata = to_tps68470_clkdata(hw);

	/* Disable clock first ... */
	regmap_update_bits(clkdata->regmap, TPS68470_REG_PLLCTL, TPS68470_PLL_EN_MASK, 0);

	/* ... and then tri-state the clock outputs. */
	regmap_write(clkdata->regmap, TPS68470_REG_CLKCFG1, 0);
}

static unsigned long tps68470_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	struct tps68470_clkdata *clkdata = to_tps68470_clkdata(hw);

	return clkdata->rate;
}

/*
 * This returns the index of the clk_freqs[] cfg with the closest rate for
 * use in tps68470_clk_round_rate(). tps68470_clk_set_rate() checks that
 * the rate of the returned cfg is an exact match.
 */
static unsigned int tps68470_clk_cfg_lookup(unsigned long rate)
{
	long diff, best_diff = LONG_MAX;
	unsigned int i, best_idx = 0;

	for (i = 0; i < ARRAY_SIZE(clk_freqs); i++) {
		diff = clk_freqs[i].freq - rate;
		if (diff == 0)
			return i;

		diff = abs(diff);
		if (diff < best_diff) {
			best_diff = diff;
			best_idx = i;
		}
	}

	return best_idx;
}

static long tps68470_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *parent_rate)
{
	unsigned int idx = tps68470_clk_cfg_lookup(rate);

	return clk_freqs[idx].freq;
}

static int tps68470_clk_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct tps68470_clkdata *clkdata = to_tps68470_clkdata(hw);
	unsigned int idx = tps68470_clk_cfg_lookup(rate);

	if (rate != clk_freqs[idx].freq)
		return -EINVAL;

	regmap_write(clkdata->regmap, TPS68470_REG_BOOSTDIV, clk_freqs[idx].boostdiv);
	regmap_write(clkdata->regmap, TPS68470_REG_BUCKDIV, clk_freqs[idx].buckdiv);
	regmap_write(clkdata->regmap, TPS68470_REG_PLLSWR, TPS68470_PLLSWR_DEFAULT);
	regmap_write(clkdata->regmap, TPS68470_REG_XTALDIV, clk_freqs[idx].xtaldiv);
	regmap_write(clkdata->regmap, TPS68470_REG_PLLDIV, clk_freqs[idx].plldiv);
	regmap_write(clkdata->regmap, TPS68470_REG_POSTDIV, clk_freqs[idx].postdiv);
	regmap_write(clkdata->regmap, TPS68470_REG_POSTDIV2, clk_freqs[idx].postdiv);
	regmap_write(clkdata->regmap, TPS68470_REG_CLKCFG2, TPS68470_CLKCFG2_DRV_STR_2MA);

	regmap_write(clkdata->regmap, TPS68470_REG_PLLCTL,
		     TPS68470_OSC_EXT_CAP_DEFAULT << TPS68470_OSC_EXT_CAP_SHIFT |
		     TPS68470_CLK_SRC_XTAL << TPS68470_CLK_SRC_SHIFT);

	clkdata->rate = rate;

	return 0;
}

static const struct clk_ops tps68470_clk_ops = {
	.is_prepared = tps68470_clk_is_prepared,
	.prepare = tps68470_clk_prepare,
	.unprepare = tps68470_clk_unprepare,
	.recalc_rate = tps68470_clk_recalc_rate,
	.round_rate = tps68470_clk_round_rate,
	.set_rate = tps68470_clk_set_rate,
};

static int tps68470_clk_probe(struct platform_device *pdev)
{
	struct tps68470_clk_platform_data *pdata = pdev->dev.platform_data;
	struct clk_init_data tps68470_clk_initdata = {
		.name = TPS68470_CLK_NAME,
		.ops = &tps68470_clk_ops,
		/* Changing the dividers when the PLL is on is not allowed */
		.flags = CLK_SET_RATE_GATE,
	};
	struct tps68470_clkdata *tps68470_clkdata;
	struct tps68470_clk_consumer *consumer;
	int ret;
	int i;

	tps68470_clkdata = devm_kzalloc(&pdev->dev, sizeof(*tps68470_clkdata),
					GFP_KERNEL);
	if (!tps68470_clkdata)
		return -ENOMEM;

	tps68470_clkdata->regmap = dev_get_drvdata(pdev->dev.parent);
	tps68470_clkdata->clkout_hw.init = &tps68470_clk_initdata;

	/* Set initial rate */
	tps68470_clk_set_rate(&tps68470_clkdata->clkout_hw, clk_freqs[0].freq, 0);

	ret = devm_clk_hw_register(&pdev->dev, &tps68470_clkdata->clkout_hw);
	if (ret)
		return ret;

	ret = devm_clk_hw_register_clkdev(&pdev->dev, &tps68470_clkdata->clkout_hw,
					  TPS68470_CLK_NAME, NULL);
	if (ret)
		return ret;

	if (pdata) {
		for (i = 0; i < pdata->n_consumers; i++) {
			consumer = &pdata->consumers[i];
			ret = devm_clk_hw_register_clkdev(&pdev->dev,
							  &tps68470_clkdata->clkout_hw,
							  consumer->consumer_con_id,
							  consumer->consumer_dev_name);
		}
	}

	return ret;
}

static struct platform_driver tps68470_clk_driver = {
	.driver = {
		.name = TPS68470_CLK_NAME,
	},
	.probe = tps68470_clk_probe,
};

/*
 * The ACPI tps68470 probe-ordering depends on the clk/gpio/regulator drivers
 * registering before the drivers for the camera-sensors which use them bind.
 * subsys_initcall() ensures this when the drivers are builtin.
 */
static int __init tps68470_clk_init(void)
{
	return platform_driver_register(&tps68470_clk_driver);
}
subsys_initcall(tps68470_clk_init);

static void __exit tps68470_clk_exit(void)
{
	platform_driver_unregister(&tps68470_clk_driver);
}
module_exit(tps68470_clk_exit);

MODULE_ALIAS("platform:tps68470-clk");
MODULE_DESCRIPTION("clock driver for TPS68470 pmic");
MODULE_LICENSE("GPL");
