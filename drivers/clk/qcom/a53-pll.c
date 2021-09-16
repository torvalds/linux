// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm A53 PLL driver
 *
 * Copyright (c) 2017, Linaro Limited
 * Author: Georgi Djakov <georgi.djakov@linaro.org>
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/platform_device.h>
#include <linux/pm_opp.h>
#include <linux/regmap.h>
#include <linux/module.h>

#include "clk-pll.h"
#include "clk-regmap.h"

static const struct pll_freq_tbl a53pll_freq[] = {
	{  998400000, 52, 0x0, 0x1, 0 },
	{ 1094400000, 57, 0x0, 0x1, 0 },
	{ 1152000000, 62, 0x0, 0x1, 0 },
	{ 1209600000, 63, 0x0, 0x1, 0 },
	{ 1248000000, 65, 0x0, 0x1, 0 },
	{ 1363200000, 71, 0x0, 0x1, 0 },
	{ 1401600000, 73, 0x0, 0x1, 0 },
	{ }
};

static const struct regmap_config a53pll_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x40,
	.fast_io		= true,
};

static struct pll_freq_tbl *qcom_a53pll_get_freq_tbl(struct device *dev)
{
	struct pll_freq_tbl *freq_tbl;
	unsigned long xo_freq;
	unsigned long freq;
	struct clk *xo_clk;
	int count;
	int ret;
	int i;

	xo_clk = devm_clk_get(dev, "xo");
	if (IS_ERR(xo_clk))
		return NULL;

	xo_freq = clk_get_rate(xo_clk);

	ret = devm_pm_opp_of_add_table(dev);
	if (ret)
		return NULL;

	count = dev_pm_opp_get_opp_count(dev);
	if (count <= 0)
		return NULL;

	freq_tbl = devm_kcalloc(dev, count + 1, sizeof(*freq_tbl), GFP_KERNEL);
	if (!freq_tbl)
		return NULL;

	for (i = 0, freq = 0; i < count; i++, freq++) {
		struct dev_pm_opp *opp;

		opp = dev_pm_opp_find_freq_ceil(dev, &freq);
		if (IS_ERR(opp))
			return NULL;

		/* Skip the freq that is not divisible */
		if (freq % xo_freq)
			continue;

		freq_tbl[i].freq = freq;
		freq_tbl[i].l = freq / xo_freq;
		freq_tbl[i].n = 1;

		dev_pm_opp_put(opp);
	}

	return freq_tbl;
}

static int qcom_a53pll_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regmap *regmap;
	struct resource *res;
	struct clk_pll *pll;
	void __iomem *base;
	struct clk_init_data init = { };
	int ret;

	pll = devm_kzalloc(dev, sizeof(*pll), GFP_KERNEL);
	if (!pll)
		return -ENOMEM;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	base = devm_ioremap_resource(dev, res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &a53pll_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	pll->l_reg = 0x04;
	pll->m_reg = 0x08;
	pll->n_reg = 0x0c;
	pll->config_reg = 0x14;
	pll->mode_reg = 0x00;
	pll->status_reg = 0x1c;
	pll->status_bit = 16;

	pll->freq_tbl = qcom_a53pll_get_freq_tbl(dev);
	if (!pll->freq_tbl) {
		/* Fall on a53pll_freq if no freq_tbl is found from OPP */
		pll->freq_tbl = a53pll_freq;
	}

	/* Use an unique name by appending @unit-address */
	init.name = devm_kasprintf(dev, GFP_KERNEL, "a53pll%s",
				   strchrnul(np->full_name, '@'));
	if (!init.name)
		return -ENOMEM;

	init.parent_names = (const char *[]){ "xo" };
	init.num_parents = 1;
	init.ops = &clk_pll_sr2_ops;
	pll->clkr.hw.init = &init;

	ret = devm_clk_register_regmap(dev, &pll->clkr);
	if (ret) {
		dev_err(dev, "failed to register regmap clock: %d\n", ret);
		return ret;
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					  &pll->clkr.hw);
	if (ret) {
		dev_err(dev, "failed to add clock provider: %d\n", ret);
		return ret;
	}

	return 0;
}

static const struct of_device_id qcom_a53pll_match_table[] = {
	{ .compatible = "qcom,msm8916-a53pll" },
	{ .compatible = "qcom,msm8939-a53pll" },
	{ }
};
MODULE_DEVICE_TABLE(of, qcom_a53pll_match_table);

static struct platform_driver qcom_a53pll_driver = {
	.probe = qcom_a53pll_probe,
	.driver = {
		.name = "qcom-a53pll",
		.of_match_table = qcom_a53pll_match_table,
	},
};
module_platform_driver(qcom_a53pll_driver);

MODULE_DESCRIPTION("Qualcomm A53 PLL Driver");
MODULE_LICENSE("GPL v2");
