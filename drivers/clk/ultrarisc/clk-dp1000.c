// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 UltraRISC Technology (Shanghai) Co., Ltd.
 */

#include <linux/module.h>

#include <dt-bindings/clock/ultrarisc,dp1000-clk.h>

#include "clk-ultrarisc.h"

#define DP1000_PLL_CFG1_OFFSET		0x400
#define DP1000_PLL_CFG2_OFFSET		0x404

#define DP1000_CCR_UART_OFFSET		0x220
#define DP1000_CCR_I2C_OFFSET		0x224
#define DP1000_CCR_GMAC_OFFSET		0x228
#define DP1000_CCR_SPI_OFFSET		0x22c
#define DP1000_PERI_CLKENA_OFFSET	0x270

#define DP1000_CCR_LOAD			BIT(16)

#define DP1000_PERI_MAX_RATE		62500000UL
#define DP1000_CLK_NUM			21

static const struct ultrarisc_pll_layout dp1000_pll_layout = {
	.cfg1_offset = DP1000_PLL_CFG1_OFFSET,
	.cfg2_offset = DP1000_PLL_CFG2_OFFSET,
	.frac_mask = GENMASK(23, 0),
	.m_mask = GENMASK(23, 16),
	.n_mask = GENMASK(11, 6),
	.oddiv1_mask = GENMASK(1, 0),
	.oddiv2_mask = GENMASK(4, 3),
};

static const struct ultrarisc_pll_desc dp1000_plls[] = {
	{
		.id = DP1000_CLK_SYSPLL,
		.name = "syspll_clk",
	},
};

#define DP1000_FIXED_FACTOR(_id, _name, _parent, _mult, _div)	\
	{							\
		.id = (_id),					\
		.name = (_name),				\
		.parent_id = (_parent),				\
		.mult = (_mult),				\
		.div = (_div),					\
	}

#define DP1000_DIV(_id, _name, _offset, _parent, _max_rate)	\
	{							\
		.id = (_id),					\
		.name = (_name),				\
		.offset = (_offset),				\
		.parent_id = (_parent),				\
		.max_rate = (_max_rate),			\
		.load_mask = DP1000_CCR_LOAD,			\
		.div_shift = 8,					\
		.div_width = 4,					\
		.gate_bit = 0,					\
		.divider_flags = CLK_DIVIDER_ONE_BASED,		\
		.gate_flags = 0,				\
	}

#define DP1000_GATE(_id, _name, _parent, _bit)		\
	{							\
		.id = (_id),					\
		.name = (_name),				\
		.offset = DP1000_PERI_CLKENA_OFFSET,		\
		.parent_id = (_parent),				\
		.gate_bit = (_bit),				\
		.gate_flags = 0,				\
	}

static const struct ultrarisc_fixed_factor_desc dp1000_fixed_factor_clks[] = {
	DP1000_FIXED_FACTOR(DP1000_CLK_SYSPLL_DIV2, "syspll_div2_clk",
			    DP1000_CLK_SYSPLL, 1, 2),
	DP1000_FIXED_FACTOR(DP1000_CLK_SUBSYS, "subsys_clk",
			    DP1000_CLK_SYSPLL_DIV2, 1, 2),
	DP1000_FIXED_FACTOR(DP1000_CLK_PCIE_DBI, "pcie_dbi_clk",
			    DP1000_CLK_SYSPLL, 1, 10),
	DP1000_FIXED_FACTOR(DP1000_CLK_PCIEX4_CORE, "pciex4_core_clk",
			    DP1000_CLK_SYSPLL, 1, 2),
	DP1000_FIXED_FACTOR(DP1000_CLK_PCIEX16_CORE, "pciex16_core_clk",
			    DP1000_CLK_SYSPLL, 1, 1),
	DP1000_FIXED_FACTOR(DP1000_CLK_PCIE_AUX, "pcie_aux_clk",
			    DP1000_CLK_SYSPLL, 1, 40),
};

