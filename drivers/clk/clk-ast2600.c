// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright IBM Corp
// Copyright ASPEED Technology

#define pr_fmt(fmt) "clk-ast2600: " fmt

#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/clock/ast2600-clock.h>

#include "clk-aspeed.h"

#define ASPEED_G6_NUM_CLKS		71

#define ASPEED_G6_SILICON_REV		0x014
#define CHIP_REVISION_ID			GENMASK(23, 16)

#define ASPEED_G6_RESET_CTRL		0x040
#define ASPEED_G6_RESET_CTRL2		0x050

#define ASPEED_G6_CLK_STOP_CTRL		0x080
#define ASPEED_G6_CLK_STOP_CTRL2	0x090

#define ASPEED_G6_MISC_CTRL		0x0C0
#define  UART_DIV13_EN			BIT(12)

#define ASPEED_G6_CLK_SELECTION1	0x300
#define ASPEED_G6_CLK_SELECTION2	0x304
#define ASPEED_G6_CLK_SELECTION4	0x310

#define ASPEED_HPLL_PARAM		0x200
#define ASPEED_APLL_PARAM		0x210
#define ASPEED_MPLL_PARAM		0x220
#define ASPEED_EPLL_PARAM		0x240
#define ASPEED_DPLL_PARAM		0x260

#define ASPEED_G6_STRAP1		0x500

#define ASPEED_MAC12_CLK_DLY		0x340
#define ASPEED_MAC34_CLK_DLY		0x350

/* Globally visible clocks */
static DEFINE_SPINLOCK(aspeed_g6_clk_lock);

/* Keeps track of all clocks */
static struct clk_hw_onecell_data *aspeed_g6_clk_data;

static void __iomem *scu_g6_base;
/* AST2600 revision: A0, A1, A2, etc */
static u8 soc_rev;

/*
 * Clocks marked with CLK_IS_CRITICAL:
 *
 *  ref0 and ref1 are essential for the SoC to operate
 *  mpll is required if SDRAM is used
 */
