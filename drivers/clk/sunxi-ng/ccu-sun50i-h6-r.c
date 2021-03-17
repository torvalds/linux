// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2017 Icenowy Zheng <icenowy@aosc.xyz>
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

#include "ccu-sun50i-h6-r.h"

/*
 * Information about AR100 and AHB/APB clocks in R_CCU are gathered from
 * clock definitions in the BSP source code.
 */

static const char * const ar100_r_apb2_parents[] = { "osc24M", "osc32k",
					     "pll-periph0", "iosc" };
static const struct ccu_mux_var_prediv ar100_r_apb2_predivs[] = {
	{ .index = 2, .shift = 0, .width = 5 },
};

static struct ccu_div ar100_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),

	.mux		= {
		.shift	= 24,
		.width	= 2,

		.var_predivs	= ar100_r_apb2_predivs,
		.n_var_predivs	= ARRAY_SIZE(ar100_r_apb2_predivs),
	},

	.common		= {
		.reg		= 0x000,
		.features	= CCU_FEATURE_VARIABLE_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("ar100",
						      ar100_r_apb2_parents,
						      &ccu_div_ops,
						      0),
	},
};

static CLK_FIXED_FACTOR(r_ahb_clk, "r-ahb", "ar100", 1, 1, 0);

static struct ccu_div r_apb1_clk = {
	.div		= _SUNXI_CCU_DIV(0, 2),

	.common		= {
		.reg		= 0x00c,
		.hw.init	= CLK_HW_INIT("r-apb1",
					      "r-ahb",
					      &ccu_div_ops,
					      0),
	},
};

static struct ccu_div r_apb2_clk = {
	.div		= _SUNXI_CCU_DIV_FLAGS(8, 2, CLK_DIVIDER_POWER_OF_TWO),

	.mux		= {
		.shift	= 24,
		.width	= 2,

		.var_predivs	= ar100_r_apb2_predivs,
		.n_var_predivs	= ARRAY_SIZE(ar100_r_apb2_predivs),
	},

	.common		= {
		.reg		= 0x010,
		.features	= CCU_FEATURE_VARIABLE_PREDIV,
		.hw.init	= CLK_HW_INIT_PARENTS("r-apb2",
						      ar100_r_apb2_parents,
						      &ccu_div_ops,
						      0),
	},
};

/*
 * Information about the gate/resets are gathered from the clock header file
 * in the BSP source code, although most of them are unused. The existence
 * of the hardware block is verified with "3.1 Memory Mapping" chapter in
 * "Allwinner H6 V200 User Manual V1.1"; and the parent APB buses are verified
 * with "3.3.2.1 System Bus Tree" chapter inthe same document.
 */
static SUNXI_CCU_GATE(r_apb1_timer_clk,	"r-apb1-timer",	"r-apb1",
		      0x11c, BIT(0), 0);
static SUNXI_CCU_GATE(r_apb1_twd_clk,	"r-apb1-twd",	"r-apb1",
		      0x12c, BIT(0), 0);
static SUNXI_CCU_GATE(r_apb1_pwm_clk,	"r-apb1-pwm",	"r-apb1",
		      0x13c, BIT(0), 0);
static SUNXI_CCU_GATE(r_apb2_uart_clk,	"r-apb2-uart",	"r-apb2",
		      0x18c, BIT(0), 0);
static SUNXI_CCU_GATE(r_apb2_i2c_clk,	"r-apb2-i2c",	"r-apb2",
		      0x19c, BIT(0), 0);
static SUNXI_CCU_GATE(r_apb1_ir_clk,	"r-apb1-ir",	"r-apb1",
		      0x1cc, BIT(0), 0);
static SUNXI_CCU_GATE(r_apb1_w1_clk,	"r-apb1-w1",	"r-apb1",
		      0x1cc, BIT(0), 0);

/* Information of IR(RX) mod clock is gathered from BSP source code */
static const char * const r_mod0_default_parents[] = { "osc32k", "osc24M" };
static SUNXI_CCU_MP_WITH_MUX_GATE(ir_clk, "ir",
				  r_mod0_default_parents, 0x1c0,
				  0, 5,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);

