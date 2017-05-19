/*
 * Copyright (c) 2016 Icenowy Zheng <icenowy@aosc.xyz>
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

#include <linux/clk-provider.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>

#include "ccu_common.h"
#include "ccu_reset.h"

#include "ccu_div.h"
#include "ccu_gate.h"
#include "ccu_mp.h"
#include "ccu_nm.h"

#include "ccu-sun8i-r.h"

static const char * const ar100_parents[] = { "osc32k", "osc24M",
					     "pll-periph0", "iosc" };
static const struct ccu_mux_var_prediv ar100_predivs[] = {
	{ .index = 2, .shift = 8, .width = 5 },
};

static struct ccu_div ar100_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(4, 2, CLK_DIVIDER_POWER_OF_TWO),

	.mux		= {
		.shift	= 16,
		.width	= 2,

		.var_predivs	= ar100_predivs,
		.n_var_predivs	= ARRAY_SIZE(ar100_predivs),
	},

	.common		= {
		.reg		= 0x00,
		.features	= CCU_FEATURE_VARIABLE_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("ar100",
						      ar100_parents,
						      &ccu_div_ops,
						      0),
	},
};

static CLK_FIXED_FACTOR(ahb0_clk, "ahb0", "ar100", 1, 1, 0);

static struct ccu_div apb0_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(0, 2, CLK_DIVIDER_POWER_OF_TWO),

	.common		= {
		.reg		= 0x0c,
		.hw.init	= CLK_HW_INIT("apb0",
					      "ahb0",
					      &ccu_div_ops,
					      0),
	},
};

static SUNXI_CCU_GATE(apb0_pio_clk,	"apb0-pio",	"apb0",
		      0x28, BIT(0), 0);
static SUNXI_CCU_GATE(apb0_ir_clk,	"apb0-ir",	"apb0",
		      0x28, BIT(1), 0);
static SUNXI_CCU_GATE(apb0_timer_clk,	"apb0-timer",	"apb0",
		      0x28, BIT(2), 0);
static SUNXI_CCU_GATE(apb0_rsb_clk,	"apb0-rsb",	"apb0",
		      0x28, BIT(3), 0);
static SUNXI_CCU_GATE(apb0_uart_clk,	"apb0-uart",	"apb0",
		      0x28, BIT(4), 0);
static SUNXI_CCU_GATE(apb0_i2c_clk,	"apb0-i2c",	"apb0",
		      0x28, BIT(6), 0);
static SUNXI_CCU_GATE(apb0_twd_clk,	"apb0-twd",	"apb0",
		      0x28, BIT(7), 0);

static const char * const r_mod0_default_parents[] = { "osc32k", "osc24M" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ir_clk, "ir",
				  r_mod0_default_parents, 0x54,
				  0, 4,		/* M */
				  16, 2,	/* P */
				  24, 2,	/* mux */
				  BIT(31),	/* gate */
				  0);

static struct ccu_common *sun8i_h3_r_ccu_clks[] = {
	&ar100_clk.common,
	&apb0_clk.common,
	&apb0_pio_clk.common,
	&apb0_ir_clk.common,
	&apb0_timer_clk.common,
	&apb0_uart_clk.common,
	&apb0_i2c_clk.common,
	&apb0_twd_clk.common,
	&ir_clk.common,
};

static struct ccu_common *sun50i_a64_r_ccu_clks[] = {
	&ar100_clk.common,
	&apb0_clk.common,
	&apb0_pio_clk.common,
	&apb0_ir_clk.common,
	&apb0_timer_clk.common,
	&apb0_rsb_clk.common,
	&apb0_uart_clk.common,
	&apb0_i2c_clk.common,
	&apb0_twd_clk.common,
	&ir_clk.common,
};

