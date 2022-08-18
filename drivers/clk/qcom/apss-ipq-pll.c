// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clk-alpha-pll.h"

static const u8 ipq_pll_offsets[] = {
	[PLL_OFF_L_VAL] = 0x08,
	[PLL_OFF_ALPHA_VAL] = 0x10,
	[PLL_OFF_USER_CTL] = 0x18,
	[PLL_OFF_CONFIG_CTL] = 0x20,
	[PLL_OFF_CONFIG_CTL_U] = 0x24,
	[PLL_OFF_STATUS] = 0x28,
	[PLL_OFF_TEST_CTL] = 0x30,
	[PLL_OFF_TEST_CTL_U] = 0x34,
};

static struct clk_alpha_pll ipq_pll = {
	.offset = 0x0,
	.regs = ipq_pll_offsets,
	.flags = SUPPORTS_DYNAMIC_UPDATE,
	.clkr = {
		.enable_reg = 0x0,
		.enable_mask = BIT(0),
		.hw.init = &(struct clk_init_data){
			.name = "a53pll",
			.parent_data = &(const struct clk_parent_data) {
				.fw_name = "xo",
			},
			.num_parents = 1,
			.ops = &clk_alpha_pll_huayra_ops,
		},
	},
};

static const struct alpha_pll_config ipq6018_pll_config = {
	.l = 0x37,
	.config_ctl_val = 0x240d4828,
	.config_ctl_hi_val = 0x6,
	.early_output_mask = BIT(3),
	.aux2_output_mask = BIT(2),
	.aux_output_mask = BIT(1),
	.main_output_mask = BIT(0),
	.test_ctl_val = 0x1c0000C0,
	.test_ctl_hi_val = 0x4000,
};

static const struct alpha_pll_config ipq8074_pll_config = {
	.l = 0x48,
	.config_ctl_val = 0x200d4828,
	.config_ctl_hi_val = 0x6,
	.early_output_mask = BIT(3),
	.aux2_output_mask = BIT(2),
	.aux_output_mask = BIT(1),
	.main_output_mask = BIT(0),
	.test_ctl_val = 0x1c000000,
	.test_ctl_hi_val = 0x4000,
};

static const struct regmap_config ipq_pll_regmap_config = {
	.reg_bits		= 32,
	.reg_stride		= 4,
	.val_bits		= 32,
	.max_register		= 0x40,
	.fast_io		= true,
};

static int apss_ipq_pll_probe(struct platform_device *pdev)
{
	const struct alpha_pll_config *ipq_pll_config;
	struct device *dev = &pdev->dev;
	struct regmap *regmap;
	void __iomem *base;
	int ret;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(dev, base, &ipq_pll_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ipq_pll_config = of_device_get_match_data(&pdev->dev);
	if (!ipq_pll_config)
		return -ENODEV;

	clk_alpha_pll_configure(&ipq_pll, regmap, ipq_pll_config);

	ret = devm_clk_register_regmap(dev, &ipq_pll.clkr);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &ipq_pll.clkr.hw);
}

static const struct of_device_id apss_ipq_pll_match_table[] = {
	{ .compatible = "qcom,ipq6018-a53pll", .data = &ipq6018_pll_config },
	{ .compatible = "qcom,ipq8074-a53pll", .data = &ipq8074_pll_config },
	{ }
};
MODULE_DEVICE_TABLE(of, apss_ipq_pll_match_table);

static struct platform_driver apss_ipq_pll_driver = {
	.probe = apss_ipq_pll_probe,
	.driver = {
		.name = "qcom-ipq-apss-pll",
		.of_match_table = apss_ipq_pll_match_table,
	},
};
module_platform_driver(apss_ipq_pll_driver);

MODULE_DESCRIPTION("Qualcomm technology Inc APSS ALPHA PLL Driver");
MODULE_LICENSE("GPL v2");
