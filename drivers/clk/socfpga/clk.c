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

/* Clock Manager offsets */
#define CLKMGR_CTRL	0x0
#define CLKMGR_BYPASS	0x4
#define CLKMGR_DBCTRL	0x10
#define CLKMGR_L4SRC	0x70
#define CLKMGR_PERIP_VCO	0x80
#define CLKMGR_PERPLL_SRC	0xAC
#define CLKMGR_SDRAM_VCO	0xC0

#define CLK_MGR_PERIP_PLL_CLK_SRC_SHIFT	22
#define CLK_MGR_PERIP_PLL_CLK_SRC_MASK	0x3

#define CLK_MGR_SDRAM_CLK_SRC_SHIFT	22
#define CLK_MGR_SDRAM_CLK_SRC_MASK	0x3

/* Clock bypass bits */
#define MAINPLL_BYPASS		(1<<0)
#define SDRAMPLL_BYPASS		(1<<1)
#define SDRAMPLL_SRC_BYPASS	(1<<2)
#define PERPLL_BYPASS		(1<<3)
#define PERPLL_SRC_BYPASS	(1<<4)

#define SOCFPGA_PLL_BG_PWRDWN		0
#define SOCFPGA_PLL_EXT_ENA		1
#define SOCFPGA_PLL_PWR_DOWN		2
#define SOCFPGA_PLL_DIVF_MASK		0x0000FFF8
#define SOCFPGA_PLL_DIVF_SHIFT	3
#define SOCFPGA_PLL_DIVQ_MASK		0x003F0000
#define SOCFPGA_PLL_DIVQ_SHIFT	16
#define SOCFGPA_MAX_PARENTS	3

#define SOCFPGA_L4_MP_CLK		"l4_mp_clk"
#define SOCFPGA_L4_SP_CLK		"l4_sp_clk"
#define SOCFPGA_NAND_CLK		"nand_clk"
#define SOCFPGA_NAND_X_CLK		"nand_x_clk"
#define SOCFPGA_MMC_CLK			"sdmmc_clk"
#define SOCFPGA_DB_CLK			"gpio_db_clk"
#define SOCFPGA_PERIP_PLL_CLK		"periph_pll"
#define SOCFPGA_SDRAM_PLL_CLK		"sdram_pll"

#define div_mask(width)	((1 << (width)) - 1)
#define streq(a, b) (strcmp((a), (b)) == 0)

extern void __iomem *clk_mgr_base_addr;

struct socfpga_clk {
	struct clk_gate hw;
	char *parent_name;
	char *clk_name;
	u32 fixed_div;
	void __iomem *div_reg;
	u32 width;	/* only valid if div_reg != 0 */
	u32 shift;	/* only valid if div_reg != 0 */
};
#define to_socfpga_clk(p) container_of(p, struct socfpga_clk, hw.hw)

static unsigned long clk_pll_recalc_rate(struct clk_hw *hwclk,
					 unsigned long parent_rate)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	unsigned long divf, divq, reg;
	unsigned long long vco_freq;
	unsigned long bypass;

	reg = readl(socfpgaclk->hw.reg);
	bypass = readl(clk_mgr_base_addr + CLKMGR_BYPASS);
	if (bypass & MAINPLL_BYPASS)
		return parent_rate;

	divf = (reg & SOCFPGA_PLL_DIVF_MASK) >> SOCFPGA_PLL_DIVF_SHIFT;
	divq = (reg & SOCFPGA_PLL_DIVQ_MASK) >> SOCFPGA_PLL_DIVQ_SHIFT;
	vco_freq = (unsigned long long)parent_rate * (divf + 1);
	do_div(vco_freq, (1 + divq));
	return (unsigned long)vco_freq;
}

static u8 clk_pll_get_parent(struct clk_hw *hwclk)
{
	u32 pll_src;

	if (streq(hwclk->init->name, SOCFPGA_PERIP_PLL_CLK)) {
		pll_src = readl(clk_mgr_base_addr + CLKMGR_PERIP_VCO);
		return (pll_src >> CLK_MGR_PERIP_PLL_CLK_SRC_SHIFT) &
				CLK_MGR_PERIP_PLL_CLK_SRC_MASK;
	} else if (streq(hwclk->init->name, SOCFPGA_SDRAM_PLL_CLK)) {
		pll_src = readl(clk_mgr_base_addr + CLKMGR_SDRAM_VCO);
		return (pll_src >> CLK_MGR_SDRAM_CLK_SRC_SHIFT) &
				CLK_MGR_SDRAM_CLK_SRC_MASK;
	}
	return 0;
}


