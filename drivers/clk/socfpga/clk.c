/*
 *  Copyright 2011-2012 Calxeda, Inc.
 *  Copyright (C) 2012-2013 Altera Corporation <www.altera.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * Based from clk-highbank.c
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

static DEFINE_SPINLOCK(_lock);

/* Clock Manager offsets */
#define CLKMGR_CTRL    0x0
#define CLKMGR_BYPASS 0x4

#define SOCFPGA_MAIN_PLL_CLK		1200000000
#define SOCFPGA_PER_PLL_CLK		900000000
#define SOCFPGA_SDRAM_PLL_CLK		800000000

#define CLKMGR_PERPLLGRP_EN	0xA0

#define CLKMGR_QSPI_CLK_EN				11
#define CLKMGR_NAND_CLK_EN				10
#define CLKMGR_NAND_X_CLK_EN			9
#define CLKMGR_SDMMC_CLK_EN			8
#define CLKMGR_S2FUSR_CLK_EN			7
#define CLKMGR_GPIO_CLK_EN				6
#define CLKMGR_CAN1_CLK_EN				5
#define CLKMGR_CAN0_CLK_EN				4
#define CLKMGR_SPI_M_CLK_EN			3
#define CLKMGR_USB_MP_CLK_EN			2
#define CLKMGR_EMAC1_CLK_EN			1
#define CLKMGR_EMAC0_CLK_EN			0

void __iomem *clk_mgr_base_addr;

void __init socfpga_init_clocks(void)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long divf, divq, vco_freq, reg;
	unsigned long bypass;

	reg = readl(socfpgaclk->hw.reg);
	bypass = readl(clk_mgr_base_addr + CLKMGR_BYPASS);
	if (bypass & MAINPLL_BYPASS)
		return parent_rate;

	divf = (reg & SOCFPGA_PLL_DIVF_MASK) >> SOCFPGA_PLL_DIVF_SHIFT;
	divq = (reg & SOCFPGA_PLL_DIVQ_MASK) >> SOCFPGA_PLL_DIVQ_SHIFT;
	vco_freq = parent_rate * (divf + 1);
	return vco_freq / (1 + divq);
}


static struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
};

static unsigned long clk_periclk_recalc_rate(struct clk_hw *hwclk,
					     unsigned long parent_rate)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	u32 div;

	if (socfpgaclk->fixed_div)
		div = socfpgaclk->fixed_div;
	else
		div = ((readl(socfpgaclk->hw.reg) & 0x1ff) + 1);

	return parent_rate / div;
}

static const struct clk_ops periclk_ops = {
	.recalc_rate = clk_periclk_recalc_rate,
};

static __init struct clk *socfpga_clk_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct device_node *np;

	np = of_find_compatible_node(NULL, NULL, "altr,clk-mgr");
	clk_mgr_base_addr = of_iomap(np, 0);

	clk = clk_register_fixed_rate(NULL, "main_pll_clk", NULL, CLK_IS_ROOT,
			SOCFPGA_MAIN_PLL_CLK);
	clk_register_clkdev(clk, "main_pll_clk", NULL);
	
	clk = clk_register_fixed_rate(NULL, "per_pll_clk", NULL, CLK_IS_ROOT,
			SOCFPGA_PER_PLL_CLK);
	clk_register_clkdev(clk, "per_pll_clk", NULL);
	
	clk = clk_register_fixed_rate(NULL, "sdram_pll_clk", NULL, CLK_IS_ROOT,
			SOCFPGA_SDRAM_PLL_CLK);
	clk_register_clkdev(clk, "sdram_pll_clk", NULL);
	
	clk = clk_register_fixed_rate(NULL, "osc1_clk", NULL, CLK_IS_ROOT, SOCFPGA_OSC1_CLK);
	clk_register_clkdev(clk, "osc1_clk", NULL);

	if (strcmp(clk_name, "main_pll") || strcmp(clk_name, "periph_pll") ||
			strcmp(clk_name, "sdram_pll")) {
		socfpga_clk->hw.bit_idx = SOCFPGA_PLL_EXT_ENA;
		clk_pll_ops.enable = clk_gate_ops.enable;
		clk_pll_ops.disable = clk_gate_ops.disable;
	}

	clk = clk_register(NULL, &socfpga_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(socfpga_clk);
		return NULL;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	return clk;
}

static void __init socfpga_pll_init(struct device_node *node)
{
	socfpga_clk_init(node, &clk_pll_ops);
}
CLK_OF_DECLARE(socfpga_pll, "altr,socfpga-pll-clock", socfpga_pll_init);

static void __init socfpga_periph_init(struct device_node *node)
{
	socfpga_clk_init(node, &periclk_ops);
}
CLK_OF_DECLARE(socfpga_periph, "altr,socfpga-perip-clk", socfpga_periph_init);

void __init socfpga_init_clocks(void)
{
	struct clk *clk;
	int ret;

	clk = clk_register_fixed_rate(NULL, "s2f_usr_clk", NULL, CLK_IS_ROOT, SOCFPGA_S2F_USR_CLK);
	clk_register_clkdev(clk, "s2f_usr_clk", NULL);

	clk = clk_register_gate(NULL, "mmc_clk", "main_nand_sdmmc_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_SDMMC_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ff704000.sdmmc");
	
	clk = clk_register_gate(NULL, "gmac_clk", "per_pll_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_EMAC0_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ff700000.stmmac");

	clk = clk_register_gate(NULL, "spi0_clk", "per_pll_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_SPI_M_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fff00000.spi");

	clk = clk_register_gate(NULL, "spi1_clk", "per_pll_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_SPI_M_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "fff01000.spi");

	clk = clk_register_gate(NULL, "gpio0_clk", "per_pll_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_GPIO_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ff708000.gpio");

	clk = clk_register_gate(NULL, "gpio1_clk", "per_pll_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_GPIO_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ff709000.gpio");

	clk = clk_register_gate(NULL, "gpio2_clk", "per_pll_clk", 0, clk_mgr_base_addr + CLKMGR_PERPLLGRP_EN,
			CLKMGR_GPIO_CLK_EN, 0, &_lock);
	clk_register_clkdev(clk, NULL, "ff70a000.gpio");
}
