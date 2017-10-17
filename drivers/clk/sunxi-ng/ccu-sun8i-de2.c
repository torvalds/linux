/*
 * Copyright (c) 2017 Icenowy Zheng <icenowy@aosc.io>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/reset.h>

#include "ccu_common.h"
#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_reset.h"

#include "ccu-sun8i-de2.h"

static SUNXI_CCU_GATE(bus_mixer0_clk,	"bus-mixer0",	"bus-de",
		      0x04, BIT(0), 0);
static SUNXI_CCU_GATE(bus_mixer1_clk,	"bus-mixer1",	"bus-de",
		      0x04, BIT(1), 0);
static SUNXI_CCU_GATE(bus_wb_clk,	"bus-wb",	"bus-de",
		      0x04, BIT(2), 0);

static SUNXI_CCU_GATE(mixer0_clk,	"mixer0",	"mixer0-div",
		      0x00, BIT(0), CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(mixer1_clk,	"mixer1",	"mixer1-div",
		      0x00, BIT(1), CLK_SET_RATE_PARENT);
static SUNXI_CCU_GATE(wb_clk,		"wb",		"wb-div",
		      0x00, BIT(2), CLK_SET_RATE_PARENT);

static SUNXI_CCU_M(mixer0_div_clk, "mixer0-div", "de", 0x0c, 0, 4,
		   CLK_SET_RATE_PARENT);
static SUNXI_CCU_M(wb_div_clk, "wb-div", "de", 0x0c, 8, 4,
		   CLK_SET_RATE_PARENT);

static SUNXI_CCU_M(mixer0_div_a83_clk, "mixer0-div", "pll-de", 0x0c, 0, 4,
		   CLK_SET_RATE_PARENT);
static SUNXI_CCU_M(mixer1_div_a83_clk, "mixer1-div", "pll-de", 0x0c, 4, 4,
		   CLK_SET_RATE_PARENT);
static SUNXI_CCU_M(wb_div_a83_clk, "wb-div", "pll-de", 0x0c, 8, 4,
		   CLK_SET_RATE_PARENT);

static struct ccu_common *sun8i_a83t_de2_clks[] = {
	&mixer0_clk.common,
	&mixer1_clk.common,
	&wb_clk.common,

	&bus_mixer0_clk.common,
	&bus_mixer1_clk.common,
	&bus_wb_clk.common,

	&mixer0_div_a83_clk.common,
	&mixer1_div_a83_clk.common,
	&wb_div_a83_clk.common,
};

static struct ccu_common *sun8i_v3s_de2_clks[] = {
	&mixer0_clk.common,
	&wb_clk.common,

	&bus_mixer0_clk.common,
	&bus_wb_clk.common,

	&mixer0_div_clk.common,
	&wb_div_clk.common,
};

static struct clk_hw_onecell_data sun8i_a83t_de2_hw_clks = {
	.hws	= {
		[CLK_MIXER0]		= &mixer0_clk.common.hw,
		[CLK_MIXER1]		= &mixer1_clk.common.hw,
		[CLK_WB]		= &wb_clk.common.hw,

		[CLK_BUS_MIXER0]	= &bus_mixer0_clk.common.hw,
		[CLK_BUS_MIXER1]	= &bus_mixer1_clk.common.hw,
		[CLK_BUS_WB]		= &bus_wb_clk.common.hw,

		[CLK_MIXER0_DIV]	= &mixer0_div_a83_clk.common.hw,
		[CLK_MIXER1_DIV]	= &mixer1_div_a83_clk.common.hw,
		[CLK_WB_DIV]		= &wb_div_a83_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct clk_hw_onecell_data sun8i_v3s_de2_hw_clks = {
	.hws	= {
		[CLK_MIXER0]		= &mixer0_clk.common.hw,
		[CLK_WB]		= &wb_clk.common.hw,

		[CLK_BUS_MIXER0]	= &bus_mixer0_clk.common.hw,
		[CLK_BUS_WB]		= &bus_wb_clk.common.hw,

		[CLK_MIXER0_DIV]	= &mixer0_div_clk.common.hw,
		[CLK_WB_DIV]		= &wb_div_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun8i_a83t_de2_resets[] = {
	[RST_MIXER0]	= { 0x08, BIT(0) },
	/*
	 * For A83T, H3 and R40, mixer1 reset line is shared with wb, so
	 * only RST_WB is exported here.
	 * For V3s there's just no mixer1, so it also shares this struct.
	 */
	[RST_WB]	= { 0x08, BIT(2) },
};

