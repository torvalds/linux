// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 Nuvoton Technology Corp.
 * Author: Chi-Fang Li <cfli0@nuvoton.com>
 */

#include <linux/clk-provider.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/spinlock.h>
#include <dt-bindings/clock/nuvoton,ma35d1-clk.h>

#include "clk-ma35d1.h"

static DEFINE_SPINLOCK(ma35d1_lock);

#define PLL_MAX_NUM		5

/* Clock Control Registers Offset */
#define REG_CLK_PWRCTL		0x00
#define REG_CLK_SYSCLK0		0x04
#define REG_CLK_SYSCLK1		0x08
#define REG_CLK_APBCLK0		0x0c
#define REG_CLK_APBCLK1		0x10
#define REG_CLK_APBCLK2		0x14
#define REG_CLK_CLKSEL0		0x18
#define REG_CLK_CLKSEL1		0x1c
#define REG_CLK_CLKSEL2		0x20
#define REG_CLK_CLKSEL3		0x24
#define REG_CLK_CLKSEL4		0x28
#define REG_CLK_CLKDIV0		0x2c
#define REG_CLK_CLKDIV1		0x30
#define REG_CLK_CLKDIV2		0x34
#define REG_CLK_CLKDIV3		0x38
#define REG_CLK_CLKDIV4		0x3c
#define REG_CLK_CLKOCTL		0x40
#define REG_CLK_STATUS		0x50
#define REG_CLK_PLL0CTL0	0x60
#define REG_CLK_PLL2CTL0	0x80
#define REG_CLK_PLL2CTL1	0x84
#define REG_CLK_PLL2CTL2	0x88
#define REG_CLK_PLL3CTL0	0x90
#define REG_CLK_PLL3CTL1	0x94
#define REG_CLK_PLL3CTL2	0x98
#define REG_CLK_PLL4CTL0	0xa0
#define REG_CLK_PLL4CTL1	0xa4
#define REG_CLK_PLL4CTL2	0xa8
#define REG_CLK_PLL5CTL0	0xb0
#define REG_CLK_PLL5CTL1	0xb4
#define REG_CLK_PLL5CTL2	0xb8
#define REG_CLK_CLKDCTL		0xc0
#define REG_CLK_CLKDSTS		0xc4
#define REG_CLK_CDUPB		0xc8
#define REG_CLK_CDLOWB		0xcc
#define REG_CLK_CKFLTRCTL	0xd0
#define REG_CLK_TESTCLK		0xf0
#define REG_CLK_PLLCTL		0x40

#define PLL_MODE_INT            0
#define PLL_MODE_FRAC           1
#define PLL_MODE_SS             2

static const struct clk_parent_data ca35clk_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "capll", },
	{ .fw_name = "ddrpll", },
};

static const struct clk_parent_data sysclk0_sel_clks[] = {
	{ .fw_name = "epll_div2", },
	{ .fw_name = "syspll", },
};

static const struct clk_parent_data sysclk1_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "syspll", },
};

static const struct clk_parent_data axiclk_sel_clks[] = {
	{ .fw_name = "capll_div2", },
	{ .fw_name = "capll_div4", },
};

static const struct clk_parent_data ccap_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "vpll", },
	{ .fw_name = "apll", },
	{ .fw_name = "syspll", },
};

static const struct clk_parent_data sdh_sel_clks[] = {
	{ .fw_name = "syspll", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data dcu_sel_clks[] = {
	{ .fw_name = "epll_div2", },
	{ .fw_name = "syspll", },
};

static const struct clk_parent_data gfx_sel_clks[] = {
	{ .fw_name = "epll", },
	{ .fw_name = "syspll", },
};

static const struct clk_parent_data dbg_sel_clks[] = {
	{ .fw_name = "hirc", },
	{ .fw_name = "syspll", },
};

static const struct clk_parent_data timer0_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk0", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer1_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk0", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer2_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk1", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer3_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk1", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer4_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk2", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer5_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk2", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer6_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk0", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer7_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk0", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer8_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk1", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer9_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk1", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer10_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk2", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data timer11_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk2", },
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "lirc", },
	{ .index = -1, },
	{ .fw_name = "hirc", },
};

static const struct clk_parent_data uart_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "sysclk1_div2", },
};

static const struct clk_parent_data wdt0_sel_clks[] = {
	{ .index = -1, },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk3_div4096", },
	{ .fw_name = "lirc", },
};

static const struct clk_parent_data wdt1_sel_clks[] = {
	{ .index = -1, },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk3_div4096", },
	{ .fw_name = "lirc", },
};

static const struct clk_parent_data wdt2_sel_clks[] = {
	{ .index = -1, },
	{ .fw_name = "lxt", },
	{ .fw_name = "pclk4_div4096", },
	{ .fw_name = "lirc", },
};

static const struct clk_parent_data wwdt0_sel_clks[] = {
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "pclk3_div4096", },
	{ .fw_name = "lirc", },
};

static const struct clk_parent_data wwdt1_sel_clks[] = {
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "pclk3_div4096", },
	{ .fw_name = "lirc", },
};

static const struct clk_parent_data wwdt2_sel_clks[] = {
	{ .index = -1, },
	{ .index = -1, },
	{ .fw_name = "pclk4_div4096", },
	{ .fw_name = "lirc", },
};

