// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2018, The Linux Foundation. All rights reserved.
#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "clk-alpha-pll.h"

/*
 * Even though APSS PLL type is of existing one (like Huayra), its offsets
 * are different from the one mentioned in the clk-alpha-pll.c, since the
 * PLL is specific to APSS, so lets the define the same.
 */
static const u8 ipq_pll_offsets[][PLL_OFF_MAX_REGS] = {
	[CLK_ALPHA_PLL_TYPE_HUAYRA] =  {
		[PLL_OFF_L_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL] = 0x10,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_CONFIG_CTL] = 0x20,
		[PLL_OFF_CONFIG_CTL_U] = 0x24,
		[PLL_OFF_STATUS] = 0x28,
		[PLL_OFF_TEST_CTL] = 0x30,
		[PLL_OFF_TEST_CTL_U] = 0x34,
	},
	[CLK_ALPHA_PLL_TYPE_STROMER_PLUS] = {
		[PLL_OFF_L_VAL] = 0x08,
		[PLL_OFF_ALPHA_VAL] = 0x10,
		[PLL_OFF_ALPHA_VAL_U] = 0x14,
		[PLL_OFF_USER_CTL] = 0x18,
		[PLL_OFF_USER_CTL_U] = 0x1c,
		[PLL_OFF_CONFIG_CTL] = 0x20,
		[PLL_OFF_STATUS] = 0x28,
		[PLL_OFF_TEST_CTL] = 0x30,
		[PLL_OFF_TEST_CTL_U] = 0x34,
	},
};

static struct clk_alpha_pll ipq_pll_huayra = {
	.offset = 0x0,
	.regs = ipq_pll_offsets[CLK_ALPHA_PLL_TYPE_HUAYRA],
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

static struct clk_alpha_pll ipq_pll_stromer_plus = {
	.offset = 0x0,
	.regs = ipq_pll_offsets[CLK_ALPHA_PLL_TYPE_STROMER_PLUS],
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
			.ops = &clk_alpha_pll_stromer_ops,
		},
	},
};

static const struct alpha_pll_config ipq5332_pll_config = {
	.l = 0x3e,
	.config_ctl_val = 0x4001075b,
	.config_ctl_hi_val = 0x304,
	.main_output_mask = BIT(0),
	.aux_output_mask = BIT(1),
	.early_output_mask = BIT(3),
	.alpha_en_mask = BIT(24),
	.status_val = 0x3,
	.status_mask = GENMASK(10, 8),
	.lock_det = BIT(2),
	.test_ctl_hi_val = 0x00400003,
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

static const struct alpha_pll_config ipq9574_pll_config = {
	.l = 0x3b,
	.config_ctl_val = 0x200d4828,
	.config_ctl_hi_val = 0x6,
	.early_output_mask = BIT(3),
	.aux2_output_mask = BIT(2),
	.aux_output_mask = BIT(1),
	.main_output_mask = BIT(0),
	.test_ctl_val = 0x0,
	.test_ctl_hi_val = 0x4000,
};

struct apss_pll_data {
	int pll_type;
	struct clk_alpha_pll *pll;
	const struct alpha_pll_config *pll_config;
};

static struct apss_pll_data ipq5332_pll_data = {
	.pll_type = CLK_ALPHA_PLL_TYPE_STROMER_PLUS,
	.pll = &ipq_pll_stromer_plus,
	.pll_config = &ipq5332_pll_config,
};

static struct apss_pll_data ipq8074_pll_data = {
	.pll_type = CLK_ALPHA_PLL_TYPE_HUAYRA,
	.pll = &ipq_pll_huayra,
	.pll_config = &ipq8074_pll_config,
};

static struct apss_pll_data ipq6018_pll_data = {
	.pll_type = CLK_ALPHA_PLL_TYPE_HUAYRA,
	.pll = &ipq_pll_huayra,
	.pll_config = &ipq6018_pll_config,
};

static struct apss_pll_data ipq9574_pll_data = {
	.pll_type = CLK_ALPHA_PLL_TYPE_HUAYRA,
	.pll = &ipq_pll_huayra,
	.pll_config = &ipq9574_pll_config,
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
	const struct apss_pll_data *data;
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

	data = of_device_get_match_data(&pdev->dev);
	if (!data)
		return -ENODEV;

	if (data->pll_type == CLK_ALPHA_PLL_TYPE_HUAYRA)
		clk_alpha_pll_configure(data->pll, regmap, data->pll_config);
	else if (data->pll_type == CLK_ALPHA_PLL_TYPE_STROMER_PLUS)
		clk_stromer_pll_configure(data->pll, regmap, data->pll_config);

	ret = devm_clk_register_regmap(dev, &data->pll->clkr);
	if (ret)
		return ret;

	return devm_of_clk_add_hw_provider(dev, of_clk_hw_simple_get,
					   &data->pll->clkr.hw);
}

static const struct of_device_id apss_ipq_pll_match_table[] = {
	{ .compatible = "qcom,ipq5332-a53pll", .data = &ipq5332_pll_data },
	{ .compatible = "qcom,ipq6018-a53pll", .data = &ipq6018_pll_data },
	{ .compatible = "qcom,ipq8074-a53pll", .data = &ipq8074_pll_data },
	{ .compatible = "qcom,ipq9574-a73pll", .data = &ipq9574_pll_data },
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
