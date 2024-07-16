// SPDX-License-Identifier: GPL-2.0+
/*
 * Amlogic Meson8 DDR clock controller
 *
 * Copyright (C) 2019 Martin Blumenstingl <martin.blumenstingl@googlemail.com>
 */

#include <dt-bindings/clock/meson8-ddr-clkc.h>

#include <linux/clk-provider.h>
#include <linux/platform_device.h>

#include "clk-regmap.h"
#include "clk-pll.h"

#define AM_DDR_PLL_CNTL			0x00
#define AM_DDR_PLL_CNTL1		0x04
#define AM_DDR_PLL_CNTL2		0x08
#define AM_DDR_PLL_CNTL3		0x0c
#define AM_DDR_PLL_CNTL4		0x10
#define AM_DDR_PLL_STS			0x14
#define DDR_CLK_CNTL			0x18
#define DDR_CLK_STS			0x1c

static struct clk_regmap meson8_ddr_pll_dco = {
	.data = &(struct meson_clk_pll_data){
		.en = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 30,
			.width   = 1,
		},
		.m = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 0,
			.width   = 9,
		},
		.n = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 9,
			.width   = 5,
		},
		.l = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 31,
			.width   = 1,
		},
		.rst = {
			.reg_off = AM_DDR_PLL_CNTL,
			.shift   = 29,
			.width   = 1,
		},
	},
	.hw.init = &(struct clk_init_data){
		.name = "ddr_pll_dco",
		.ops = &meson_clk_pll_ro_ops,
		.parent_data = &(const struct clk_parent_data) {
			.fw_name = "xtal",
		},
		.num_parents = 1,
	},
};

static struct clk_regmap meson8_ddr_pll = {
	.data = &(struct clk_regmap_div_data){
		.offset = AM_DDR_PLL_CNTL,
		.shift = 16,
		.width = 2,
		.flags = CLK_DIVIDER_POWER_OF_TWO,
	},
	.hw.init = &(struct clk_init_data){
		.name = "ddr_pll",
		.ops = &clk_regmap_divider_ro_ops,
		.parent_hws = (const struct clk_hw *[]) {
			&meson8_ddr_pll_dco.hw
		},
		.num_parents = 1,
	},
};

static struct clk_hw_onecell_data meson8_ddr_clk_hw_onecell_data = {
	.hws = {
		[DDR_CLKID_DDR_PLL_DCO]		= &meson8_ddr_pll_dco.hw,
		[DDR_CLKID_DDR_PLL]		= &meson8_ddr_pll.hw,
	},
	.num = 2,
};

static struct clk_regmap *const meson8_ddr_clk_regmaps[] = {
	&meson8_ddr_pll_dco,
	&meson8_ddr_pll,
};

static const struct regmap_config meson8_ddr_clkc_regmap_config = {
	.reg_bits = 8,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = DDR_CLK_STS,
};

static int meson8_ddr_clkc_probe(struct platform_device *pdev)
{
	struct regmap *regmap;
	void __iomem *base;
	struct clk_hw *hw;
	int ret, i;

	base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(base))
		return PTR_ERR(base);

	regmap = devm_regmap_init_mmio(&pdev->dev, base,
				       &meson8_ddr_clkc_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	/* Populate regmap */
	for (i = 0; i < ARRAY_SIZE(meson8_ddr_clk_regmaps); i++)
		meson8_ddr_clk_regmaps[i]->map = regmap;

	/* Register all clks */
	for (i = 0; i < meson8_ddr_clk_hw_onecell_data.num; i++) {
		hw = meson8_ddr_clk_hw_onecell_data.hws[i];

		ret = devm_clk_hw_register(&pdev->dev, hw);
		if (ret) {
			dev_err(&pdev->dev, "Clock registration failed\n");
			return ret;
		}
	}

	return devm_of_clk_add_hw_provider(&pdev->dev, of_clk_hw_onecell_get,
					   &meson8_ddr_clk_hw_onecell_data);
}

static const struct of_device_id meson8_ddr_clkc_match_table[] = {
	{ .compatible = "amlogic,meson8-ddr-clkc" },
	{ .compatible = "amlogic,meson8b-ddr-clkc" },
	{ /* sentinel */ }
};

static struct platform_driver meson8_ddr_clkc_driver = {
	.probe		= meson8_ddr_clkc_probe,
	.driver		= {
		.name	= "meson8-ddr-clkc",
		.of_match_table = meson8_ddr_clkc_match_table,
	},
};

builtin_platform_driver(meson8_ddr_clkc_driver);