static const struct aspeed_gate_data aspeed_g6_gates[] = {
	/*				    clk rst  name		parent	 flags */
	[ASPEED_CLK_GATE_MCLK]		= {  0, -1, "mclk-gate",	"mpll",	 CLK_IS_CRITICAL }, /* SDRAM */
	[ASPEED_CLK_GATE_ECLK]		= {  1,  6, "eclk-gate",	"eclk",	 0 },	/* Video Engine */
	[ASPEED_CLK_GATE_GCLK]		= {  2,  7, "gclk-gate",	NULL,	 0 },	/* 2D engine */
	/* vclk parent - dclk/d1clk/hclk/mclk */
	[ASPEED_CLK_GATE_VCLK]		= {  3, -1, "vclk-gate",	NULL,	 0 },	/* Video Capture */
	[ASPEED_CLK_GATE_BCLK]		= {  4,  8, "bclk-gate",	"bclk",	 0 }, /* PCIe/PCI */
	/* From dpll */
	[ASPEED_CLK_GATE_DCLK]		= {  5, -1, "dclk-gate",	NULL,	 CLK_IS_CRITICAL }, /* DAC */
	[ASPEED_CLK_GATE_REF0CLK]	= {  6, -1, "ref0clk-gate",	"clkin", CLK_IS_CRITICAL },
	[ASPEED_CLK_GATE_USBPORT2CLK]	= {  7,  3, "usb-port2-gate",	NULL,	 0 },	/* USB2.0 Host port 2 */
	/* Reserved 8 */
	[ASPEED_CLK_GATE_USBUHCICLK]	= {  9, 15, "usb-uhci-gate",	NULL,	 0 },	/* USB1.1 (requires port 2 enabled) */
	/* From dpll/epll/40mhz usb p1 phy/gpioc6/dp phy pll */
	[ASPEED_CLK_GATE_D1CLK]		= { 10, 13, "d1clk-gate",	"d1clk", 0 },	/* GFX CRT */
	/* Reserved 11/12 */
	[ASPEED_CLK_GATE_YCLK]		= { 13,  4, "yclk-gate",	NULL,	 0 },	/* HAC */
	[ASPEED_CLK_GATE_USBPORT1CLK]	= { 14, 14, "usb-port1-gate",	NULL,	 0 },	/* USB2 hub/USB2 host port 1/USB1.1 dev */
	[ASPEED_CLK_GATE_UART5CLK]	= { 15, -1, "uart5clk-gate",	"uart",	 0 },	/* UART5 */
	/* Reserved 16/19 */
	[ASPEED_CLK_GATE_MAC1CLK]	= { 20, 11, "mac1clk-gate",	"mac12", 0 },	/* MAC1 */
	[ASPEED_CLK_GATE_MAC2CLK]	= { 21, 12, "mac2clk-gate",	"mac12", 0 },	/* MAC2 */
	/* Reserved 22/23 */
	[ASPEED_CLK_GATE_RSACLK]	= { 24,  4, "rsaclk-gate",	NULL,	 0 },	/* HAC */
	[ASPEED_CLK_GATE_RVASCLK]	= { 25,  9, "rvasclk-gate",	NULL,	 0 },	/* RVAS */
	/* Reserved 26 */
	[ASPEED_CLK_GATE_EMMCCLK]	= { 27, 16, "emmcclk-gate",	NULL,	 0 },	/* For card clk */
	/* Reserved 28/29/30 */
	[ASPEED_CLK_GATE_LCLK]		= { 32, 32, "lclk-gate",	NULL,	 0 }, /* LPC */
	[ASPEED_CLK_GATE_ESPICLK]	= { 33, -1, "espiclk-gate",	NULL,	 0 }, /* eSPI */
	[ASPEED_CLK_GATE_REF1CLK]	= { 34, -1, "ref1clk-gate",	"clkin", CLK_IS_CRITICAL },
	/* Reserved 35 */
	[ASPEED_CLK_GATE_SDCLK]		= { 36, 56, "sdclk-gate",	NULL,	 0 },	/* SDIO/SD */
	[ASPEED_CLK_GATE_LHCCLK]	= { 37, -1, "lhclk-gate",	"lhclk", 0 },	/* LPC master/LPC+ */
	/* Reserved 38 RSA: no longer used */
	/* Reserved 39 */
	[ASPEED_CLK_GATE_I3C0CLK]	= { 40,  40, "i3c0clk-gate",	NULL,	 0 },	/* I3C0 */
	[ASPEED_CLK_GATE_I3C1CLK]	= { 41,  41, "i3c1clk-gate",	NULL,	 0 },	/* I3C1 */
	[ASPEED_CLK_GATE_I3C2CLK]	= { 42,  42, "i3c2clk-gate",	NULL,	 0 },	/* I3C2 */
	[ASPEED_CLK_GATE_I3C3CLK]	= { 43,  43, "i3c3clk-gate",	NULL,	 0 },	/* I3C3 */
	[ASPEED_CLK_GATE_I3C4CLK]	= { 44,  44, "i3c4clk-gate",	NULL,	 0 },	/* I3C4 */
	[ASPEED_CLK_GATE_I3C5CLK]	= { 45,  45, "i3c5clk-gate",	NULL,	 0 },	/* I3C5 */
	[ASPEED_CLK_GATE_I3C6CLK]	= { 46,  46, "i3c6clk-gate",	NULL,	 0 },	/* I3C6 */
	[ASPEED_CLK_GATE_I3C7CLK]	= { 47,  47, "i3c7clk-gate",	NULL,	 0 },	/* I3C7 */
	[ASPEED_CLK_GATE_UART1CLK]	= { 48,  -1, "uart1clk-gate",	"uart",	 0 },	/* UART1 */
	[ASPEED_CLK_GATE_UART2CLK]	= { 49,  -1, "uart2clk-gate",	"uart",	 0 },	/* UART2 */
	[ASPEED_CLK_GATE_UART3CLK]	= { 50,  -1, "uart3clk-gate",	"uart",  0 },	/* UART3 */
	[ASPEED_CLK_GATE_UART4CLK]	= { 51,  -1, "uart4clk-gate",	"uart",	 0 },	/* UART4 */
	[ASPEED_CLK_GATE_MAC3CLK]	= { 52,  52, "mac3clk-gate",	"mac34", 0 },	/* MAC3 */
	[ASPEED_CLK_GATE_MAC4CLK]	= { 53,  53, "mac4clk-gate",	"mac34", 0 },	/* MAC4 */
	[ASPEED_CLK_GATE_UART6CLK]	= { 54,  -1, "uart6clk-gate",	"uartx", 0 },	/* UART6 */
	[ASPEED_CLK_GATE_UART7CLK]	= { 55,  -1, "uart7clk-gate",	"uartx", 0 },	/* UART7 */
	[ASPEED_CLK_GATE_UART8CLK]	= { 56,  -1, "uart8clk-gate",	"uartx", 0 },	/* UART8 */
	[ASPEED_CLK_GATE_UART9CLK]	= { 57,  -1, "uart9clk-gate",	"uartx", 0 },	/* UART9 */
	[ASPEED_CLK_GATE_UART10CLK]	= { 58,  -1, "uart10clk-gate",	"uartx", 0 },	/* UART10 */
	[ASPEED_CLK_GATE_UART11CLK]	= { 59,  -1, "uart11clk-gate",	"uartx", 0 },	/* UART11 */
	[ASPEED_CLK_GATE_UART12CLK]	= { 60,  -1, "uart12clk-gate",	"uartx", 0 },	/* UART12 */
	[ASPEED_CLK_GATE_UART13CLK]	= { 61,  -1, "uart13clk-gate",	"uartx", 0 },	/* UART13 */
	[ASPEED_CLK_GATE_FSICLK]	= { 62,  59, "fsiclk-gate",	NULL,	 0 },	/* FSI */
};