static const struct ultrarisc_divider_desc dp1000_divider_clks[] = {
	DP1000_DIV(DP1000_CLK_GMAC, "gmac_clk", DP1000_CCR_GMAC_OFFSET,
		   DP1000_CLK_SYSPLL_DIV2, 0),
	DP1000_DIV(DP1000_CLK_UART_ROOT, "uart_root_clk",
		   DP1000_CCR_UART_OFFSET, DP1000_CLK_SUBSYS,
		   DP1000_PERI_MAX_RATE),
	DP1000_DIV(DP1000_CLK_I2C_ROOT, "i2c_root_clk",
		   DP1000_CCR_I2C_OFFSET, DP1000_CLK_SUBSYS,
		   DP1000_PERI_MAX_RATE),
	DP1000_DIV(DP1000_CLK_SPI_ROOT, "spi_root_clk",
		   DP1000_CCR_SPI_OFFSET, DP1000_CLK_SUBSYS,
		   DP1000_PERI_MAX_RATE),
};

static const struct ultrarisc_gate_desc dp1000_gate_clks[] = {
	DP1000_GATE(DP1000_CLK_UART0, "uart0_clk", DP1000_CLK_UART_ROOT, 0),
	DP1000_GATE(DP1000_CLK_UART1, "uart1_clk", DP1000_CLK_UART_ROOT, 1),
	DP1000_GATE(DP1000_CLK_UART2, "uart2_clk", DP1000_CLK_UART_ROOT, 2),
	DP1000_GATE(DP1000_CLK_UART3, "uart3_clk", DP1000_CLK_UART_ROOT, 3),
	DP1000_GATE(DP1000_CLK_I2C0, "i2c0_clk", DP1000_CLK_I2C_ROOT, 4),
	DP1000_GATE(DP1000_CLK_I2C1, "i2c1_clk", DP1000_CLK_I2C_ROOT, 5),
	DP1000_GATE(DP1000_CLK_I2C2, "i2c2_clk", DP1000_CLK_I2C_ROOT, 6),
	DP1000_GATE(DP1000_CLK_I2C3, "i2c3_clk", DP1000_CLK_I2C_ROOT, 7),
	DP1000_GATE(DP1000_CLK_SPI0, "spi0_clk", DP1000_CLK_SPI_ROOT, 8),
	DP1000_GATE(DP1000_CLK_SPI1, "spi1_clk", DP1000_CLK_SPI_ROOT, 9),
};

static const struct ultrarisc_clk_soc_data dp1000_clk_soc_data = {
	.num_clks = DP1000_CLK_NUM,
	.pll_layout = &dp1000_pll_layout,
	.plls = dp1000_plls,
	.num_plls = ARRAY_SIZE(dp1000_plls),
	.fixed_factors = dp1000_fixed_factor_clks,
	.num_fixed_factors = ARRAY_SIZE(dp1000_fixed_factor_clks),
	.dividers = dp1000_divider_clks,
	.num_dividers = ARRAY_SIZE(dp1000_divider_clks),
	.gates = dp1000_gate_clks,
	.num_gates = ARRAY_SIZE(dp1000_gate_clks),
};

static int dp1000_clk_probe(struct platform_device *pdev)
{
	return ultrarisc_clk_probe(pdev, &dp1000_clk_soc_data);
}

static const struct of_device_id dp1000_clk_of_match[] = {
	{ .compatible = "ultrarisc,dp1000-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, dp1000_clk_of_match);

static struct platform_driver dp1000_clk_driver = {
	.probe = dp1000_clk_probe,
	.driver = {
		.name = "ultrarisc-dp1000-clk",
		.of_match_table = dp1000_clk_of_match,
	},
};
module_platform_driver(dp1000_clk_driver);

MODULE_IMPORT_NS("CLK_ULTRARISC");
MODULE_DESCRIPTION("UltraRISC DP1000 clock controller");
MODULE_LICENSE("GPL");
