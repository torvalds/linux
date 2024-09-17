// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 huangzhenwei@allwinnertech.com
 * Copyright (C) 2021 Samuel Holland <samuel@sholland.org>
 */

#include <linux/clk-provider.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_gate.h"
#include "ccu_mp.h"

#include "ccu-sun20i-d1-r.h"

static const struct clk_parent_data r_ahb_apb0_parents[] = {
	{ .fw_name = "hosc" },
	{ .fw_name = "losc" },
	{ .fw_name = "iosc" },
	{ .fw_name = "pll-periph" },
};
static SUNXI_CCU_MP_DATA_WITH_MUX(r_ahb_clk, "r-ahb",
				  r_ahb_apb0_parents, 0x000,
				  0, 5,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  0);
static const struct clk_hw *r_ahb_hw = &r_ahb_clk.common.hw;

static SUNXI_CCU_MP_DATA_WITH_MUX(r_apb0_clk, "r-apb0",
				  r_ahb_apb0_parents, 0x00c,
				  0, 5,		/* M */
				  8, 2,		/* P */
				  24, 3,	/* mux */
				  0);
static const struct clk_hw *r_apb0_hw = &r_apb0_clk.common.hw;

static SUNXI_CCU_GATE_HWS(bus_r_timer_clk,	"bus-r-timer",	&r_apb0_hw,
			  0x11c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_r_twd_clk,	"bus-r-twd",	&r_apb0_hw,
			  0x12c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_r_ppu_clk,	"bus-r-ppu",	&r_apb0_hw,
			  0x1ac, BIT(0), 0);

static const struct clk_parent_data r_ir_rx_parents[] = {
	{ .fw_name = "losc" },
	{ .fw_name = "hosc" },
};
static SUNXI_CCU_MP_DATA_WITH_MUX_GATE(r_ir_rx_clk, "r-ir-rx",
				       r_ir_rx_parents, 0x1c0,
				       0, 5,	/* M */
				       8, 2,	/* P */
				       24, 2,	/* mux */
				       BIT(31),	/* gate */
				       0);

static SUNXI_CCU_GATE_HWS(bus_r_ir_rx_clk,	"bus-r-ir-rx",	&r_apb0_hw,
			  0x1cc, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_r_rtc_clk,	"bus-r-rtc",	&r_ahb_hw,
			  0x20c, BIT(0), 0);
static SUNXI_CCU_GATE_HWS(bus_r_cpucfg_clk,	"bus-r-cpucfg",	&r_apb0_hw,
			  0x22c, BIT(0), 0);

static struct ccu_common *sun20i_d1_r_ccu_clks[] = {
	&r_ahb_clk.common,
	&r_apb0_clk.common,
	&bus_r_timer_clk.common,
	&bus_r_twd_clk.common,
	&bus_r_ppu_clk.common,
	&r_ir_rx_clk.common,
	&bus_r_ir_rx_clk.common,
	&bus_r_rtc_clk.common,
	&bus_r_cpucfg_clk.common,
};

static struct clk_hw_onecell_data sun20i_d1_r_hw_clks = {
	.num	= CLK_NUMBER,
	.hws	= {
		[CLK_R_AHB]		= &r_ahb_clk.common.hw,
		[CLK_R_APB0]		= &r_apb0_clk.common.hw,
		[CLK_BUS_R_TIMER]	= &bus_r_timer_clk.common.hw,
		[CLK_BUS_R_TWD]		= &bus_r_twd_clk.common.hw,
		[CLK_BUS_R_PPU]		= &bus_r_ppu_clk.common.hw,
		[CLK_R_IR_RX]		= &r_ir_rx_clk.common.hw,
		[CLK_BUS_R_IR_RX]	= &bus_r_ir_rx_clk.common.hw,
		[CLK_BUS_R_RTC]		= &bus_r_rtc_clk.common.hw,
		[CLK_BUS_R_CPUCFG]	= &bus_r_cpucfg_clk.common.hw,
	},
};

static struct ccu_reset_map sun20i_d1_r_ccu_resets[] = {
	[RST_BUS_R_TIMER]	= { 0x11c, BIT(16) },
	[RST_BUS_R_TWD]		= { 0x12c, BIT(16) },
	[RST_BUS_R_PPU]		= { 0x1ac, BIT(16) },
	[RST_BUS_R_IR_RX]	= { 0x1cc, BIT(16) },
	[RST_BUS_R_RTC]		= { 0x20c, BIT(16) },
	[RST_BUS_R_CPUCFG]	= { 0x22c, BIT(16) },
};

static const struct sunxi_ccu_desc sun20i_d1_r_ccu_desc = {
	.ccu_clks	= sun20i_d1_r_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun20i_d1_r_ccu_clks),

	.hw_clks	= &sun20i_d1_r_hw_clks,

	.resets		= sun20i_d1_r_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun20i_d1_r_ccu_resets),
};

static int sun20i_d1_r_ccu_probe(struct platform_device *pdev)
{
	void __iomem *reg;

	reg = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(reg))
		return PTR_ERR(reg);

	return devm_sunxi_ccu_probe(&pdev->dev, reg, &sun20i_d1_r_ccu_desc);
}

static const struct of_device_id sun20i_d1_r_ccu_ids[] = {
	{ .compatible = "allwinner,sun20i-d1-r-ccu" },
	{ }
};

static struct platform_driver sun20i_d1_r_ccu_driver = {
	.probe	= sun20i_d1_r_ccu_probe,
	.driver	= {
		.name			= "sun20i-d1-r-ccu",
		.suppress_bind_attrs	= true,
		.of_match_table		= sun20i_d1_r_ccu_ids,
	},
};
module_platform_driver(sun20i_d1_r_ccu_driver);

MODULE_IMPORT_NS(SUNXI_CCU);
MODULE_LICENSE("GPL");