static const struct clk_div_table ast2600_eclk_div_table[] = {
	{ 0x0, 2 },
	{ 0x1, 2 },
	{ 0x2, 3 },
	{ 0x3, 4 },
	{ 0x4, 5 },
	{ 0x5, 6 },
	{ 0x6, 7 },
	{ 0x7, 8 },
	{ 0 }
};

static const struct clk_div_table ast2600_emmc_extclk_div_table[] = {
	{ 0x0, 2 },
	{ 0x1, 4 },
	{ 0x2, 6 },
	{ 0x3, 8 },
	{ 0x4, 10 },
	{ 0x5, 12 },
	{ 0x6, 14 },
	{ 0x7, 16 },
	{ 0 }
};

static const struct clk_div_table ast2600_mac_div_table[] = {
	{ 0x0, 4 },
	{ 0x1, 4 },
	{ 0x2, 6 },
	{ 0x3, 8 },
	{ 0x4, 10 },
	{ 0x5, 12 },
	{ 0x6, 14 },
	{ 0x7, 16 },
	{ 0 }
};

static const struct clk_div_table ast2600_div_table[] = {
	{ 0x0, 4 },
	{ 0x1, 8 },
	{ 0x2, 12 },
	{ 0x3, 16 },
	{ 0x4, 20 },
	{ 0x5, 24 },
	{ 0x6, 28 },
	{ 0x7, 32 },
	{ 0 }
};

/* For hpll/dpll/epll/mpll */
static struct clk_hw *ast2600_calc_pll(const char *name, u32 val)
{
	unsigned int mult, div;

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = div = 1;
	} else {
		/* F = 25Mhz * [(M + 2) / (n + 1)] / (p + 1) */
		u32 m = val  & 0x1fff;
		u32 n = (val >> 13) & 0x3f;
		u32 p = (val >> 19) & 0xf;
		mult = (m + 1) / (n + 1);
		div = (p + 1);
	}
	return clk_hw_register_fixed_factor(NULL, name, "clkin", 0,
			mult, div);
};

static struct clk_hw *ast2600_calc_apll(const char *name, u32 val)
{
	unsigned int mult, div;