/*
 * BSP didn't use the 1-wire function at all now, and the information about
 * this mod clock is guessed from the IR mod clock above. The existence of
 * this mod clock is proven by BSP clock header, and the dividers are verified
 * by contents in the 1-wire related chapter of the User Manual.
 */

static SUNXI_CCU_MP_WITH_MUX_GATE(w1_clk, "w1",
				  r_mod0_default_parents, 0x1e0,
				  0, 5,		/* M */
				  8, 2,		/* P */
				  24, 1,	/* mux */
				  BIT(31),	/* gate */
				  0);

static struct ccu_common *sun50i_h6_r_ccu_clks[] = {
	&ar100_clk.common,
	&r_apb1_clk.common,
	&r_apb2_clk.common,
	&r_apb1_timer_clk.common,
	&r_apb1_twd_clk.common,
	&r_apb1_pwm_clk.common,
	&r_apb2_uart_clk.common,
	&r_apb2_i2c_clk.common,
	&r_apb1_ir_clk.common,
	&r_apb1_w1_clk.common,
	&ir_clk.common,
	&w1_clk.common,
};

static struct clk_hw_onecell_data sun50i_h6_r_hw_clks = {
	.hws	= {
		[CLK_AR100]		= &ar100_clk.common.hw,
		[CLK_R_AHB]		= &r_ahb_clk.hw,
		[CLK_R_APB1]		= &r_apb1_clk.common.hw,
		[CLK_R_APB2]		= &r_apb2_clk.common.hw,
		[CLK_R_APB1_TIMER]	= &r_apb1_timer_clk.common.hw,
		[CLK_R_APB1_TWD]	= &r_apb1_twd_clk.common.hw,
		[CLK_R_APB1_PWM]	= &r_apb1_pwm_clk.common.hw,
		[CLK_R_APB2_UART]	= &r_apb2_uart_clk.common.hw,
		[CLK_R_APB2_I2C]	= &r_apb2_i2c_clk.common.hw,
		[CLK_R_APB1_IR]		= &r_apb1_ir_clk.common.hw,
		[CLK_R_APB1_W1]		= &r_apb1_w1_clk.common.hw,
		[CLK_IR]		= &ir_clk.common.hw,
		[CLK_W1]		= &w1_clk.common.hw,
	},
	.num	= CLK_NUMBER,
};

static struct ccu_reset_map sun50i_h6_r_ccu_resets[] = {
	[RST_R_APB1_TIMER]	=  { 0x11c, BIT(16) },
	[RST_R_APB1_TWD]	=  { 0x12c, BIT(16) },
	[RST_R_APB1_PWM]	=  { 0x13c, BIT(16) },
	[RST_R_APB2_UART]	=  { 0x18c, BIT(16) },
	[RST_R_APB2_I2C]	=  { 0x19c, BIT(16) },
	[RST_R_APB1_IR]		=  { 0x1cc, BIT(16) },
	[RST_R_APB1_W1]		=  { 0x1ec, BIT(16) },
};

static const struct sunxi_ccu_desc sun50i_h6_r_ccu_desc = {
	.ccu_clks	= sun50i_h6_r_ccu_clks,
	.num_ccu_clks	= ARRAY_SIZE(sun50i_h6_r_ccu_clks),

	.hw_clks	= &sun50i_h6_r_hw_clks,

	.resets		= sun50i_h6_r_ccu_resets,
	.num_resets	= ARRAY_SIZE(sun50i_h6_r_ccu_resets),
};

static void __init sunxi_r_ccu_init(struct device_node *node,
				    const struct sunxi_ccu_desc *desc)
{
	void __iomem *reg;

	reg = of_io_request_and_map(node, 0, of_node_full_name(node));
	if (IS_ERR(reg)) {
		pr_err("%pOF: Could not map the clock registers\n", node);
		return;
	}

	sunxi_ccu_probe(node, reg, desc);
}

static void __init sun50i_h6_r_ccu_setup(struct device_node *node)
{
	sunxi_r_ccu_init(node, &sun50i_h6_r_ccu_desc);
}
CLK_OF_DECLARE(sun50i_h6_r_ccu, "allwinner,sun50i-h6-r-ccu",
	       sun50i_h6_r_ccu_setup);
