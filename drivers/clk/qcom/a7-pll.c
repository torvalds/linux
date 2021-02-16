// SPDX-License-Identifier: GPL-2.0
/*
 * Qualcomm A7 PLL driver
 *
 * Copyright (c) 2020, Linaro Limited
 * Author: Manivannan Sadhasivam <manivannan.sadhasivam@linaro.org>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clk-alpha-pll.h"

#define LUCID_PLL_OFF_L_VAL 0x04

static const struct pll_vco lucid_vco[] = {
	{ 249600000, 2000000000, 0 },
};

static struct clk_alpha_pll a7pll = {
	.offset = 0x100,
	.vco_table = lucid_vco,
	.num_vco = ARRAY_SIZE(lucid_vco),
	.regs = clk_alpha_pll_regs[CLK_ALPHA_PLL_TYPE_LUCID],
	.clkr = {
		.hw.init = &(struct clk_init_data){
			.name = "a7pll",
			.parent_data =  &(const struct clk_parent_data){
				.fw_name = "bi_tcxo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_lucid_ops,
		},
	},
};

static const struct alpha_pll_config a7pll_config = {
	.l = 0x39,
	.config_ctl_val = 0x20485699,
	.config_ctl_hi_val = 0x2261,
	.config_ctl_hi1_val = 0x029A699C,
	.user_ctl_val = 0x1,
	.user_ctl_hi_val = 0x805,
};

static const struct regmap_config a7pll_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x1000,
	.fast_io		= true,
};

static int qcom_a7pll_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	void __iomem *base;
	u32 l_val;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &a7pll_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Configure PLL only if the l_val is zero */
	regmap_read(regmap, a7pll.offset + LUCID_PLL_OFF_L_VAL, &l_val);
	if (!l_val)
		clk_lucid_pll_configure(&a7pll, regmap, &a7pll_config);

	ret = devm_clk_register_regmap(dev, &a7pll.clkr);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &a7pll.clkr.hw);
}

static const struct of_device_id qcom_a7pll_match_table[] = {
	{ .compatible = "qcom,sdx55-a7pll" },
	{ }
};

static struct platform_driver qcom_a7pll_driver = {
	.probe = qcom_a7pll_probe,
	.driver = {
		.name = "qcom-a7pll",
		.of_match_table = qcom_a7pll_match_table,
	},
};
module_platform_driver(qcom_a7pll_driver);

MODULE_DESCRIPTION("Qualcomm A7 PLL Driver");
MODULE_LICENSE("GPL v2");