	if (soc_rev >= 2) {
		if (val & BIT(24)) {
			/* Pass through mode */
			mult = div = 1;
		} else {
			/* F = 25Mhz * [(m + 1) / (n + 1)] / (p + 1) */
			u32 m = val & 0x1fff;
			u32 n = (val >> 13) & 0x3f;
			u32 p = (val >> 19) & 0xf;

			mult = (m + 1);
			div = (n + 1) * (p + 1);
		}
	} else {
		if (val & BIT(20)) {
			/* Pass through mode */
			mult = div = 1;
		} else {
			/* F = 25Mhz * (2-od) * [(m + 2) / (n + 1)] */
			u32 m = (val >> 5) & 0x3f;
			u32 od = (val >> 4) & 0x1;
			u32 n = val & 0xf;

			mult = (2 - od) * (m + 2);
			div = n + 1;
		}
	}
	return clk_hw_register_fixed_factor(NULL, name, "clkin", 0,
			mult, div);
};

static u32 get_bit(u8 idx)
{
	return BIT(idx % 32);
}

static u32 get_reset_reg(struct aspeed_clk_gate *gate)
{
	if (gate->reset_idx < 32)
		return ASPEED_G6_RESET_CTRL;

	return ASPEED_G6_RESET_CTRL2;
}

static u32 get_clock_reg(struct aspeed_clk_gate *gate)
{
	if (gate->clock_idx < 32)
		return ASPEED_G6_CLK_STOP_CTRL;

	return ASPEED_G6_CLK_STOP_CTRL2;
}

static int aspeed_g6_clk_is_enabled(struct clk_hw *hw)
{
	struct aspeed_clk_gate *gate = to_aspeed_clk_gate(hw);
	u32 clk = get_bit(gate->clock_idx);
	u32 rst = get_bit(gate->reset_idx);
	u32 reg;
	u32 enval;

	/*
	 * If the IP is in reset, treat the clock as not enabled,
	 * this happens with some clocks such as the USB one when
	 * coming from cold reset. Without this, aspeed_clk_enable()
	 * will fail to lift the reset.
	 */
	if (gate->reset_idx >= 0) {
		regmap_read(gate->map, get_reset_reg(gate), &reg);

		if (reg & rst)
			return 0;
	}

	regmap_read(gate->map, get_clock_reg(gate), &reg);

	enval = (gate->flags & CLK_GATE_SET_TO_DISABLE) ? 0 : clk;

	return ((reg & clk) == enval) ? 1 : 0;
}

static int aspeed_g6_clk_enable(struct clk_hw *hw)
{
	struct aspeed_clk_gate *gate = to_aspeed_clk_gate(hw);
	unsigned long flags;
	u32 clk = get_bit(gate->clock_idx);
	u32 rst = get_bit(gate->reset_idx);

	spin_lock_irqsave(gate->lock, flags);

	if (aspeed_g6_clk_is_enabled(hw)) {
		spin_unlock_irqrestore(gate->lock, flags);
		return 0;
	}

	if (gate->reset_idx >= 0) {
		/* Put IP in reset */
		regmap_write(gate->map, get_reset_reg(gate), rst);
		/* Delay 100us */
		udelay(100);
	}

	/* Enable clock */
	if (gate->flags & CLK_GATE_SET_TO_DISABLE) {
		/* Clock is clear to enable, so use set to clear register */
		regmap_write(gate->map, get_clock_reg(gate) + 0x04, clk);
	} else {
		/* Clock is set to enable, so use write to set register */
		regmap_write(gate->map, get_clock_reg(gate), clk);
	}

	if (gate->reset_idx >= 0) {
		/* A delay of 10ms is specified by the ASPEED docs */
		mdelay(10);
		/* Take IP out of reset */
		regmap_write(gate->map, get_reset_reg(gate) + 0x4, rst);
	}

	spin_unlock_irqrestore(gate->lock, flags);

	return 0;
}

static void aspeed_g6_clk_disable(struct clk_hw *hw)
{
	struct aspeed_clk_gate *gate = to_aspeed_clk_gate(hw);
	unsigned long flags;
	u32 clk = get_bit(gate->clock_idx);

	spin_lock_irqsave(gate->lock, flags);

	if (gate->flags & CLK_GATE_SET_TO_DISABLE) {
		regmap_write(gate->map, get_clock_reg(gate), clk);
	} else {
		/* Use set to clear register */
		regmap_write(gate->map, get_clock_reg(gate) + 0x4, clk);
	}

	spin_unlock_irqrestore(gate->lock, flags);
}