static struct clk_hw_onecell_data sun8i_h3_r_hw_clks = {
	.hws	= {
		[CLK_AR100]		= &ar100_clk.common.hw,
		[CLK_AHB0]		= &ahb0_clk.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB0_PIO]		= &apb0_pio_clk.common.hw,
		[CLK_APB0_IR]		= &apb0_ir_clk.common.hw,
		[CLK_APB0_TIMER]	= &apb0_timer_clk.common.hw,
		[CLK_APB0_UART]		= &apb0_uart_clk.common.hw,
		[CLK_APB0_I2C]		= &apb0_i2c_clk.common.hw,
		[CLK_APB0_TWD]		= &apb0_twd_clk.common.hw,
		[CLK_IR]		= &ir_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct clk_hw_onecell_data sun50i_a64_r_hw_clks = {
	.hws	= {
		[CLK_AR100]		= &ar100_clk.common.hw,
		[CLK_AHB0]		= &ahb0_clk.hw,
		[CLK_APB0]		= &apb0_clk.common.hw,
		[CLK_APB0_PIO]		= &apb0_pio_clk.common.hw,
		[CLK_APB0_IR]		= &apb0_ir_clk.common.hw,
		[CLK_APB0_TIMER]	= &apb0_timer_clk.common.hw,
		[CLK_APB0_RSB]		= &apb0_rsb_clk.common.hw,
		[CLK_APB0_UART]		= &apb0_uart_clk.common.hw,
		[CLK_APB0_I2C]		= &apb0_i2c_clk.common.hw,
		[CLK_APB0_TWD]		= &apb0_twd_clk.common.hw,
		[CLK_IR]		= &ir_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun8i_h3_r_ccu_resets[] = {
	[RST_APB0_IR]		=  { 0xb0, BIT(1) },
	[RST_APB0_TIMER]	=  { 0xb0, BIT(2) },
	[RST_APB0_UART]		=  { 0xb0, BIT(4) },
	[RST_APB0_I2C]		=  { 0xb0, BIT(6) },
};

static struct ccu_reset_map sun50i_a64_r_ccu_resets[] = {
	[RST_APB0_IR]		=  { 0xb0, BIT(1) },
	[RST_APB0_TIMER]	=  { 0xb0, BIT(2) },
	[RST_APB0_RSB]		=  { 0xb0, BIT(3) },
	[RST_APB0_UART]		=  { 0xb0, BIT(4) },
	[RST_APB0_I2C]		=  { 0xb0, BIT(6) },
};

static const struct sunxi_ccu_desc sun8i_h3_r_ccu_desc = {
	.ccu_clks	= sun8i_h3_r_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun8i_h3_r_ccu_clks),

	.hw_clks	= &sun8i_h3_r_hw_clks,

	.resets		= sun8i_h3_r_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun8i_h3_r_ccu_resets),
};

static const struct sunxi_ccu_desc sun50i_a64_r_ccu_desc = {
	.ccu_clks	= sun50i_a64_r_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun50i_a64_r_ccu_clks),

	.hw_clks	= &sun50i_a64_r_hw_clks,

	.resets		= sun50i_a64_r_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun50i_a64_r_ccu_resets),
};

static void __init sunxi_r_ccu_init(struct device_node *node,
				    const struct sunxi_ccu_desc *desc)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%s: Could not map the clock registers\n",
		       of_node_full_name(node));
		return;
	}

	sunxi_ccu_probe(node, reg, desc);
}

static void __init sun8i_h3_r_ccu_setup(struct device_node *node)
{
	sunxi_r_ccu_init(node, &sun8i_h3_r_ccu_desc);
}
CLK_OF_DECLARE(sun8i_h3_r_ccu, "allwinner,sun8i-h3-r-ccu",
	       sun8i_h3_r_ccu_setup);

static void __init sun50i_a64_r_ccu_setup(struct device_node *node)
{
	sunxi_r_ccu_init(node, &sun50i_a64_r_ccu_desc);
}
CLK_OF_DECLARE(sun50i_a64_r_ccu, "allwinner,sun50i-a64-r-ccu",
	       sun50i_a64_r_ccu_setup);
