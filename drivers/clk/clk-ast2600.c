// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright IBM Corp
// Copyright ASPEED Technology

#define pr_fmt(fmt) "clk-ast2600: " fmt

#include <linux/bitfield.h>
#include <linux/mfd/syscon.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/slab.h>

#include <dt-bindings/clock/ast2600-clock.h>

#include "clk-aspeed.h"

#define ASPEED_G6_NUM_CLKS		75

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
#define ASPEED_G6_CLK_SELECTION5	0x314
#define   I3C_CLK_SELECTION		BIT(31)
#define     I3C_CLK_SELECT_HCLK		0
#define     I3C_CLK_SELECT_APLL_DIV	1
#define   APLL_DIV_SELECTION		GENMASK(30, 28)
#define     APLL_DIV_2			0b001
#define     APLL_DIV_3			0b010
#define     APLL_DIV_4			0b011
#define     APLL_DIV_5			0b100
#define     APLL_DIV_6			0b101
#define     APLL_DIV_7			0b110
#define     APLL_DIV_8			0b111

#define ASPEED_HPLL_PARAM		0x200
#define ASPEED_APLL_PARAM		0x210
#define ASPEED_MPLL_PARAM		0x220
#define ASPEED_EPLL_PARAM		0x240
#define ASPEED_DPLL_PARAM		0x260

#define ASPEED_G6_STRAP1		0x500

#define ASPEED_UARTCLK_FROM_UXCLK	0x338

#define ASPEED_MAC12_CLK_DLY		0x340
#define ASPEED_MAC12_CLK_DLY_100M	0x348
#define ASPEED_MAC12_CLK_DLY_10M	0x34C

#define ASPEED_MAC34_CLK_DLY		0x350
#define ASPEED_MAC34_CLK_DLY_100M	0x358
#define ASPEED_MAC34_CLK_DLY_10M	0x35C

#define ASPEED_G6_MAC34_DRIVING_CTRL	0x458

#define ASPEED_G6_DEF_MAC12_DELAY_1G	0x0041b75d
#define ASPEED_G6_DEF_MAC12_DELAY_100M	0x00417410
#define ASPEED_G6_DEF_MAC12_DELAY_10M	0x00417410
#define ASPEED_G6_DEF_MAC34_DELAY_1G	0x00104208
#define ASPEED_G6_DEF_MAC34_DELAY_100M	0x00104208
#define ASPEED_G6_DEF_MAC34_DELAY_10M	0x00104208

/* Globally visible clocks */
static DEFINE_SPINLOCK(aspeed_g6_clk_lock);

/* Keeps track of all clocks */
static struct clk_hw_onecell_data *aspeed_g6_clk_data;

static void __iomem *scu_g6_base;
/* AST2600 revision: A0, A1, A2, etc */
static u8 soc_rev;

struct mac_delay_config {
	u32 tx_delay_1000;
	u32 rx_delay_1000;
	u32 tx_delay_100;
	u32 rx_delay_100;
	u32 tx_delay_10;
	u32 rx_delay_10;
};

union mac_delay_1g {
	u32 w;
	struct {
		unsigned int tx_delay_1		: 6;	/* bit[5:0] */
		unsigned int tx_delay_2		: 6;	/* bit[11:6] */
		unsigned int rx_delay_1		: 6;	/* bit[17:12] */
		unsigned int rx_delay_2		: 6;	/* bit[23:18] */
		unsigned int rx_clk_inv_1	: 1;	/* bit[24] */
		unsigned int rx_clk_inv_2	: 1;	/* bit[25] */
		unsigned int rmii_tx_data_at_falling_1 : 1; /* bit[26] */
		unsigned int rmii_tx_data_at_falling_2 : 1; /* bit[27] */
		unsigned int rgmiick_pad_dir	: 1;	/* bit[28] */
		unsigned int rmii_50m_oe_1	: 1;	/* bit[29] */
		unsigned int rmii_50m_oe_2	: 1;	/* bit[30] */
		unsigned int rgmii_125m_o_sel	: 1;	/* bit[31] */
	} b;
};