static const struct clk_ops aspeed_g6_clk_gate_ops = {
	.enable = aspeed_g6_clk_enable,
	.disable = aspeed_g6_clk_disable,
	.is_enabled = aspeed_g6_clk_is_enabled,
};

static int aspeed_g6_reset_deassert(struct reset_controller_dev *rcdev,
				    unsigned long id)
{
	struct aspeed_reset *ar = to_aspeed_reset(rcdev);
	u32 rst = get_bit(id);
	u32 reg = id >= 32 ? ASPEED_G6_RESET_CTRL2 : ASPEED_G6_RESET_CTRL;

	/* Use set to clear register */
	return regmap_write(ar->map, reg + 0x04, rst);
}

static int aspeed_g6_reset_assert(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct aspeed_reset *ar = to_aspeed_reset(rcdev);
	u32 rst = get_bit(id);
	u32 reg = id >= 32 ? ASPEED_G6_RESET_CTRL2 : ASPEED_G6_RESET_CTRL;

	return regmap_write(ar->map, reg, rst);
}

static int aspeed_g6_reset_status(struct reset_controller_dev *rcdev,
				  unsigned long id)
{
	struct aspeed_reset *ar = to_aspeed_reset(rcdev);
	int ret;
	u32 val;
	u32 rst = get_bit(id);
	u32 reg = id >= 32 ? ASPEED_G6_RESET_CTRL2 : ASPEED_G6_RESET_CTRL;

	ret = regmap_read(ar->map, reg, &val);
	if (ret)
		return ret;

	return !!(val & rst);
}

static const struct reset_control_ops aspeed_g6_reset_ops = {
	.assert = aspeed_g6_reset_assert,
	.deassert = aspeed_g6_reset_deassert,
	.status = aspeed_g6_reset_status,
};

static struct clk_hw *aspeed_g6_clk_hw_register_gate(struct device *dev,
		const char *name, const char *parent_name, unsigned long flags,
		struct regmap *map, u8 clock_idx, u8 reset_idx,
		u8 clk_gate_flags, spinlock_t *lock)
{
	struct aspeed_clk_gate *gate;
	struct clk_init_data init;
	struct clk_hw *hw;
	int ret;

	gate = kzalloc(sizeof(*gate), GFP_KERNEL);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &aspeed_g6_clk_gate_ops;
	init.flags = flags;
	init.parent_names = parent_name ? &parent_name : NULL;
	init.num_parents = parent_name ? 1 : 0;

	gate->map = map;
	gate->clock_idx = clock_idx;
	gate->reset_idx = reset_idx;
	gate->flags = clk_gate_flags;
	gate->lock = lock;
	gate->hw.init = &init;

	hw = &gate->hw;
	ret = clk_hw_register(dev, hw);
	if (ret) {
		kfree(gate);
		hw = ERR_PTR(ret);
	}

	return hw;
}

static const char *const emmc_extclk_parent_names[] = {
	"emmc_extclk_hpll_in",
	"mpll",
};

static const char * const vclk_parent_names[] = {
	"dpll",
	"d1pll",
	"hclk",
	"mclk",
};

static const char * const d1clk_parent_names[] = {
	"dpll",
	"epll",
	"usb-phy-40m",
	"gpioc6_clkin",
	"dp_phy_pll",
};