static const struct clk_parent_data spi0_sel_clks[] = {
	{ .fw_name = "pclk1", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data spi1_sel_clks[] = {
	{ .fw_name = "pclk2", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data spi2_sel_clks[] = {
	{ .fw_name = "pclk1", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data spi3_sel_clks[] = {
	{ .fw_name = "pclk2", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data qspi0_sel_clks[] = {
	{ .fw_name = "pclk0", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data qspi1_sel_clks[] = {
	{ .fw_name = "pclk0", },
	{ .fw_name = "apll", },
};

static const struct clk_parent_data i2s0_sel_clks[] = {
	{ .fw_name = "apll", },
	{ .fw_name = "sysclk1_div2", },
};

static const struct clk_parent_data i2s1_sel_clks[] = {
	{ .fw_name = "apll", },
	{ .fw_name = "sysclk1_div2", },
};

static const struct clk_parent_data can_sel_clks[] = {
	{ .fw_name = "apll", },
	{ .fw_name = "vpll", },
};

static const struct clk_parent_data cko_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
	{ .fw_name = "hirc", },
	{ .fw_name = "lirc", },
	{ .fw_name = "capll_div4", },
	{ .fw_name = "syspll", },
	{ .fw_name = "ddrpll", },
	{ .fw_name = "epll_div2", },
	{ .fw_name = "apll", },
	{ .fw_name = "vpll", },
};

static const struct clk_parent_data smc_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "pclk4", },
};

static const struct clk_parent_data kpi_sel_clks[] = {
	{ .fw_name = "hxt", },
	{ .fw_name = "lxt", },
};

static const struct clk_div_table ip_div_table[] = {
	{0, 2}, {1, 4}, {2, 6}, {3, 8}, {4, 10},
	{5, 12}, {6, 14}, {7, 16}, {0, 0},
};

static const struct clk_div_table eadc_div_table[] = {
	{0, 2}, {1, 4}, {2, 6}, {3, 8}, {4, 10},
	{5, 12}, {6, 14}, {7, 16}, {8, 18},
	{9, 20}, {10, 22}, {11, 24}, {12, 26},
	{13, 28}, {14, 30}, {15, 32}, {0, 0},
};

static struct clk_hw *ma35d1_clk_fixed(const char *name, int rate)
{
	return clk_hw_register_fixed_rate(NULL, name, NULL, 0, rate);
}

static struct clk_hw *ma35d1_clk_mux_parent(struct device *dev, const char *name,
					    void __iomem *reg, u8 shift, u8 width,
					    const struct clk_parent_data *pdata,
					    int num_pdata)
{
	return clk_hw_register_mux_parent_data(dev, name, pdata, num_pdata,
					       CLK_SET_RATE_NO_REPARENT, reg, shift,
					       width, 0, &ma35d1_lock);
}

static struct clk_hw *ma35d1_clk_mux(struct device *dev, const char *name,
				     void __iomem *reg, u8 shift, u8 width,
				     const struct clk_parent_data *pdata,
				     int num_pdata)
{
	return clk_hw_register_mux_parent_data(dev, name, pdata, num_pdata,
					       CLK_SET_RATE_NO_REPARENT, reg, shift,
					       width, 0, &ma35d1_lock);
}

static struct clk_hw *ma35d1_clk_divider(struct device *dev, const char *name,
					 const char *parent, void __iomem *reg,
					 u8 shift, u8 width)
{
	return devm_clk_hw_register_divider(dev, name, parent, CLK_SET_RATE_PARENT,
					    reg, shift, width, 0, &ma35d1_lock);
}

static struct clk_hw *ma35d1_clk_divider_pow2(struct device *dev, const char *name,
					      const char *parent, void __iomem *reg,
					      u8 shift, u8 width)
{
	return devm_clk_hw_register_divider(dev, name, parent,
					    CLK_DIVIDER_POWER_OF_TWO, reg, shift,
					    width, 0, &ma35d1_lock);
}

static struct clk_hw *ma35d1_clk_divider_table(struct device *dev, const char *name,
					       const char *parent, void __iomem *reg,
					       u8 shift, u8 width,
					       const struct clk_div_table *table)
{
	return devm_clk_hw_register_divider_table(dev, name, parent, 0,
						  reg, shift, width, 0,
						  table, &ma35d1_lock);
}

static struct clk_hw *ma35d1_clk_fixed_factor(struct device *dev, const char *name,
					      const char *parent, unsigned int mult,
					      unsigned int div)
{
	return devm_clk_hw_register_fixed_factor(dev, name, parent,
					    CLK_SET_RATE_PARENT, mult, div);
}

static struct clk_hw *ma35d1_clk_gate(struct device *dev, const char *name, const char *parent,
				      void __iomem *reg, u8 shift)
{
	return devm_clk_hw_register_gate(dev, name, parent, CLK_SET_RATE_PARENT,
				    reg, shift, 0, &ma35d1_lock);
}

static int ma35d1_get_pll_setting(struct device_node *clk_node, u32 *pllmode)
{
	const char *of_str;
	int i;

	for (i = 0; i < PLL_MAX_NUM; i++) {
		if (of_property_read_string_index(clk_node, "nuvoton,pll-mode", i, &of_str))
			return -EINVAL;
		if (!strcmp(of_str, "integer"))
			pllmode[i] = PLL_MODE_INT;
		else if (!strcmp(of_str, "fractional"))
			pllmode[i] = PLL_MODE_FRAC;
		else if (!strcmp(of_str, "spread-spectrum"))
			pllmode[i] = PLL_MODE_SS;
		else
			return -EINVAL;
	}
	return 0;
}

static int ma35d1_clocks_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *clk_node = pdev->dev.of_node;
	void __iomem *clk_base;
	static struct clk_hw **hws;
	static struct clk_hw_onecell_data *ma35d1_hw_data;
	u32 pllmode[PLL_MAX_NUM];
	int ret;

	ma35d1_hw_data = devm_kzalloc(dev,
				      struct_size(ma35d1_hw_data, hws, CLK_MAX_IDX),
				      GFP_KERNEL);
	if (!ma35d1_hw_data)
		return -ENOMEM;

	ma35d1_hw_data->num = CLK_MAX_IDX;
	hws = ma35d1_hw_data->hws;

	clk_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clk_base))
		return PTR_ERR(clk_base);

	ret = ma35d1_get_pll_setting(clk_node, pllmode);
	if (ret < 0) {
		dev_err(dev, "Invalid PLL setting!\n");
		return -EINVAL;
	}

	hws[HXT] = ma35d1_clk_fixed("hxt", 24000000);
	hws[HXT_GATE] = ma35d1_clk_gate(dev, "hxt_gate", "hxt",
					clk_base + REG_CLK_PWRCTL, 0);
	hws[LXT] = ma35d1_clk_fixed("lxt", 32768);
	hws[LXT_GATE] = ma35d1_clk_gate(dev, "lxt_gate", "lxt",
					clk_base + REG_CLK_PWRCTL, 1);
	hws[HIRC] = ma35d1_clk_fixed("hirc", 12000000);
	hws[HIRC_GATE] = ma35d1_clk_gate(dev, "hirc_gate", "hirc",
					 clk_base + REG_CLK_PWRCTL, 2);
	hws[LIRC] = ma35d1_clk_fixed("lirc", 32000);
	hws[LIRC_GATE] = ma35d1_clk_gate(dev, "lirc_gate", "lirc",
					 clk_base + REG_CLK_PWRCTL, 3);

	hws[CAPLL] = ma35d1_reg_clk_pll(dev, CAPLL, pllmode[0], "capll",
					hws[HXT], clk_base + REG_CLK_PLL0CTL0);
	hws[SYSPLL] = ma35d1_clk_fixed("syspll", 180000000);
	hws[DDRPLL] = ma35d1_reg_clk_pll(dev, DDRPLL, pllmode[1], "ddrpll",
					hws[HXT], clk_base + REG_CLK_PLL2CTL0);
	hws[APLL] = ma35d1_reg_clk_pll(dev, APLL, pllmode[2], "apll",
				       hws[HXT], clk_base + REG_CLK_PLL3CTL0);
	hws[EPLL] = ma35d1_reg_clk_pll(dev, EPLL, pllmode[3], "epll",
				       hws[HXT], clk_base + REG_CLK_PLL4CTL0);
	hws[VPLL] = ma35d1_reg_clk_pll(dev, VPLL, pllmode[4], "vpll",
				       hws[HXT], clk_base + REG_CLK_PLL5CTL0);

	hws[EPLL_DIV2] = ma35d1_clk_fixed_factor(dev, "epll_div2", "epll", 1, 2);
	hws[EPLL_DIV4] = ma35d1_clk_fixed_factor(dev, "epll_div4", "epll", 1, 4);
	hws[EPLL_DIV8] = ma35d1_clk_fixed_factor(dev, "epll_div8", "epll", 1, 8);

	hws[CA35CLK_MUX] = ma35d1_clk_mux_parent(dev, "ca35clk_mux",
						 clk_base + REG_CLK_CLKSEL0, 0, 2,
						 ca35clk_sel_clks,
						 ARRAY_SIZE(ca35clk_sel_clks));
	hws[AXICLK_DIV2] = ma35d1_clk_fixed_factor(dev, "capll_div2", "ca35clk_mux", 1, 2);
	hws[AXICLK_DIV4] = ma35d1_clk_fixed_factor(dev, "capll_div4", "ca35clk_mux", 1, 4);

	hws[AXICLK_MUX] = ma35d1_clk_mux(dev, "axiclk_mux", clk_base + REG_CLK_CLKDIV0,
					 26, 1, axiclk_sel_clks,
					 ARRAY_SIZE(axiclk_sel_clks));
	hws[SYSCLK0_MUX] = ma35d1_clk_mux(dev, "sysclk0_mux", clk_base + REG_CLK_CLKSEL0,
					  2, 1, sysclk0_sel_clks,
					  ARRAY_SIZE(sysclk0_sel_clks));
	hws[SYSCLK1_MUX] = ma35d1_clk_mux(dev, "sysclk1_mux", clk_base + REG_CLK_CLKSEL0,
					  4, 1, sysclk1_sel_clks,
					  ARRAY_SIZE(sysclk1_sel_clks));
	hws[SYSCLK1_DIV2] = ma35d1_clk_fixed_factor(dev, "sysclk1_div2", "sysclk1_mux", 1, 2);

	/* HCLK0~3 & PCLK0~4 */
	hws[HCLK0] = ma35d1_clk_fixed_factor(dev, "hclk0", "sysclk1_mux", 1, 1);
	hws[HCLK1] = ma35d1_clk_fixed_factor(dev, "hclk1", "sysclk1_mux", 1, 1);
	hws[HCLK2] = ma35d1_clk_fixed_factor(dev, "hclk2", "sysclk1_mux", 1, 1);
	hws[PCLK0] = ma35d1_clk_fixed_factor(dev, "pclk0", "sysclk1_mux", 1, 1);
	hws[PCLK1] = ma35d1_clk_fixed_factor(dev, "pclk1", "sysclk1_mux", 1, 1);
	hws[PCLK2] = ma35d1_clk_fixed_factor(dev, "pclk2", "sysclk1_mux", 1, 1);

	hws[HCLK3] = ma35d1_clk_fixed_factor(dev, "hclk3", "sysclk1_mux", 1, 2);
	hws[PCLK3] = ma35d1_clk_fixed_factor(dev, "pclk3", "sysclk1_mux", 1, 2);
	hws[PCLK4] = ma35d1_clk_fixed_factor(dev, "pclk4", "sysclk1_mux", 1, 2);

	hws[USBPHY0] = ma35d1_clk_fixed("usbphy0", 480000000);
	hws[USBPHY1] = ma35d1_clk_fixed("usbphy1", 480000000);

	/* DDR */
	hws[DDR0_GATE] = ma35d1_clk_gate(dev, "ddr0_gate", "ddrpll",
					 clk_base + REG_CLK_SYSCLK0, 4);
	hws[DDR6_GATE] = ma35d1_clk_gate(dev, "ddr6_gate", "ddrpll",
					 clk_base + REG_CLK_SYSCLK0, 5);

	hws[CAN0_MUX] = ma35d1_clk_mux(dev, "can0_mux", clk_base + REG_CLK_CLKSEL4,
				       16, 1, can_sel_clks, ARRAY_SIZE(can_sel_clks));
	hws[CAN0_DIV] = ma35d1_clk_divider_table(dev, "can0_div", "can0_mux",
						 clk_base + REG_CLK_CLKDIV0,
						 0, 3, ip_div_table);
	hws[CAN0_GATE] = ma35d1_clk_gate(dev, "can0_gate", "can0_div",
					 clk_base + REG_CLK_SYSCLK0, 8);
	hws[CAN1_MUX] = ma35d1_clk_mux(dev, "can1_mux", clk_base + REG_CLK_CLKSEL4,
				       17, 1, can_sel_clks, ARRAY_SIZE(can_sel_clks));
	hws[CAN1_DIV] = ma35d1_clk_divider_table(dev, "can1_div", "can1_mux",
						 clk_base + REG_CLK_CLKDIV0,
						 4, 3, ip_div_table);
	hws[CAN1_GATE] = ma35d1_clk_gate(dev, "can1_gate", "can1_div",
					 clk_base + REG_CLK_SYSCLK0, 9);
	hws[CAN2_MUX] = ma35d1_clk_mux(dev, "can2_mux", clk_base + REG_CLK_CLKSEL4,
				       18, 1, can_sel_clks, ARRAY_SIZE(can_sel_clks));
	hws[CAN2_DIV] = ma35d1_clk_divider_table(dev, "can2_div", "can2_mux",
						 clk_base + REG_CLK_CLKDIV0,
						 8, 3, ip_div_table);
	hws[CAN2_GATE] = ma35d1_clk_gate(dev, "can2_gate", "can2_div",
					 clk_base + REG_CLK_SYSCLK0, 10);
	hws[CAN3_MUX] = ma35d1_clk_mux(dev, "can3_mux", clk_base + REG_CLK_CLKSEL4,
				       19, 1, can_sel_clks, ARRAY_SIZE(can_sel_clks));
	hws[CAN3_DIV] = ma35d1_clk_divider_table(dev, "can3_div", "can3_mux",
						 clk_base + REG_CLK_CLKDIV0,
						 12, 3, ip_div_table);
	hws[CAN3_GATE] = ma35d1_clk_gate(dev, "can3_gate", "can3_div",
					 clk_base + REG_CLK_SYSCLK0, 11);

	hws[SDH0_MUX] = ma35d1_clk_mux(dev, "sdh0_mux", clk_base + REG_CLK_CLKSEL0,
				       16, 2, sdh_sel_clks, ARRAY_SIZE(sdh_sel_clks));
	hws[SDH0_GATE] = ma35d1_clk_gate(dev, "sdh0_gate", "sdh0_mux",
					 clk_base + REG_CLK_SYSCLK0, 16);
	hws[SDH1_MUX] = ma35d1_clk_mux(dev, "sdh1_mux", clk_base + REG_CLK_CLKSEL0,
				       18, 2, sdh_sel_clks, ARRAY_SIZE(sdh_sel_clks));
	hws[SDH1_GATE] = ma35d1_clk_gate(dev, "sdh1_gate", "sdh1_mux",
					 clk_base + REG_CLK_SYSCLK0, 17);

	hws[NAND_GATE] = ma35d1_clk_gate(dev, "nand_gate", "hclk1",
					 clk_base + REG_CLK_SYSCLK0, 18);

	hws[USBD_GATE] = ma35d1_clk_gate(dev, "usbd_gate", "usbphy0",
					 clk_base + REG_CLK_SYSCLK0, 19);
	hws[USBH_GATE] = ma35d1_clk_gate(dev, "usbh_gate", "usbphy0",
					 clk_base + REG_CLK_SYSCLK0, 20);
	hws[HUSBH0_GATE] = ma35d1_clk_gate(dev, "husbh0_gate", "usbphy0",
					   clk_base + REG_CLK_SYSCLK0, 21);
	hws[HUSBH1_GATE] = ma35d1_clk_gate(dev, "husbh1_gate", "usbphy0",
					   clk_base + REG_CLK_SYSCLK0, 22);

	hws[GFX_MUX] = ma35d1_clk_mux(dev, "gfx_mux", clk_base + REG_CLK_CLKSEL0,
				      26, 1, gfx_sel_clks, ARRAY_SIZE(gfx_sel_clks));
	hws[GFX_GATE] = ma35d1_clk_gate(dev, "gfx_gate", "gfx_mux",
					clk_base + REG_CLK_SYSCLK0, 24);
	hws[VC8K_GATE] = ma35d1_clk_gate(dev, "vc8k_gate", "sysclk0_mux",
					 clk_base + REG_CLK_SYSCLK0, 25);
	hws[DCU_MUX] = ma35d1_clk_mux(dev, "dcu_mux", clk_base + REG_CLK_CLKSEL0,
				      24, 1, dcu_sel_clks, ARRAY_SIZE(dcu_sel_clks));
	hws[DCU_GATE] = ma35d1_clk_gate(dev, "dcu_gate", "dcu_mux",
					clk_base + REG_CLK_SYSCLK0, 26);
	hws[DCUP_DIV] = ma35d1_clk_divider_table(dev, "dcup_div", "vpll",
						 clk_base + REG_CLK_CLKDIV0,
						 16, 3, ip_div_table);

	hws[EMAC0_GATE] = ma35d1_clk_gate(dev, "emac0_gate", "epll_div2",
					  clk_base + REG_CLK_SYSCLK0, 27);
	hws[EMAC1_GATE] = ma35d1_clk_gate(dev, "emac1_gate", "epll_div2",
					  clk_base + REG_CLK_SYSCLK0, 28);

	hws[CCAP0_MUX] = ma35d1_clk_mux(dev, "ccap0_mux", clk_base + REG_CLK_CLKSEL0,
					12, 1, ccap_sel_clks, ARRAY_SIZE(ccap_sel_clks));
	hws[CCAP0_DIV] = ma35d1_clk_divider(dev, "ccap0_div", "ccap0_mux",
					    clk_base + REG_CLK_CLKDIV1, 8, 4);
	hws[CCAP0_GATE] = ma35d1_clk_gate(dev, "ccap0_gate", "ccap0_div",
					  clk_base + REG_CLK_SYSCLK0, 29);
	hws[CCAP1_MUX] = ma35d1_clk_mux(dev, "ccap1_mux", clk_base + REG_CLK_CLKSEL0,
					14, 1, ccap_sel_clks, ARRAY_SIZE(ccap_sel_clks));
	hws[CCAP1_DIV] = ma35d1_clk_divider(dev, "ccap1_div", "ccap1_mux",
					    clk_base + REG_CLK_CLKDIV1,
					    12, 4);
	hws[CCAP1_GATE] = ma35d1_clk_gate(dev, "ccap1_gate", "ccap1_div",
					  clk_base + REG_CLK_SYSCLK0, 30);

	hws[PDMA0_GATE] = ma35d1_clk_gate(dev, "pdma0_gate", "hclk0",
					  clk_base + REG_CLK_SYSCLK1, 0);
	hws[PDMA1_GATE] = ma35d1_clk_gate(dev, "pdma1_gate", "hclk0",
					  clk_base + REG_CLK_SYSCLK1, 1);
	hws[PDMA2_GATE] = ma35d1_clk_gate(dev, "pdma2_gate", "hclk0",
					  clk_base + REG_CLK_SYSCLK1, 2);
	hws[PDMA3_GATE] = ma35d1_clk_gate(dev, "pdma3_gate", "hclk0",
					  clk_base + REG_CLK_SYSCLK1, 3);

	hws[WH0_GATE] = ma35d1_clk_gate(dev, "wh0_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 4);
	hws[WH1_GATE] = ma35d1_clk_gate(dev, "wh1_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 5);

	hws[HWS_GATE] = ma35d1_clk_gate(dev, "hws_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 6);

	hws[EBI_GATE] = ma35d1_clk_gate(dev, "ebi_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 7);

	hws[SRAM0_GATE] = ma35d1_clk_gate(dev, "sram0_gate", "hclk0",
					  clk_base + REG_CLK_SYSCLK1, 8);
	hws[SRAM1_GATE] = ma35d1_clk_gate(dev, "sram1_gate", "hclk0",
					  clk_base + REG_CLK_SYSCLK1, 9);

	hws[ROM_GATE] = ma35d1_clk_gate(dev, "rom_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 10);

	hws[TRA_GATE] = ma35d1_clk_gate(dev, "tra_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 11);

	hws[DBG_MUX] = ma35d1_clk_mux(dev, "dbg_mux", clk_base + REG_CLK_CLKSEL0,
				      27, 1, dbg_sel_clks, ARRAY_SIZE(dbg_sel_clks));
	hws[DBG_GATE] = ma35d1_clk_gate(dev, "dbg_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 12);

	hws[CKO_MUX] = ma35d1_clk_mux(dev, "cko_mux", clk_base + REG_CLK_CLKSEL4,
				      24, 4, cko_sel_clks, ARRAY_SIZE(cko_sel_clks));
	hws[CKO_DIV] = ma35d1_clk_divider_pow2(dev, "cko_div", "cko_mux",
					       clk_base + REG_CLK_CLKOCTL, 0, 4);
	hws[CKO_GATE] = ma35d1_clk_gate(dev, "cko_gate", "cko_div",
					clk_base + REG_CLK_SYSCLK1, 13);

	hws[GTMR_GATE] = ma35d1_clk_gate(dev, "gtmr_gate", "hirc",
					 clk_base + REG_CLK_SYSCLK1, 14);

	hws[GPA_GATE] = ma35d1_clk_gate(dev, "gpa_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 16);
	hws[GPB_GATE] = ma35d1_clk_gate(dev, "gpb_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 17);
	hws[GPC_GATE] = ma35d1_clk_gate(dev, "gpc_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 18);
	hws[GPD_GATE] = ma35d1_clk_gate(dev, "gpd_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 19);
	hws[GPE_GATE] = ma35d1_clk_gate(dev, "gpe_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 20);
	hws[GPF_GATE] = ma35d1_clk_gate(dev, "gpf_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 21);
	hws[GPG_GATE] = ma35d1_clk_gate(dev, "gpg_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 22);
	hws[GPH_GATE] = ma35d1_clk_gate(dev, "gph_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 23);
	hws[GPI_GATE] = ma35d1_clk_gate(dev, "gpi_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 24);
	hws[GPJ_GATE] = ma35d1_clk_gate(dev, "gpj_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 25);
	hws[GPK_GATE] = ma35d1_clk_gate(dev, "gpk_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 26);
	hws[GPL_GATE] = ma35d1_clk_gate(dev, "gpl_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 27);
	hws[GPM_GATE] = ma35d1_clk_gate(dev, "gpm_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 28);
	hws[GPN_GATE] = ma35d1_clk_gate(dev, "gpn_gate", "hclk0",
					clk_base + REG_CLK_SYSCLK1, 29);

	hws[TMR0_MUX] = ma35d1_clk_mux(dev, "tmr0_mux", clk_base + REG_CLK_CLKSEL1,
				       0, 3, timer0_sel_clks,
				       ARRAY_SIZE(timer0_sel_clks));
	hws[TMR0_GATE] = ma35d1_clk_gate(dev, "tmr0_gate", "tmr0_mux",
					 clk_base + REG_CLK_APBCLK0, 0);
	hws[TMR1_MUX] = ma35d1_clk_mux(dev, "tmr1_mux", clk_base + REG_CLK_CLKSEL1,
				       4, 3, timer1_sel_clks,
				       ARRAY_SIZE(timer1_sel_clks));
	hws[TMR1_GATE] = ma35d1_clk_gate(dev, "tmr1_gate", "tmr1_mux",
					 clk_base + REG_CLK_APBCLK0, 1);
	hws[TMR2_MUX] = ma35d1_clk_mux(dev, "tmr2_mux", clk_base + REG_CLK_CLKSEL1,
				       8, 3, timer2_sel_clks,
				       ARRAY_SIZE(timer2_sel_clks));
	hws[TMR2_GATE] = ma35d1_clk_gate(dev, "tmr2_gate", "tmr2_mux",
					 clk_base + REG_CLK_APBCLK0, 2);
	hws[TMR3_MUX] = ma35d1_clk_mux(dev, "tmr3_mux", clk_base + REG_CLK_CLKSEL1,
				       12, 3, timer3_sel_clks,
				       ARRAY_SIZE(timer3_sel_clks));
	hws[TMR3_GATE] = ma35d1_clk_gate(dev, "tmr3_gate", "tmr3_mux",
					 clk_base + REG_CLK_APBCLK0, 3);
	hws[TMR4_MUX] = ma35d1_clk_mux(dev, "tmr4_mux", clk_base + REG_CLK_CLKSEL1,
				       16, 3, timer4_sel_clks,
				       ARRAY_SIZE(timer4_sel_clks));
	hws[TMR4_GATE] = ma35d1_clk_gate(dev, "tmr4_gate", "tmr4_mux",
					 clk_base + REG_CLK_APBCLK0, 4);
	hws[TMR5_MUX] = ma35d1_clk_mux(dev, "tmr5_mux", clk_base + REG_CLK_CLKSEL1,
				       20, 3, timer5_sel_clks,
				       ARRAY_SIZE(timer5_sel_clks));
	hws[TMR5_GATE] = ma35d1_clk_gate(dev, "tmr5_gate", "tmr5_mux",
					 clk_base + REG_CLK_APBCLK0, 5);
	hws[TMR6_MUX] = ma35d1_clk_mux(dev, "tmr6_mux", clk_base + REG_CLK_CLKSEL1,
				       24, 3, timer6_sel_clks,
				       ARRAY_SIZE(timer6_sel_clks));
	hws[TMR6_GATE] = ma35d1_clk_gate(dev, "tmr6_gate", "tmr6_mux",
					 clk_base + REG_CLK_APBCLK0, 6);
	hws[TMR7_MUX] = ma35d1_clk_mux(dev, "tmr7_mux", clk_base + REG_CLK_CLKSEL1,
				       28, 3, timer7_sel_clks,
				       ARRAY_SIZE(timer7_sel_clks));
	hws[TMR7_GATE] = ma35d1_clk_gate(dev, "tmr7_gate", "tmr7_mux",
					 clk_base + REG_CLK_APBCLK0, 7);
	hws[TMR8_MUX] = ma35d1_clk_mux(dev, "tmr8_mux", clk_base + REG_CLK_CLKSEL2,
				       0, 3, timer8_sel_clks,
				       ARRAY_SIZE(timer8_sel_clks));
	hws[TMR8_GATE] = ma35d1_clk_gate(dev, "tmr8_gate", "tmr8_mux",
					 clk_base + REG_CLK_APBCLK0, 8);
	hws[TMR9_MUX] = ma35d1_clk_mux(dev, "tmr9_mux", clk_base + REG_CLK_CLKSEL2,
				       4, 3, timer9_sel_clks,
				       ARRAY_SIZE(timer9_sel_clks));
	hws[TMR9_GATE] = ma35d1_clk_gate(dev, "tmr9_gate", "tmr9_mux",
					 clk_base + REG_CLK_APBCLK0, 9);
	hws[TMR10_MUX] = ma35d1_clk_mux(dev, "tmr10_mux", clk_base + REG_CLK_CLKSEL2,
					8, 3, timer10_sel_clks,
					ARRAY_SIZE(timer10_sel_clks));
	hws[TMR10_GATE] = ma35d1_clk_gate(dev, "tmr10_gate", "tmr10_mux",
					  clk_base + REG_CLK_APBCLK0, 10);
	hws[TMR11_MUX] = ma35d1_clk_mux(dev, "tmr11_mux", clk_base + REG_CLK_CLKSEL2,
					12, 3, timer11_sel_clks,
					ARRAY_SIZE(timer11_sel_clks));
	hws[TMR11_GATE] = ma35d1_clk_gate(dev, "tmr11_gate", "tmr11_mux",
					  clk_base + REG_CLK_APBCLK0, 11);

	hws[UART0_MUX] = ma35d1_clk_mux(dev, "uart0_mux", clk_base + REG_CLK_CLKSEL2,
					16, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART0_DIV] = ma35d1_clk_divider(dev, "uart0_div", "uart0_mux",
					    clk_base + REG_CLK_CLKDIV1,
					    16, 4);
	hws[UART0_GATE] = ma35d1_clk_gate(dev, "uart0_gate", "uart0_div",
					  clk_base + REG_CLK_APBCLK0, 12);
	hws[UART1_MUX] = ma35d1_clk_mux(dev, "uart1_mux", clk_base + REG_CLK_CLKSEL2,
					18, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART1_DIV] = ma35d1_clk_divider(dev, "uart1_div", "uart1_mux",
					    clk_base + REG_CLK_CLKDIV1,
					    20, 4);
	hws[UART1_GATE] = ma35d1_clk_gate(dev, "uart1_gate", "uart1_div",
					  clk_base + REG_CLK_APBCLK0, 13);
	hws[UART2_MUX] = ma35d1_clk_mux(dev, "uart2_mux", clk_base + REG_CLK_CLKSEL2,
					20, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART2_DIV] = ma35d1_clk_divider(dev, "uart2_div", "uart2_mux",
					    clk_base + REG_CLK_CLKDIV1,
					    24, 4);
	hws[UART2_GATE] = ma35d1_clk_gate(dev, "uart2_gate", "uart2_div",
					  clk_base + REG_CLK_APBCLK0, 14);
	hws[UART3_MUX] = ma35d1_clk_mux(dev, "uart3_mux", clk_base + REG_CLK_CLKSEL2,
					22, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART3_DIV] = ma35d1_clk_divider(dev, "uart3_div", "uart3_mux",
					    clk_base + REG_CLK_CLKDIV1,
					    28, 4);
	hws[UART3_GATE] = ma35d1_clk_gate(dev, "uart3_gate", "uart3_div",
					  clk_base + REG_CLK_APBCLK0, 15);
	hws[UART4_MUX] = ma35d1_clk_mux(dev, "uart4_mux", clk_base + REG_CLK_CLKSEL2,
					24, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART4_DIV] = ma35d1_clk_divider(dev, "uart4_div", "uart4_mux",
					    clk_base + REG_CLK_CLKDIV2,
					    0, 4);
	hws[UART4_GATE] = ma35d1_clk_gate(dev, "uart4_gate", "uart4_div",
					  clk_base + REG_CLK_APBCLK0, 16);
	hws[UART5_MUX] = ma35d1_clk_mux(dev, "uart5_mux", clk_base + REG_CLK_CLKSEL2,
					26, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART5_DIV] = ma35d1_clk_divider(dev, "uart5_div", "uart5_mux",
					    clk_base + REG_CLK_CLKDIV2,
					    4, 4);
	hws[UART5_GATE] = ma35d1_clk_gate(dev, "uart5_gate", "uart5_div",
					  clk_base + REG_CLK_APBCLK0, 17);
	hws[UART6_MUX] = ma35d1_clk_mux(dev, "uart6_mux", clk_base + REG_CLK_CLKSEL2,
					28, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART6_DIV] = ma35d1_clk_divider(dev, "uart6_div", "uart6_mux",
					    clk_base + REG_CLK_CLKDIV2,
					    8, 4);
	hws[UART6_GATE] = ma35d1_clk_gate(dev, "uart6_gate", "uart6_div",
					  clk_base + REG_CLK_APBCLK0, 18);
	hws[UART7_MUX] = ma35d1_clk_mux(dev, "uart7_mux", clk_base + REG_CLK_CLKSEL2,
					30, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART7_DIV] = ma35d1_clk_divider(dev, "uart7_div", "uart7_mux",
					    clk_base + REG_CLK_CLKDIV2,
					    12, 4);
	hws[UART7_GATE] = ma35d1_clk_gate(dev, "uart7_gate", "uart7_div",
					  clk_base + REG_CLK_APBCLK0, 19);
	hws[UART8_MUX] = ma35d1_clk_mux(dev, "uart8_mux", clk_base + REG_CLK_CLKSEL3,
					0, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART8_DIV] = ma35d1_clk_divider(dev, "uart8_div", "uart8_mux",
					    clk_base + REG_CLK_CLKDIV2,
					    16, 4);
	hws[UART8_GATE] = ma35d1_clk_gate(dev, "uart8_gate", "uart8_div",
					  clk_base + REG_CLK_APBCLK0, 20);
	hws[UART9_MUX] = ma35d1_clk_mux(dev, "uart9_mux", clk_base + REG_CLK_CLKSEL3,
					2, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART9_DIV] = ma35d1_clk_divider(dev, "uart9_div", "uart9_mux",
					    clk_base + REG_CLK_CLKDIV2,
					    20, 4);
	hws[UART9_GATE] = ma35d1_clk_gate(dev, "uart9_gate", "uart9_div",
					  clk_base + REG_CLK_APBCLK0, 21);
	hws[UART10_MUX] = ma35d1_clk_mux(dev, "uart10_mux", clk_base + REG_CLK_CLKSEL3,
					 4, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART10_DIV] = ma35d1_clk_divider(dev, "uart10_div", "uart10_mux",
					     clk_base + REG_CLK_CLKDIV2,
					     24, 4);
	hws[UART10_GATE] = ma35d1_clk_gate(dev, "uart10_gate", "uart10_div",
					   clk_base + REG_CLK_APBCLK0, 22);
	hws[UART11_MUX] = ma35d1_clk_mux(dev, "uart11_mux", clk_base + REG_CLK_CLKSEL3,
					 6, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART11_DIV] = ma35d1_clk_divider(dev, "uart11_div", "uart11_mux",
					     clk_base + REG_CLK_CLKDIV2,
					     28, 4);
	hws[UART11_GATE] = ma35d1_clk_gate(dev, "uart11_gate", "uart11_div",
					   clk_base + REG_CLK_APBCLK0, 23);
	hws[UART12_MUX] = ma35d1_clk_mux(dev, "uart12_mux", clk_base + REG_CLK_CLKSEL3,
					 8, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART12_DIV] = ma35d1_clk_divider(dev, "uart12_div", "uart12_mux",
					     clk_base + REG_CLK_CLKDIV3,
					     0, 4);
	hws[UART12_GATE] = ma35d1_clk_gate(dev, "uart12_gate", "uart12_div",
					   clk_base + REG_CLK_APBCLK0, 24);
	hws[UART13_MUX] = ma35d1_clk_mux(dev, "uart13_mux", clk_base + REG_CLK_CLKSEL3,
					 10, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART13_DIV] = ma35d1_clk_divider(dev, "uart13_div", "uart13_mux",
					     clk_base + REG_CLK_CLKDIV3,
					     4, 4);
	hws[UART13_GATE] = ma35d1_clk_gate(dev, "uart13_gate", "uart13_div",
					   clk_base + REG_CLK_APBCLK0, 25);
	hws[UART14_MUX] = ma35d1_clk_mux(dev, "uart14_mux", clk_base + REG_CLK_CLKSEL3,
					 12, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART14_DIV] = ma35d1_clk_divider(dev, "uart14_div", "uart14_mux",
					     clk_base + REG_CLK_CLKDIV3,
					     8, 4);
	hws[UART14_GATE] = ma35d1_clk_gate(dev, "uart14_gate", "uart14_div",
					   clk_base + REG_CLK_APBCLK0, 26);
	hws[UART15_MUX] = ma35d1_clk_mux(dev, "uart15_mux", clk_base + REG_CLK_CLKSEL3,
					 14, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART15_DIV] = ma35d1_clk_divider(dev, "uart15_div", "uart15_mux",
					     clk_base + REG_CLK_CLKDIV3,
					     12, 4);
	hws[UART15_GATE] = ma35d1_clk_gate(dev, "uart15_gate", "uart15_div",
					   clk_base + REG_CLK_APBCLK0, 27);
	hws[UART16_MUX] = ma35d1_clk_mux(dev, "uart16_mux", clk_base + REG_CLK_CLKSEL3,
					 16, 2, uart_sel_clks, ARRAY_SIZE(uart_sel_clks));
	hws[UART16_DIV] = ma35d1_clk_divider(dev, "uart16_div", "uart16_mux",
					     clk_base + REG_CLK_CLKDIV3,
					     16, 4);
	hws[UART16_GATE] = ma35d1_clk_gate(dev, "uart16_gate", "uart16_div",
					   clk_base + REG_CLK_APBCLK0, 28);

	hws[RTC_GATE] = ma35d1_clk_gate(dev, "rtc_gate", "lxt",
					clk_base + REG_CLK_APBCLK0, 29);
	hws[DDR_GATE] = ma35d1_clk_gate(dev, "ddr_gate", "ddrpll",
					clk_base + REG_CLK_APBCLK0, 30);

	hws[KPI_MUX] = ma35d1_clk_mux(dev, "kpi_mux", clk_base + REG_CLK_CLKSEL4,
				      30, 1, kpi_sel_clks, ARRAY_SIZE(kpi_sel_clks));
	hws[KPI_DIV] = ma35d1_clk_divider(dev, "kpi_div", "kpi_mux",
					  clk_base + REG_CLK_CLKDIV4,
					  24, 8);
	hws[KPI_GATE] = ma35d1_clk_gate(dev, "kpi_gate", "kpi_div",
					clk_base + REG_CLK_APBCLK0, 31);

	hws[I2C0_GATE] = ma35d1_clk_gate(dev, "i2c0_gate", "pclk0",
					 clk_base + REG_CLK_APBCLK1, 0);
	hws[I2C1_GATE] = ma35d1_clk_gate(dev, "i2c1_gate", "pclk1",
					 clk_base + REG_CLK_APBCLK1, 1);
	hws[I2C2_GATE] = ma35d1_clk_gate(dev, "i2c2_gate", "pclk2",
					 clk_base + REG_CLK_APBCLK1, 2);
	hws[I2C3_GATE] = ma35d1_clk_gate(dev, "i2c3_gate", "pclk0",
					 clk_base + REG_CLK_APBCLK1, 3);
	hws[I2C4_GATE] = ma35d1_clk_gate(dev, "i2c4_gate", "pclk1",
					 clk_base + REG_CLK_APBCLK1, 4);
	hws[I2C5_GATE] = ma35d1_clk_gate(dev, "i2c5_gate", "pclk2",
					 clk_base + REG_CLK_APBCLK1, 5);

	hws[QSPI0_MUX] = ma35d1_clk_mux(dev, "qspi0_mux", clk_base + REG_CLK_CLKSEL4,
					8, 2, qspi0_sel_clks, ARRAY_SIZE(qspi0_sel_clks));
	hws[QSPI0_GATE] = ma35d1_clk_gate(dev, "qspi0_gate", "qspi0_mux",
					  clk_base + REG_CLK_APBCLK1, 6);
	hws[QSPI1_MUX] = ma35d1_clk_mux(dev, "qspi1_mux", clk_base + REG_CLK_CLKSEL4,
					10, 2, qspi1_sel_clks, ARRAY_SIZE(qspi1_sel_clks));
	hws[QSPI1_GATE] = ma35d1_clk_gate(dev, "qspi1_gate", "qspi1_mux",
					  clk_base + REG_CLK_APBCLK1, 7);

	hws[SMC0_MUX] = ma35d1_clk_mux(dev, "smc0_mux", clk_base + REG_CLK_CLKSEL4,
					28, 1, smc_sel_clks, ARRAY_SIZE(smc_sel_clks));
	hws[SMC0_DIV] = ma35d1_clk_divider(dev, "smc0_div", "smc0_mux",
					   clk_base + REG_CLK_CLKDIV1,
					   0, 4);
	hws[SMC0_GATE] = ma35d1_clk_gate(dev, "smc0_gate", "smc0_div",
					 clk_base + REG_CLK_APBCLK1, 12);
	hws[SMC1_MUX] = ma35d1_clk_mux(dev, "smc1_mux", clk_base + REG_CLK_CLKSEL4,
					 29, 1, smc_sel_clks, ARRAY_SIZE(smc_sel_clks));
	hws[SMC1_DIV] = ma35d1_clk_divider(dev, "smc1_div", "smc1_mux",
					   clk_base + REG_CLK_CLKDIV1,
					   4, 4);
	hws[SMC1_GATE] = ma35d1_clk_gate(dev, "smc1_gate", "smc1_div",
					 clk_base + REG_CLK_APBCLK1, 13);

	hws[WDT0_MUX] = ma35d1_clk_mux(dev, "wdt0_mux", clk_base + REG_CLK_CLKSEL3,
				       20, 2, wdt0_sel_clks, ARRAY_SIZE(wdt0_sel_clks));
	hws[WDT0_GATE] = ma35d1_clk_gate(dev, "wdt0_gate", "wdt0_mux",
					 clk_base + REG_CLK_APBCLK1, 16);
	hws[WDT1_MUX] = ma35d1_clk_mux(dev, "wdt1_mux", clk_base + REG_CLK_CLKSEL3,
				       24, 2, wdt1_sel_clks, ARRAY_SIZE(wdt1_sel_clks));
	hws[WDT1_GATE] = ma35d1_clk_gate(dev, "wdt1_gate", "wdt1_mux",
					 clk_base + REG_CLK_APBCLK1, 17);
	hws[WDT2_MUX] = ma35d1_clk_mux(dev, "wdt2_mux", clk_base + REG_CLK_CLKSEL3,
				       28, 2, wdt2_sel_clks, ARRAY_SIZE(wdt2_sel_clks));
	hws[WDT2_GATE] = ma35d1_clk_gate(dev, "wdt2_gate", "wdt2_mux",
				       clk_base + REG_CLK_APBCLK1, 18);

	hws[WWDT0_MUX] = ma35d1_clk_mux(dev, "wwdt0_mux", clk_base + REG_CLK_CLKSEL3,
					22, 2, wwdt0_sel_clks, ARRAY_SIZE(wwdt0_sel_clks));
	hws[WWDT1_MUX] = ma35d1_clk_mux(dev, "wwdt1_mux", clk_base + REG_CLK_CLKSEL3,
					26, 2, wwdt1_sel_clks, ARRAY_SIZE(wwdt1_sel_clks));
	hws[WWDT2_MUX] = ma35d1_clk_mux(dev, "wwdt2_mux", clk_base + REG_CLK_CLKSEL3,
					30, 2, wwdt2_sel_clks, ARRAY_SIZE(wwdt2_sel_clks));

	hws[EPWM0_GATE] = ma35d1_clk_gate(dev, "epwm0_gate", "pclk1",
					  clk_base + REG_CLK_APBCLK1, 24);
	hws[EPWM1_GATE] = ma35d1_clk_gate(dev, "epwm1_gate", "pclk2",
					  clk_base + REG_CLK_APBCLK1, 25);
	hws[EPWM2_GATE] = ma35d1_clk_gate(dev, "epwm2_gate", "pclk1",
					  clk_base + REG_CLK_APBCLK1, 26);

	hws[I2S0_MUX] = ma35d1_clk_mux(dev, "i2s0_mux", clk_base + REG_CLK_CLKSEL4,
				       12, 2, i2s0_sel_clks, ARRAY_SIZE(i2s0_sel_clks));
	hws[I2S0_GATE] = ma35d1_clk_gate(dev, "i2s0_gate", "i2s0_mux",
					 clk_base + REG_CLK_APBCLK2, 0);
	hws[I2S1_MUX] = ma35d1_clk_mux(dev, "i2s1_mux", clk_base + REG_CLK_CLKSEL4,
				       14, 2, i2s1_sel_clks, ARRAY_SIZE(i2s1_sel_clks));
	hws[I2S1_GATE] = ma35d1_clk_gate(dev, "i2s1_gate", "i2s1_mux",
					 clk_base + REG_CLK_APBCLK2, 1);

	hws[SSMCC_GATE] = ma35d1_clk_gate(dev, "ssmcc_gate", "pclk3",
					  clk_base + REG_CLK_APBCLK2, 2);
	hws[SSPCC_GATE] = ma35d1_clk_gate(dev, "sspcc_gate", "pclk3",
					  clk_base + REG_CLK_APBCLK2, 3);

	hws[SPI0_MUX] = ma35d1_clk_mux(dev, "spi0_mux", clk_base + REG_CLK_CLKSEL4,
				       0, 2, spi0_sel_clks, ARRAY_SIZE(spi0_sel_clks));
	hws[SPI0_GATE] = ma35d1_clk_gate(dev, "spi0_gate", "spi0_mux",
					 clk_base + REG_CLK_APBCLK2, 4);
	hws[SPI1_MUX] = ma35d1_clk_mux(dev, "spi1_mux", clk_base + REG_CLK_CLKSEL4,
				       2, 2, spi1_sel_clks, ARRAY_SIZE(spi1_sel_clks));
	hws[SPI1_GATE] = ma35d1_clk_gate(dev, "spi1_gate", "spi1_mux",
					 clk_base + REG_CLK_APBCLK2, 5);
	hws[SPI2_MUX] = ma35d1_clk_mux(dev, "spi2_mux", clk_base + REG_CLK_CLKSEL4,
				       4, 2, spi2_sel_clks, ARRAY_SIZE(spi2_sel_clks));
	hws[SPI2_GATE] = ma35d1_clk_gate(dev, "spi2_gate", "spi2_mux",
					 clk_base + REG_CLK_APBCLK2, 6);
	hws[SPI3_MUX] = ma35d1_clk_mux(dev, "spi3_mux", clk_base + REG_CLK_CLKSEL4,
				       6, 2, spi3_sel_clks, ARRAY_SIZE(spi3_sel_clks));
	hws[SPI3_GATE] = ma35d1_clk_gate(dev, "spi3_gate", "spi3_mux",
					 clk_base + REG_CLK_APBCLK2, 7);

	hws[ECAP0_GATE] = ma35d1_clk_gate(dev, "ecap0_gate", "pclk1",
					  clk_base + REG_CLK_APBCLK2, 8);
	hws[ECAP1_GATE] = ma35d1_clk_gate(dev, "ecap1_gate", "pclk2",
					  clk_base + REG_CLK_APBCLK2, 9);
	hws[ECAP2_GATE] = ma35d1_clk_gate(dev, "ecap2_gate", "pclk1",
					  clk_base + REG_CLK_APBCLK2, 10);

	hws[QEI0_GATE] = ma35d1_clk_gate(dev, "qei0_gate", "pclk1",
					 clk_base + REG_CLK_APBCLK2, 12);
	hws[QEI1_GATE] = ma35d1_clk_gate(dev, "qei1_gate", "pclk2",
					 clk_base + REG_CLK_APBCLK2, 13);
	hws[QEI2_GATE] = ma35d1_clk_gate(dev, "qei2_gate", "pclk1",
					 clk_base + REG_CLK_APBCLK2, 14);

	hws[ADC_DIV] = ma35d1_reg_adc_clkdiv(dev, "adc_div", hws[PCLK0],
					     &ma35d1_lock, 0,
					     clk_base + REG_CLK_CLKDIV4,
					     4, 17, 0x1ffff);
	hws[ADC_GATE] = ma35d1_clk_gate(dev, "adc_gate", "adc_div",
					clk_base + REG_CLK_APBCLK2, 24);

	hws[EADC_DIV] = ma35d1_clk_divider_table(dev, "eadc_div", "pclk2",
						 clk_base + REG_CLK_CLKDIV4,
						 0, 4, eadc_div_table);
	hws[EADC_GATE] = ma35d1_clk_gate(dev, "eadc_gate", "eadc_div",
					 clk_base + REG_CLK_APBCLK2, 25);

	return devm_of_clk_add_hw_provider(dev,
					   of_clk_hw_onecell_get,
					   ma35d1_hw_data);
}

static const struct of_device_id ma35d1_clk_of_match[] = {
	{ .compatible = "nuvoton,ma35d1-clk" },
	{ }
};
MODULE_DEVICE_TABLE(of, ma35d1_clk_of_match);

static struct platform_driver ma35d1_clk_driver = {
	.probe = ma35d1_clocks_probe,
	.driver = {
		.name = "ma35d1-clk",
		.of_match_table = ma35d1_clk_of_match,
	},
};

static int __init ma35d1_clocks_init(void)
{
	return platform_driver_register(&ma35d1_clk_driver);
}

postcore_initcall(ma35d1_clocks_init);

MODULE_AUTHOR("Chi-Fang Li <cfli0@nuvoton.com>");
MODULE_DESCRIPTION("NUVOTON MA35D1 Clock Driver");
MODULE_LICENSE("GPL");