union mac_delay_100_10 {
	u32 w;
	struct {
		unsigned int tx_delay_1		: 6;	/* bit[5:0] */
		unsigned int tx_delay_2		: 6;	/* bit[11:6] */
		unsigned int rx_delay_1		: 6;	/* bit[17:12] */
		unsigned int rx_delay_2		: 6;	/* bit[23:18] */
		unsigned int rx_clk_inv_1	: 1;	/* bit[24] */
		unsigned int rx_clk_inv_2	: 1;	/* bit[25] */
		unsigned int reserved_0		: 6;	/* bit[31:26] */
	} b;
};
/*
 * Clocks marked with CLK_IS_CRITICAL:
 *
 *  ref0 and ref1 are essential for the SoC to operate
 *  mpll is required if SDRAM is used
 */
static struct aspeed_gate_data aspeed_g6_gates[] = {
	/*				    clk rst  name		parent	 flags */
	[ASPEED_CLK_GATE_MCLK]		= {  0, -1, "mclk-gate",	"mpll",	 CLK_IS_CRITICAL }, /* SDRAM */
	[ASPEED_CLK_GATE_ECLK]		= {  1,  6, "eclk-gate",	"eclk",	 0 },	/* Video Engine */
	[ASPEED_CLK_GATE_GCLK]		= {  2,  7, "gclk-gate",	NULL,	 0 },	/* 2D engine */
	/* vclk parent - dclk/d1clk/hclk/mclk */
	[ASPEED_CLK_GATE_VCLK]		= {  3, -1, "vclk-gate",	NULL,	 0 },	/* Video Capture */
	[ASPEED_CLK_GATE_BCLK]		= {  4,  8, "bclk-gate",	"bclk",	 CLK_IS_CRITICAL }, /* PCIe/PCI */
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
	[ASPEED_CLK_GATE_LCLK]		= { 32, 32, "lclk-gate",	NULL,	 CLK_IS_CRITICAL }, /* LPC */
	[ASPEED_CLK_GATE_ESPICLK]	= { 33, -1, "espiclk-gate",	NULL,	 CLK_IS_CRITICAL }, /* eSPI */
	[ASPEED_CLK_GATE_REF1CLK]	= { 34, -1, "ref1clk-gate",	"clkin", CLK_IS_CRITICAL },
	/* Reserved 35 */
	[ASPEED_CLK_GATE_SDCLK]		= { 36, 56, "sdclk-gate",	NULL,	 0 },	/* SDIO/SD */
	[ASPEED_CLK_GATE_LHCCLK]	= { 37, -1, "lhclk-gate",	"lhclk", 0 },	/* LPC master/LPC+ */
	/* Reserved 38 RSA: no longer used */
	/* Reserved 39 */
	[ASPEED_CLK_GATE_I3CDMACLK] 	= { 39,  ASPEED_RESET_I3C,		"i3cclk-gate",	NULL,	0 }, 			/* I3C_DMA */
	[ASPEED_CLK_GATE_I3C0CLK]	= { 40, ASPEED_RESET_I3C0, "i3c0clk-gate", "i3cclk", 0 },
	[ASPEED_CLK_GATE_I3C1CLK]	= { 41, ASPEED_RESET_I3C1, "i3c1clk-gate", "i3cclk", 0 },
	[ASPEED_CLK_GATE_I3C2CLK]	= { 42, ASPEED_RESET_I3C2, "i3c2clk-gate", "i3cclk", 0 },
	[ASPEED_CLK_GATE_I3C3CLK]	= { 43, ASPEED_RESET_I3C3, "i3c3clk-gate", "i3cclk", 0 },
	[ASPEED_CLK_GATE_I3C4CLK]	= { 44, ASPEED_RESET_I3C4, "i3c4clk-gate", "i3cclk", 0 },
	[ASPEED_CLK_GATE_I3C5CLK]	= { 45, ASPEED_RESET_I3C5, "i3c5clk-gate", "i3cclk", 0 },
	[ASPEED_CLK_GATE_RESERVED44]	= { 46, ASPEED_RESET_RESERVED46, "reserved-46", NULL, 0 },
	[ASPEED_CLK_GATE_UART1CLK]	= { 48,  -1, "uart1clk-gate",	"uxclk",	 CLK_IS_CRITICAL },	/* UART1 */
	[ASPEED_CLK_GATE_UART2CLK]	= { 49,  -1, "uart2clk-gate",	"uxclk",	 CLK_IS_CRITICAL },	/* UART2 */
	[ASPEED_CLK_GATE_UART3CLK]	= { 50,  -1, "uart3clk-gate",	"uxclk",  0 },	/* UART3 */
	[ASPEED_CLK_GATE_UART4CLK]	= { 51,  -1, "uart4clk-gate",	"uxclk",	 0 },	/* UART4 */
	[ASPEED_CLK_GATE_MAC3CLK]	= { 52,  52, "mac3clk-gate",	"mac34", 0 },	/* MAC3 */
	[ASPEED_CLK_GATE_MAC4CLK]	= { 53,  53, "mac4clk-gate",	"mac34", 0 },	/* MAC4 */
	[ASPEED_CLK_GATE_UART6CLK]	= { 54,  -1, "uart6clk-gate",	"uxclk", 0 },	/* UART6 */
	[ASPEED_CLK_GATE_UART7CLK]	= { 55,  -1, "uart7clk-gate",	"uxclk", 0 },	/* UART7 */
	[ASPEED_CLK_GATE_UART8CLK]	= { 56,  -1, "uart8clk-gate",	"uxclk", 0 },	/* UART8 */
	[ASPEED_CLK_GATE_UART9CLK]	= { 57,  -1, "uart9clk-gate",	"uxclk", 0 },	/* UART9 */
	[ASPEED_CLK_GATE_UART10CLK]	= { 58,  -1, "uart10clk-gate",	"uxclk", 0 },	/* UART10 */
	[ASPEED_CLK_GATE_UART11CLK]	= { 59,  -1, "uart11clk-gate",	"uxclk", CLK_IS_CRITICAL },	/* UART11 */
	[ASPEED_CLK_GATE_UART12CLK]	= { 60,  -1, "uart12clk-gate",	"uxclk", 0 },	/* UART12 */
	[ASPEED_CLK_GATE_UART13CLK]	= { 61,  -1, "uart13clk-gate",	"uxclk", 0 },	/* UART13 */
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

static const struct clk_div_table ast2600_sd_div_a1_table[] = {
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

static const struct clk_div_table ast2600_sd_div_a2_table[] = {
	{ 0x0, 2 },
	{ 0x1, 4 },
	{ 0x2, 6 },
	{ 0x3, 8 },
	{ 0x4, 10 },
	{ 0x5, 12 },
	{ 0x6, 14 },
	{ 0x7, 1 },
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
static struct clk_hw *ast2600_calc_hpll(const char *name, u32 val)
{
	unsigned int mult, div;
	u32 hwstrap = readl(scu_g6_base + ASPEED_G6_STRAP1);

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = div = 1;
	} else {
		/* F = 25Mhz * [(M + 2) / (n + 1)] / (p + 1) */
		u32 m = val  & 0x1fff;
		u32 n = (val >> 13) & 0x3f;
		u32 p = (val >> 19) & 0xf;

		if (hwstrap & BIT(10))
			m = 0x5F;
		else if (hwstrap & BIT(8))
			m = 0xBF;

		mult = (m + 1) / (n + 1);
		div = (p + 1);
	}
	return clk_hw_register_fixed_factor(NULL, name, "clkin", 0,
			mult, div);
};

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
	u32 val;
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

	regmap_read(map, 0x04, &val);
	if ((val & GENMASK(23, 16)) >> 16) {
		/* After A1 (including A1, A2 and A3), use mpll for fit 200Mhz.
		 * hpll is 12.G and 200MHz cannot be gotten by setting SCU300[14:12].
		 * mpll is 400MHz and 200MHz can be gotten by setting SCU300[14:12]
		 * to 3b'000.
		 */
		regmap_update_bits(map, ASPEED_G6_CLK_SELECTION1, GENMASK(14, 11), BIT(11));

		/* EMMC ext clock divider */
		hw = clk_hw_register_gate(dev, "emmc_extclk_gate", "mpll", 0,
						scu_g6_base + ASPEED_G6_CLK_SELECTION1, 15, 0,
						&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		//ast2600 emmc clk should under 200Mhz
		hw = clk_hw_register_divider_table(dev, "emmc_extclk", "emmc_extclk_gate", 0,
						scu_g6_base + ASPEED_G6_CLK_SELECTION1, 12, 3, 0,
						ast2600_emmc_extclk_div_table,
						&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
		aspeed_g6_clk_data->hws[ASPEED_CLK_EMMC] = hw;
	} else {
		/* EMMC ext clock divider */
		hw = clk_hw_register_gate(dev, "emmc_extclk_gate", "hpll", 0,
						scu_g6_base + ASPEED_G6_CLK_SELECTION1, 15, 0,
						&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);

		//ast2600 emmc clk should under 200Mhz
		hw = clk_hw_register_divider_table(dev, "emmc_extclk",
						"emmc_extclk_gate", 0,
						scu_g6_base +
						ASPEED_G6_CLK_SELECTION1, 12,
						3, 0, ast2600_emmc_extclk_div_table,
						&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
		aspeed_g6_clk_data->hws[ASPEED_CLK_EMMC] = hw;
	}

	clk_hw_register_fixed_rate(NULL, "hclk", NULL, 0, 200000000);

	regmap_read(map, 0x310, &val);
	if (val & BIT(8)) {
		/* SD/SDIO clock divider and gate */
		hw = clk_hw_register_gate(dev, "sd_extclk_gate", "apll", 0,
						scu_g6_base + ASPEED_G6_CLK_SELECTION4, 31, 0,
						&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
	} else {
		/* SD/SDIO clock divider and gate */
		hw = clk_hw_register_gate(dev, "sd_extclk_gate", "hclk", 0,
						scu_g6_base + ASPEED_G6_CLK_SELECTION4, 31, 0,
						&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
	}

	regmap_read(map, 0x14, &val);
	if (((val & GENMASK(23, 16)) >> 16) >= 2) {
		/* A2 and A3 clock divisor is different from A1 and A0 */
		hw = clk_hw_register_divider_table(dev, "sd_extclk", "sd_extclk_gate",
					0, scu_g6_base + ASPEED_G6_CLK_SELECTION4, 28, 3, 0,
					ast2600_sd_div_a2_table,
					&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
	} else {
		hw = clk_hw_register_divider_table(dev, "sd_extclk", "sd_extclk_gate",
					0, scu_g6_base + ASPEED_G6_CLK_SELECTION4, 28, 3, 0,
					ast2600_sd_div_a1_table,
					&aspeed_g6_clk_lock);
		if (IS_ERR(hw))
			return PTR_ERR(hw);
	}

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

	/* gfx d1clk : use usb phy */
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION1, GENMASK(10, 8), BIT(9));
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
	hw = clk_hw_register_divider_table(dev, "bclk", "epll", 0,
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

	//vclk : force disable dynmamic slow down and fix vclk = eclk / 2
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION1, GENMASK(31, 28), 0);
	/* Video Engine clock divider */
	hw = clk_hw_register_divider_table(dev, "eclk", NULL, 0,
			scu_g6_base + ASPEED_G6_CLK_SELECTION1, 28, 3, 0,
			ast2600_eclk_div_table,
			&aspeed_g6_clk_lock);
	if (IS_ERR(hw))
		return PTR_ERR(hw);
	aspeed_g6_clk_data->hws[ASPEED_CLK_ECLK] = hw;

	/* uartx parent assign*/
	for (i = 0; i < 13; i++) {
		if ((i < 6) & (i != 4)) {
			regmap_read(map, 0x310, &val);
			if (val & BIT(i))
				aspeed_g6_gates[ASPEED_CLK_GATE_UART1CLK + i].parent_name = "huxclk";
			else
				aspeed_g6_gates[ASPEED_CLK_GATE_UART1CLK + i].parent_name = "uxclk";
		}
		if (i == 4)
			aspeed_g6_gates[ASPEED_CLK_GATE_UART1CLK + i].parent_name = "uart5";
		if ((i > 5) & (i != 4)) {
			regmap_read(map, 0x314, &val);
			if (val & BIT(i))
				aspeed_g6_gates[ASPEED_CLK_GATE_UART1CLK + i].parent_name = "huxclk";
			else
				aspeed_g6_gates[ASPEED_CLK_GATE_UART1CLK + i].parent_name = "uxclk";
		}
	}

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

static int __init aspeed_g6_clk_init(void)
{
	return platform_driver_register(&aspeed_g6_clk_driver);
}

core_initcall(aspeed_g6_clk_init);

static const u32 ast2600_a0_axi_ahb_div_table[] = {
	2, 2, 3, 4,
};

static u32 ast2600_a1_axi_ahb_div0_tbl[] = {
	3, 2, 3, 4,
};

static u32 ast2600_a1_axi_ahb_div1_tbl[] = {
	3, 4, 6, 8,
};

static const u32 ast2600_a1_axi_ahb200_tbl[] = {
	3, 4, 3, 4, 2, 2, 2, 2,
};

static void __init aspeed_g6_cc(struct regmap *map)
{
	struct clk_hw *hw;
	u32 val, freq, div, divbits, chip_id, axi_div, ahb_div;
	u32 mult;

	clk_hw_register_fixed_rate(NULL, "clkin", NULL, 0, 25000000);

	/*
	 * High-speed PLL clock derived from the crystal. This the CPU clock,
	 * and we assume that it is enabled
	 */
	regmap_read(map, ASPEED_HPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_HPLL] = ast2600_calc_hpll("hpll", val);

	regmap_read(map, ASPEED_MPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_MPLL] = ast2600_calc_pll("mpll", val);

	regmap_read(map, ASPEED_DPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_DPLL] = ast2600_calc_pll("dpll", val);

	regmap_read(map, ASPEED_EPLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_EPLL] = ast2600_calc_pll("epll", val);

	regmap_read(map, ASPEED_APLL_PARAM, &val);
	aspeed_g6_clk_data->hws[ASPEED_CLK_APLL] = ast2600_calc_apll("apll", val);


	regmap_read(map, ASPEED_G6_STRAP1, &val);
	divbits = (val >> 11) & 0x3;
	regmap_read(map, ASPEED_G6_SILICON_REV, &chip_id);
	if ((chip_id & CHIP_REVISION_ID) >> 16) {
		//ast2600a1
		if (val & BIT(16)) {
			ast2600_a1_axi_ahb_div1_tbl[0] = ast2600_a1_axi_ahb200_tbl[(val >> 8) & 0x7] * 2;
			axi_div = 1;
			ahb_div = ast2600_a1_axi_ahb_div1_tbl[divbits];
		} else {
			ast2600_a1_axi_ahb_div0_tbl[0] = ast2600_a1_axi_ahb200_tbl[(val >> 8) & 0x7];
			axi_div = 2;
			ahb_div = ast2600_a1_axi_ahb_div0_tbl[divbits];
		}
	} else {
		//ast2600a0 : fix axi = hpll/2
		axi_div = 2;
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

	/* uart5 clock selection */
	regmap_read(map, ASPEED_G6_MISC_CTRL, &val);
	if (val & UART_DIV13_EN)
		div = 13;
	else
		div = 1;
	regmap_read(map, ASPEED_G6_CLK_SELECTION2, &val);
	if (val & BIT(14))
		freq = 192000000;
	else
		freq = 24000000;
	freq = freq / div;

	aspeed_g6_clk_data->hws[ASPEED_CLK_UART5] = clk_hw_register_fixed_rate(NULL, "uart5", NULL, 0, freq);

	/* UART1~13 clock div13 setting except uart5 */
	regmap_read(map, ASPEED_G6_CLK_SELECTION5, &val);

	switch (val & 0x3) {
	case 0:
		aspeed_g6_clk_data->hws[ASPEED_CLK_UARTX] = clk_hw_register_fixed_factor(NULL, "uartx", "apll", 0, 1, 4);
		break;
	case 1:
		aspeed_g6_clk_data->hws[ASPEED_CLK_UARTX] = clk_hw_register_fixed_factor(NULL, "uartx", "apll", 0, 1, 2);
		break;
	case 2:
		aspeed_g6_clk_data->hws[ASPEED_CLK_UARTX] = clk_hw_register_fixed_factor(NULL, "uartx", "apll", 0, 1, 1);
		break;
	case 3:
		aspeed_g6_clk_data->hws[ASPEED_CLK_UARTX] = clk_hw_register_fixed_factor(NULL, "uartx", "ahb", 0, 1, 1);
		break;
	}

	/* uxclk */
	regmap_read(map, ASPEED_UARTCLK_FROM_UXCLK, &val);
	div = ((val >> 8) & 0x3ff) * 2;
	mult = val & 0xff;

	hw = clk_hw_register_fixed_factor(NULL, "uxclk", "uartx", 0, mult, div);
	aspeed_g6_clk_data->hws[ASPEED_CLK_UXCLK] = hw;

	/* huxclk */
	regmap_read(map, 0x33c, &val);
	div = ((val >> 8) & 0x3ff) * 2;
	mult = val & 0xff;

	hw = clk_hw_register_fixed_factor(NULL, "huxclk", "uartx", 0, mult, div);
	aspeed_g6_clk_data->hws[ASPEED_CLK_HUXCLK] = hw;

	/* i3c clock */
	regmap_read(map, ASPEED_G6_CLK_SELECTION5, &val);
	if (FIELD_GET(I3C_CLK_SELECTION, val) == I3C_CLK_SELECT_APLL_DIV) {
		val = FIELD_GET(APLL_DIV_SELECTION, val);
		if (val)
			div = val + 1;
		else
			div = val + 2;
		hw = clk_hw_register_fixed_factor(NULL, "i3cclk", "apll", 0, 1,
						  div);
	} else {
		hw = clk_hw_register_fixed_factor(NULL, "i3cclk", "ahb", 0, 1,
						  1);
	}
	aspeed_g6_clk_data->hws[ASPEED_CLK_I3C] = hw;
};

static void __init aspeed_g6_cc_init(struct device_node *np)
{
	struct regmap *map;
	struct mac_delay_config mac_cfg;
	union mac_delay_1g reg_1g;
	union mac_delay_100_10 reg_100, reg_10;
	u32 uart_clk_source = 0;
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

	of_property_read_u32(np, "uart-clk-source", &uart_clk_source);

	if (uart_clk_source) {
		if (uart_clk_source & GENMASK(5, 0))
			regmap_update_bits(map, ASPEED_G6_CLK_SELECTION4, GENMASK(5, 0), uart_clk_source & GENMASK(5, 0));

		if (uart_clk_source & GENMASK(12, 6))
			regmap_update_bits(map, ASPEED_G6_CLK_SELECTION5, GENMASK(12, 6), uart_clk_source & GENMASK(12, 6));
	}

	/* fixed settings for RGMII/RMII clock generator */
	/* MAC1/2 RGMII 125MHz = EPLL / 8 */
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION2, GENMASK(23, 20),
			   (0x7 << 20));

	/* MAC3/4 RMII 50MHz = HCLK / 4 */
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION4, GENMASK(18, 16),
			   (0x3 << 16));

	/* BIT[31]: MAC1/2 RGMII 125M source = internal PLL
	 * BIT[28]: RGMIICK pad direction = output
	 */
	regmap_write(map, ASPEED_MAC12_CLK_DLY,
		     BIT(31) | BIT(28) | ASPEED_G6_DEF_MAC12_DELAY_1G);
	regmap_write(map, ASPEED_MAC12_CLK_DLY_100M,
		     ASPEED_G6_DEF_MAC12_DELAY_100M);
	regmap_write(map, ASPEED_MAC12_CLK_DLY_10M,
		     ASPEED_G6_DEF_MAC12_DELAY_10M);

	/* MAC3/4 RGMII 125M source = RGMIICK pad */
	regmap_write(map, ASPEED_MAC34_CLK_DLY,
		     ASPEED_G6_DEF_MAC34_DELAY_1G);
	regmap_write(map, ASPEED_MAC34_CLK_DLY_100M,
		     ASPEED_G6_DEF_MAC34_DELAY_100M);
	regmap_write(map, ASPEED_MAC34_CLK_DLY_10M,
		     ASPEED_G6_DEF_MAC34_DELAY_10M);

	/* MAC3/4 default pad driving strength */
	regmap_write(map, ASPEED_G6_MAC34_DRIVING_CTRL, 0x0000000f);

	regmap_read(map, ASPEED_MAC12_CLK_DLY, &reg_1g.w);
	regmap_read(map, ASPEED_MAC12_CLK_DLY_100M, &reg_100.w);
	regmap_read(map, ASPEED_MAC12_CLK_DLY_10M, &reg_10.w);
	ret = of_property_read_u32_array(np, "mac0-clk-delay", (u32 *)&mac_cfg, 6);
	if (!ret) {
		reg_1g.b.tx_delay_1 = mac_cfg.tx_delay_1000;
		reg_1g.b.rx_delay_1 = mac_cfg.rx_delay_1000;
		reg_100.b.tx_delay_1 = mac_cfg.tx_delay_100;
		reg_100.b.rx_delay_1 = mac_cfg.rx_delay_100;
		reg_10.b.tx_delay_1 = mac_cfg.tx_delay_10;
		reg_10.b.rx_delay_1 = mac_cfg.rx_delay_10;
	}
	ret = of_property_read_u32_array(np, "mac1-clk-delay", (u32 *)&mac_cfg, 6);
	if (!ret) {
		reg_1g.b.tx_delay_2 = mac_cfg.tx_delay_1000;
		reg_1g.b.rx_delay_2 = mac_cfg.rx_delay_1000;
		reg_100.b.tx_delay_2 = mac_cfg.tx_delay_100;
		reg_100.b.rx_delay_2 = mac_cfg.rx_delay_100;
		reg_10.b.tx_delay_2 = mac_cfg.tx_delay_10;
		reg_10.b.rx_delay_2 = mac_cfg.rx_delay_10;
	}
	regmap_write(map, ASPEED_MAC12_CLK_DLY, reg_1g.w);
	regmap_write(map, ASPEED_MAC12_CLK_DLY_100M, reg_100.w);
	regmap_write(map, ASPEED_MAC12_CLK_DLY_10M, reg_10.w);

	regmap_read(map, ASPEED_MAC34_CLK_DLY, &reg_1g.w);
	regmap_read(map, ASPEED_MAC34_CLK_DLY_100M, &reg_100.w);
	regmap_read(map, ASPEED_MAC34_CLK_DLY_10M, &reg_10.w);
	ret = of_property_read_u32_array(np, "mac2-clk-delay", (u32 *)&mac_cfg, 6);
	if (!ret) {
		reg_1g.b.tx_delay_1 = mac_cfg.tx_delay_1000;
		reg_1g.b.rx_delay_1 = mac_cfg.rx_delay_1000;
		reg_100.b.tx_delay_1 = mac_cfg.tx_delay_100;
		reg_100.b.rx_delay_1 = mac_cfg.rx_delay_100;
		reg_10.b.tx_delay_1 = mac_cfg.tx_delay_10;
		reg_10.b.rx_delay_1 = mac_cfg.rx_delay_10;
	}
	ret = of_property_read_u32_array(np, "mac3-clk-delay", (u32 *)&mac_cfg, 6);
	if (!ret) {
		reg_1g.b.tx_delay_2 = mac_cfg.tx_delay_1000;
		reg_1g.b.rx_delay_2 = mac_cfg.rx_delay_1000;
		reg_100.b.tx_delay_2 = mac_cfg.tx_delay_100;
		reg_100.b.rx_delay_2 = mac_cfg.rx_delay_100;
		reg_10.b.tx_delay_2 = mac_cfg.tx_delay_10;
		reg_10.b.rx_delay_2 = mac_cfg.rx_delay_10;
	}
	regmap_write(map, ASPEED_MAC34_CLK_DLY, reg_1g.w);
	regmap_write(map, ASPEED_MAC34_CLK_DLY_100M, reg_100.w);
	regmap_write(map, ASPEED_MAC34_CLK_DLY_10M, reg_10.w);

	/* A0/A1 need change to RSA clock = HPLL/3, A2/A3 have been set at Rom Code */
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION1, BIT(19), BIT(19));
	regmap_update_bits(map, ASPEED_G6_CLK_SELECTION1, GENMASK(27, 26), (2 << 26));

	aspeed_g6_cc(map);
	aspeed_g6_clk_data->num = ASPEED_G6_NUM_CLKS;
	ret = of_clk_add_hw_provider(np, of_clk_hw_onecell_get, aspeed_g6_clk_data);
	if (ret)
		pr_err("failed to add DT provider: %d\n", ret);
};
CLK_OF_DECLARE_DRIVER(aspeed_cc_g6, "aspeed,ast2600-scu", aspeed_g6_cc_init);