static int aspeed_g6_clk_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct aspeed_reset *ar;
	struct regmap *map;
	struct clk_hw *hw;
	u32 val, rate;
	int i, ret;

	map = syscon_node_to_regmap(dev->of_node);
	if (IS_ERR(map)) {
		dev_err(dev, "no syscon regmap\n");
		return PTR_ERR(map);
	}

	ar = devm_kzalloc(dev, sizeof(*ar), GFP_KERNEL);
	if (!ar)
		return -ENOMEM;

	ar->map = map;

	ar->rcdev.owner = THIS_MODULE;
	ar->rcdev.nr_resets = 64;
	ar->rcdev.ops = &aspeed_g6_reset_ops;
	ar->rcdev.of_node = dev->of_node;

	ret = devm_reset_controller_register(dev, &ar->rcdev);
	if (ret) {
		dev_err(dev, "could not register reset controller\n");
		return ret;
	}

	/* UART clock div13 setting */
	regmap_read(map, ASPEED_G6_MISC_CTRL, &val);
	if (val & UART_DIV13_EN)
		rate = 24000000 / 13;
	else
		rate = 24000000;
	hw = clk_hw_register_fixed_rate(dev, "uart", NULL, 0, rate);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_UART] = hw;

	/* UART6~13 clock div13 setting */
	regmap_read(map, 0x80, &val);
	if (val & BIT(31))
		rate = 24000000 / 13;
	else
		rate = 24000000;
	hw = clk_hw_register_fixed_rate(dev, "uartx", NULL, 0, rate);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_UARTX] = hw;

	/* EMMC ext clock */
	hw = clk_hw_register_fixed_factor(dev, "emmc_extclk_hpll_in", "hpll",
					  0, 1, 2);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hw = clk_hw_register_mux(dev, "emmc_extclk_mux",
				 emmc_extclk_parent_names,
				 ARRAY_SIZE(emmc_extclk_parent_names), 0,
				 scu_g6_base + ASPEED_G6_CLK_SELECTION1, 11, 1,
				 0, &aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hw = clk_hw_register_gate(dev, "emmc_extclk_gate", "emmc_extclk_mux",
				  0, scu_g6_base + ASPEED_G6_CLK_SELECTION1,
				  15, 0, &aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	hw = clk_hw_register_divider_table(dev, "emmc_extclk",
					   "emmc_extclk_gate", 0,
					   scu_g6_base +
						ASPEED_G6_CLK_SELECTION1, 12,
					   3, 0, ast2600_emmc_extclk_div_table,
					   &aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_EMMC] = hw;

	/* SD/SDIO clock divider and gate */
	hw = clk_hw_register_gate(dev, "sd_extclk_gate", "hpll", 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION4, 31, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	hw = clk_hw_register_divider_table(dev, "sd_extclk", "sd_extclk_gate",
			0, scu_g6_base + ASPEED_G6_CLK_SELECTION4, 28, 3, 0,
			ast2600_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_SDIO] = hw;

	/* MAC1/2 RMII 50MHz RCLK */
	hw = clk_hw_register_fixed_rate(dev, "mac12rclk", "hpll", 0, 50000000);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	/* MAC1/2 AHB bus clock divider */
	hw = clk_hw_register_divider_table(dev, "mac12", "hpll", 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION1, 16, 3, 0,
			ast2600_mac_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MAC12] = hw;

	/* RMII1 50MHz (RCLK) output enable */
	hw = clk_hw_register_gate(dev, "mac1rclk", "mac12rclk", 0,
			scu_g6_base + ASPEED_MAC12_CLK_DLY, 29, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MAC1RCLK] = hw;

	/* RMII2 50MHz (RCLK) output enable */
	hw = clk_hw_register_gate(dev, "mac2rclk", "mac12rclk", 0,
			scu_g6_base + ASPEED_MAC12_CLK_DLY, 30, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MAC2RCLK] = hw;

	/* MAC1/2 RMII 50MHz RCLK */
	hw = clk_hw_register_fixed_rate(dev, "mac34rclk", "hclk", 0, 50000000);
	if (IS_ERR(hw))
		return PTR_ERR(hw);

	/* MAC3/4 AHB bus clock divider */
	hw = clk_hw_register_divider_table(dev, "mac34", "hpll", 0,
			scu_g6_base + 0x310, 24, 3, 0,
			ast2600_mac_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MAC34] = hw;

	/* RMII3 50MHz (RCLK) output enable */
	hw = clk_hw_register_gate(dev, "mac3rclk", "mac34rclk", 0,
			scu_g6_base + ASPEED_MAC34_CLK_DLY, 29, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MAC3RCLK] = hw;

	/* RMII4 50MHz (RCLK) output enable */
	hw = clk_hw_register_gate(dev, "mac4rclk", "mac34rclk", 0,
			scu_g6_base + ASPEED_MAC34_CLK_DLY, 30, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MAC4RCLK] = hw;

	/* LPC Host (LHCLK) clock divider */
	hw = clk_hw_register_divider_table(dev, "lhclk", "hpll", 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION1, 20, 3, 0,
			ast2600_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_LHCLK] = hw;

	/* gfx d1clk : use dp clk */
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION1, GENMASK(10, 8), BIT(10));
	/* SoC Display clock selection */
	hw = clk_hw_register_mux(dev, "d1clk", d1clk_parent_names,
			ARRAY_SIZE(d1clk_parent_names), 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION1, 8, 3, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_D1CLK] = hw;

	/* d1 clk div 0x308[17:15] x [14:12] - 8,7,6,5,4,3,2,1 */
	regmap_write(map, 0x308, 0x12000); /* 3x3 = 9 */

	/* P-Bus (BCLK) clock divider */
	hw = clk_hw_register_divider_table(dev, "bclk", "hpll", 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION1, 20, 3, 0,
			ast2600_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_BCLK] = hw;

	/* Video Capture clock selection */
	hw = clk_hw_register_mux(dev, "vclk", vclk_parent_names,
			ARRAY_SIZE(vclk_parent_names), 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION2, 12, 3, 0,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_VCLK] = hw;

	/* Video Engine clock divider */
	hw = clk_hw_register_divider_table(dev, "eclk", NULL, 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION1, 28, 3, 0,
			ast2600_eclk_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_ECLK] = hw;

	for (i = 0; i < ARRAY_SIZE(aspeed_g6_gates); i++) {
		const struct aspeed_gate_data *gd = &aspeed_g6_gates[i];
		u32 gate_flags;

		/*
		 * Special case: the USB port 1 clock (bit 14) is always
		 * working the opposite way from the other ones.
		 */
		gate_flags = (gd->clock_idx == 14) ? 0 : CLK_GATE_SET_TO_DISABLE;
		hw = aspeed_g6_clk_hw_register_gate(dev,
				gd->name,
				gd->parent_name,
				gd->flags,
				map,
				gd->clock_idx,
				gd->reset_idx,
				gate_flags,
				&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
		aspeed_g6_clk_data->hws[i] = hw;
	}

	return 0;
};

static const struct of_device_id aspeed_g6_clk_dt_ids[] = {
	{ .compatible = "aspeed,ast2600-scu" },
	{ }
};

static struct platform_driver aspeed_g6_clk_driver = {
	.probe  = aspeed_g6_clk_probe,
	.driver = {
		.name = "ast2600-clk",
		.of_match_table = aspeed_g6_clk_dt_ids,
		.suppress_bind_attrs = true,
	},
};
builtin_platform_driver(aspeed_g6_clk_driver);

static const u32 ast2600_a0_axi_ahb_div_table[] = {
	2, 2, 3, 5,
};

static const u32 ast2600_a1_axi_ahb_div0_tbl[] = {
	3, 2, 3, 4,
};

static const u32 ast2600_a1_axi_ahb_div1_tbl[] = {
	3, 4, 6, 8,
};

static const u32 ast2600_a1_axi_ahb200_tbl[] = {
	3, 4, 3, 4, 2, 2, 2, 2,
};

static void __init aspeed_g6_cc(struct regmap *map)
{
	struct clk_hw *hw;
	u32 val, div, divbits, axi_div, ahb_div;

	clk_hw_register_fixed_rate(NULL, "clkin", NULL, 0, 25000000);

	/*
	 * High-speed PLL clock derived from the crystal. This the CPU clock,
	 * and we assume that it is enabled
	 */
	regmap_read(map, ASPEED_HPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_HPLL] = ast2600_calc_pll("hpll", val);

	regmap_read(map, ASPEED_MPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MPLL] = ast2600_calc_pll("mpll", val);

	regmap_read(map, ASPEED_DPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_DPLL] = ast2600_calc_pll("dpll", val);

	regmap_read(map, ASPEED_EPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_EPLL] = ast2600_calc_pll("epll", val);

	regmap_read(map, ASPEED_APLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_APLL] = ast2600_calc_apll("apll", val);

	/* Strap bits 12:11 define the AXI/AHB clock frequency ratio (aka HCLK)*/
	regmap_read(map, ASPEED_G6_STRAP1, &val);
	if (val & BIT(16))
		axi_div = 1;
	else
		axi_div = 2;

	divbits = (val >> 11) & 0x3;
	if (soc_rev >= 1) {
		if (!divbits) {
			ahb_div = ast2600_a1_axi_ahb200_tbl[(val >> 8) & 0x3];
			if (val & BIT(16))
				ahb_div *= 2;
		} else {
			if (val & BIT(16))
				ahb_div = ast2600_a1_axi_ahb_div1_tbl[divbits];
			else
				ahb_div = ast2600_a1_axi_ahb_div0_tbl[divbits];
		}
	} else {
		ahb_div = ast2600_a0_axi_ahb_div_table[(val >> 11) & 0x3];
	}

	hw = clk_hw_register_fixed_factor(NULL, "ahb", "hpll", 0, 1, axi_div * ahb_div);
	aspeed_g6_clk_data->hws[ASPEED_CLK_AHB] = hw;

	regmap_read(map, ASPEED_G6_CLK_SELECTION1, &val);
	val = (val >> 23) & 0x7;
	div = 4 * (val + 1);
	hw = clk_hw_register_fixed_factor(NULL, "apb1", "hpll", 0, 1, div);
	aspeed_g6_clk_data->hws[ASPEED_CLK_APB1] = hw;

	regmap_read(map, ASPEED_G6_CLK_SELECTION4, &val);
	val = (val >> 9) & 0x7;
	div = 2 * (val + 1);
	hw = clk_hw_register_fixed_factor(NULL, "apb2", "ahb", 0, 1, div);
	aspeed_g6_clk_data->hws[ASPEED_CLK_APB2] = hw;

	/* USB 2.0 port1 phy 40MHz clock */
	hw = clk_hw_register_fixed_rate(NULL, "usb-phy-40m", NULL, 0, 40000000);
	aspeed_g6_clk_data->hws[ASPEED_CLK_USBPHY_40M] = hw;
};

static void __init aspeed_g6_cc_init(struct device_node *np)
{
	struct regmap *map;
	int ret;
	int i;

	scu_g6_base = of_iomap(np, 0);
	if (!scu_g6_base)
		return;

	soc_rev = (readl(scu_g6_base + ASPEED_G6_SILICON_REV) & CHIP_REVISION_ID) >> 16;

	aspeed_g6_clk_data = kzalloc(struct_size(aspeed_g6_clk_data, hws,
				      ASPEED_G6_NUM_CLKS), GFP_KERNEL);
	if (!aspeed_g6_clk_data)
		return;

	/*
	 * This way all clocks fetched before the platform device probes,
	 * except those we assign here for early use, will be deferred.
	 */
	for (i = 0; i < ASPEED_G6_NUM_CLKS; i++)
		aspeed_g6_clk_data->hws[i] = ERR_PTR(-EPROBE_DEFER);

	/*
	 * We check that the regmap works on this very first access,
	 * but as this is an MMIO-backed regmap, subsequent regmap
	 * access is not going to fail and we skip error checks from
	 * this point.
	 */
	map = syscon_node_to_regmap(np);
	if (IS_ERR(map)) {
		pr_err("no syscon regmap\n");
		return;
	}

	aspeed_g6_cc(map);
	aspeed_g6_clk_data->num = ASPEED_G6_NUM_CLKS;
	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, aspeed_g6_clk_data);
	if (ret)
		pr_err("failed to add DT provider: %d\n", ret);
};
CLK_OF_DECLARE_DRIVER(aspeed_cc_g6, "aspeed,ast2600-scu", aspeed_g6_cc_init);