static struct clk_ops clk_pll_ops = {
	.recalc_rate = clk_pll_recalc_rate,
	.get_parent = clk_pll_get_parent,
};

static unsigned long clk_periclk_recalc_rate(struct clk_hw *hwclk,
					     unsigned long parent_rate)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	u32 div, val;

	if (socfpgaclk->fixed_div) {
		div = socfpgaclk->fixed_div;
	} else {
		if (socfpgaclk->div_reg) {
			val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
			val &= div_mask(socfpgaclk->width);
			parent_rate /= (val + 1);
		}
		div = ((readl(socfpgaclk->hw.reg) & 0x1ff) + 1);
	}

	return parent_rate / div;
}

static u8 clk_periclk_get_parent(struct clk_hw *hwclk)
{
	u32 clk_src;

	clk_src = readl(clk_mgr_base_addr + CLKMGR_DBCTRL);
	return clk_src & 0x1;
}

static const struct clk_ops periclk_ops = {
	.recalc_rate = clk_periclk_recalc_rate,
	.get_parent = clk_periclk_get_parent,
};

static __init struct clk *socfpga_clk_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 reg;
	struct clk *clk;
	struct socfpga_clk *socfpga_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFGPA_MAX_PARENTS];
	struct clk_init_data init;
	int rc;
	u32 fixed_div;
	int i = 0;
	u32 div_reg[3];

	of_property_read_u32(node, "reg", &reg);

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (WARN_ON(!socfpga_clk))
		return NULL;

	if (reg)
		socfpga_clk->hw.reg = clk_mgr_base_addr + reg;

	rc = of_property_read_u32_array(node, "div-reg", div_reg, 3);
	if (!rc) {
		socfpga_clk->div_reg = clk_mgr_base_addr + div_reg[0];
		socfpga_clk->shift = div_reg[1];
		socfpga_clk->width = div_reg[2];
	} else {
		socfpga_clk->div_reg = 0;
	}

	rc = of_property_read_u32(node, "fixed-divider", &fixed_div);
	if (rc)
		socfpga_clk->fixed_div = 0;
	else
		socfpga_clk->fixed_div = fixed_div;

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;

	while (i < SOCFGPA_MAX_PARENTS && (parent_name[i] =
			of_clk_get_parent_name(node, i)) != NULL)
		i++;
	init.num_parents = i;
	init.parent_names = parent_name;
	socfpga_clk->hw.hw.init = &init;

	if (streq(clk_name, "main_pll") ||
		streq(clk_name, "periph_pll") ||
		streq(clk_name, "sdram_pll")) {
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

static u8 socfpga_clk_get_parent(struct clk_hw *hwclk)
{
	u32 l4_src;
	u32 perpll_src;

	if (streq(hwclk->init->name, SOCFPGA_L4_MP_CLK)) {
		l4_src = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		return l4_src &= 0x1;
	}
	if (streq(hwclk->init->name, SOCFPGA_L4_SP_CLK)) {
		l4_src = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		return !!(l4_src & 2);
	}

	perpll_src = readl(clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
	if (streq(hwclk->init->name, SOCFPGA_MMC_CLK))
		return perpll_src &= 0x3;
	if (streq(hwclk->init->name, SOCFPGA_NAND_CLK) ||
			streq(hwclk->init->name, SOCFPGA_NAND_X_CLK))
			return (perpll_src >> 2) & 3;

	/* QSPI clock */
	return (perpll_src >> 4) & 3;

}

static int socfpga_clk_set_parent(struct clk_hw *hwclk, u8 parent)
{
	u32 src_reg;

	if (streq(hwclk->init->name, SOCFPGA_L4_MP_CLK)) {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		src_reg &= ~0x1;
		src_reg |= parent;
		writel(src_reg, clk_mgr_base_addr + CLKMGR_L4SRC);
	} else if (streq(hwclk->init->name, SOCFPGA_L4_SP_CLK)) {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_L4SRC);
		src_reg &= ~0x2;
		src_reg |= (parent << 1);
		writel(src_reg, clk_mgr_base_addr + CLKMGR_L4SRC);
	} else {
		src_reg = readl(clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
		if (streq(hwclk->init->name, SOCFPGA_MMC_CLK)) {
			src_reg &= ~0x3;
			src_reg |= parent;
		} else if (streq(hwclk->init->name, SOCFPGA_NAND_CLK) ||
			streq(hwclk->init->name, SOCFPGA_NAND_X_CLK)) {
			src_reg &= ~0xC;
			src_reg |= (parent << 2);
		} else {/* QSPI clock */
			src_reg &= ~0x30;
			src_reg |= (parent << 4);
		}
		writel(src_reg, clk_mgr_base_addr + CLKMGR_PERPLL_SRC);
	}

	return 0;
}

static unsigned long socfpga_clk_recalc_rate(struct clk_hw *hwclk,
	unsigned long parent_rate)
{
	struct socfpga_clk *socfpgaclk = to_socfpga_clk(hwclk);
	u32 div = 1, val;

	if (socfpgaclk->fixed_div)
		div = socfpgaclk->fixed_div;
	else if (socfpgaclk->div_reg) {
		val = readl(socfpgaclk->div_reg) >> socfpgaclk->shift;
		val &= div_mask(socfpgaclk->width);
		if (streq(hwclk->init->name, SOCFPGA_DB_CLK))
			div = val + 1;
		else
			div = (1 << val);
	}

	return parent_rate / div;
}

static struct clk_ops gateclk_ops = {
	.recalc_rate = socfpga_clk_recalc_rate,
	.get_parent = socfpga_clk_get_parent,
	.set_parent = socfpga_clk_set_parent,
};

static void __init socfpga_gate_clk_init(struct device_node *node,
	const struct clk_ops *ops)
{
	u32 clk_gate[2];
	u32 div_reg[3];
	u32 fixed_div;
	struct clk *clk;
	struct socfpga_clk *socfpga_clk;
	const char *clk_name = node->name;
	const char *parent_name[SOCFGPA_MAX_PARENTS];
	struct clk_init_data init;
	int rc;
	int i = 0;

	socfpga_clk = kzalloc(sizeof(*socfpga_clk), GFP_KERNEL);
	if (WARN_ON(!socfpga_clk))
		return;

	rc = of_property_read_u32_array(node, "clk-gate", clk_gate, 2);
	if (rc)
		clk_gate[0] = 0;

	if (clk_gate[0]) {
		socfpga_clk->hw.reg = clk_mgr_base_addr + clk_gate[0];
		socfpga_clk->hw.bit_idx = clk_gate[1];

		gateclk_ops.enable = clk_gate_ops.enable;
		gateclk_ops.disable = clk_gate_ops.disable;
	}

	rc = of_property_read_u32(node, "fixed-divider", &fixed_div);
	if (rc)
		socfpga_clk->fixed_div = 0;
	else
		socfpga_clk->fixed_div = fixed_div;

	rc = of_property_read_u32_array(node, "div-reg", div_reg, 3);
	if (!rc) {
		socfpga_clk->div_reg = clk_mgr_base_addr + div_reg[0];
		socfpga_clk->shift = div_reg[1];
		socfpga_clk->width = div_reg[2];
	} else {
		socfpga_clk->div_reg = 0;
	}

	of_property_read_string(node, "clock-output-names", &clk_name);

	init.name = clk_name;
	init.ops = ops;
	init.flags = 0;
	while (i < SOCFGPA_MAX_PARENTS && (parent_name[i] =
			of_clk_get_parent_name(node, i)) != NULL)
		i++;

	init.parent_names = parent_name;
	init.num_parents = i;
	socfpga_clk->hw.hw.init = &init;

	clk = clk_register(NULL, &socfpga_clk->hw.hw);
	if (WARN_ON(IS_ERR(clk))) {
		kfree(socfpga_clk);
		return;
	}
	rc = of_clk_add_provider(node, of_clk_src_simple_get, clk);
	if (WARN_ON(rc))
		return;
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

static void __init socfpga_gate_init(struct device_node *node)
{
	socfpga_gate_clk_init(node, &gateclk_ops);
}
CLK_OF_DECLARE(socfpga_gate, "altr,socfpga-gate-clk", socfpga_gate_init);