static struct ccu_reset_map sun50i_a64_de2_resets[] = {
	[RST_MIXER0]	= { 0x08, BIT(0) },
	[RST_MIXER1]	= { 0x08, BIT(1) },
	[RST_WB]	= { 0x08, BIT(2) },
};

static const struct sunxi_ccu_desc sun8i_a83t_de2_clk_desc = {
	.ccu_clks	= sun8i_a83t_de2_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_a83t_de2_clks),

	.hw_clks	= &sun8i_a83t_de2_hw_clks,

	.resets		= sun8i_a83t_de2_resets,
	.num_resets	= ARRAY_SIZE(sun8i_a83t_de2_resets),
};

static const struct sunxi_ccu_desc sun50i_a64_de2_clk_desc = {
	.ccu_clks	= sun8i_a83t_de2_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_a83t_de2_clks),

	.hw_clks	= &sun8i_a83t_de2_hw_clks,

	.resets		= sun50i_a64_de2_resets,
	.num_resets	= ARRAY_SIZE(sun50i_a64_de2_resets),
};

static const struct sunxi_ccu_desc sun8i_v3s_de2_clk_desc = {
	.ccu_clks	= sun8i_v3s_de2_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_v3s_de2_clks),

	.hw_clks	= &sun8i_v3s_de2_hw_clks,

	.resets		= sun8i_a83t_de2_resets,
	.num_resets	= ARRAY_SIZE(sun8i_a83t_de2_resets),
};

static int sunxi_de2_clk_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct clk *bus_clk, *mod_clk;
	struct reset_control *rstc;
	void __iomem *reg;
	const struct sunxi_ccu_desc *ccu_desc;
	int ret;

	ccu_desc = of_device_get_match_data(&pdev->dev);
	if (!ccu_desc)
		return -EINVAL;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	reg = devm_ioremap_resource(&pdev->dev, res);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	bus_clk = devm_clk_get(&pdev->dev, "bus");
	if (IS_ERR(bus_clk)) {
		ret = PTR_ERR(bus_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Couldn't get bus clk: %d\n", ret);
		return ret;
	}

	mod_clk = devm_clk_get(&pdev->dev, "mod");
	if (IS_ERR(mod_clk)) {
		ret = PTR_ERR(mod_clk);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev, "Couldn't get mod clk: %d\n", ret);
		return ret;
	}

	rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(rstc)) {
		ret = PTR_ERR(rstc);
		if (ret != -EPROBE_DEFER)
			dev_err(&pdev->dev,
				"Couldn't get reset control: %d\n", ret);
		return ret;
	}

	/* The clocks need to be enabled for us to access the registers */
	ret = clk_prepare_enable(bus_clk);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't enable bus clk: %d\n", ret);
		return ret;
	}

	ret = clk_prepare_enable(mod_clk);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't enable mod clk: %d\n", ret);
		goto err_disable_bus_clk;
	}

	/* The reset control needs to be asserted for the controls to work */
	ret = reset_control_deassert(rstc);
	if (ret) {
		dev_err(&pdev->dev,
			"Couldn't deassert reset control: %d\n", ret);
		goto err_disable_mod_clk;
	}

	ret = sunxi_ccu_probe(pdev->dev.of_node, reg, ccu_desc);
	if (ret)
		goto err_assert_reset;

	return 0;

err_assert_reset:
	reset_control_assert(rstc);
err_disable_mod_clk:
	clk_disable_unprepare(mod_clk);
err_disable_bus_clk:
	clk_disable_unprepare(bus_clk);
	return ret;
}

static const struct of_device_id sunxi_de2_clk_ids[] = {
	{
		.compatible = "allwinner,sun8i-a83t-de2-clk",
		.data = &sun8i_a83t_de2_clk_desc,
	},
	{
		.compatible = "allwinner,sun8i-v3s-de2-clk",
		.data = &sun8i_v3s_de2_clk_desc,
	},
	{
		.compatible = "allwinner,sun50i-h5-de2-clk",
		.data = &sun50i_a64_de2_clk_desc,
	},
	/*
	 * The Allwinner A64 SoC needs some bit to be poke in syscon to make
	 * DE2 really working.
	 * So there's currently no A64 compatible here.
	 * H5 shares the same reset line with A64, so here H5 is using the
	 * clock description of A64.
	 */
	{ }
};

static struct platform_driver sunxi_de2_clk_driver = {
	.probe	= sunxi_de2_clk_probe,
	.driver	= {
		.name	= "sunxi-de2-clks",
		.of_match_table	= sunxi_de2_clk_ids,
	},
};
builtin_platform_driver(sunxi_de2_clk_driver);
