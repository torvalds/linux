// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2024 ASPEED Technology Inc.
 * Author: Ryan Chen <ryan_chen@aspeedtech.com>
 */
#include <linux/auxiliary_bus.h>
#include <linux/bitfield.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/units.h>

#include <dt-bindings/clock/aspeed,ast2700-scu.h>

/* SOC0 */
#define SCU0_HWSTRAP1		0x010
#define SCU0_CLK_STOP		0x240
#define SCU0_CLK_SEL1		0x280
#define SCU0_CLK_SEL2		0x284
#define GET_USB_REFCLK_DIV(x)	((GENMASK(23, 20) & (x)) >> 20)
#define UART_DIV13_EN		BIT(30)
#define SCU0_HPLL_PARAM		0x300
#define SCU0_DPLL_PARAM		0x308
#define SCU0_MPLL_PARAM		0x310
#define SCU0_D0CLK_PARAM	0x320
#define SCU0_D1CLK_PARAM	0x330
#define SCU0_CRT0CLK_PARAM	0x340
#define SCU0_CRT1CLK_PARAM	0x350
#define SCU0_MPHYCLK_PARAM	0x360

/* SOC1 */
#define SCU1_REVISION_ID	0x0
#define REVISION_ID		GENMASK(23, 16)
#define SCU1_CLK_STOP		0x240
#define SCU1_CLK_STOP2		0x260
#define SCU1_CLK_SEL1		0x280
#define SCU1_CLK_SEL2		0x284
#define SCU1_CLK_I3C_DIV_MASK	GENMASK(25, 23)
#define SCU1_CLK_I3C_DIV(n)	((n) - 1)
#define UXCLK_MASK		GENMASK(1, 0)
#define HUXCLK_MASK		GENMASK(4, 3)
#define SCU1_HPLL_PARAM		0x300
#define SCU1_APLL_PARAM		0x310
#define SCU1_DPLL_PARAM		0x320
#define SCU1_UXCLK_CTRL		0x330
#define SCU1_HUXCLK_CTRL	0x334
#define SCU1_MAC12_CLK_DLY	0x390
#define SCU1_MAC12_CLK_DLY_100M	0x394
#define SCU1_MAC12_CLK_DLY_10M	0x398

enum ast2700_clk_type {
	CLK_MUX,
	CLK_PLL,
	CLK_HPLL,
	CLK_GATE,
	CLK_MISC,
	CLK_FIXED,
	CLK_DIVIDER,
	CLK_UART_PLL,
	CLK_GATE_ASPEED,
	CLK_FIXED_FACTOR,
	CLK_FIXED_DISPLAY,
};

struct ast2700_clk_fixed_factor_data {
	unsigned int mult;
	unsigned int div;
	int parent_id;
};

struct ast2700_clk_gate_data {
	int parent_id;
	u32 flags;
	u32 reg;
	u8 bit;
};

struct ast2700_clk_mux_data {
	const struct clk_hw **parent_hws;
	const unsigned int *parent_ids;
	unsigned int num_parents;
	u8 bit_shift;
	u8 bit_width;
	u32 reg;
};

struct ast2700_clk_div_data {
	const struct clk_div_table *div_table;
	unsigned int parent_id;
	u8 bit_shift;
	u8 bit_width;
	u32 reg;
};

struct ast2700_clk_pll_data {
	unsigned int parent_id;
	u32 reg;
};

struct ast2700_clk_fixed_rate_data {
	unsigned long fixed_rate;
};

struct ast2700_clk_display_fixed_data {
	u32 reg;
};

struct ast2700_clk_info {
	const char *name;
	u32 id;
	u32 reg;
	u32 type;
	union {
		struct ast2700_clk_fixed_factor_data factor;
		struct ast2700_clk_fixed_rate_data rate;
		struct ast2700_clk_display_fixed_data display_rate;
		struct ast2700_clk_gate_data gate;
		struct ast2700_clk_div_data div;
		struct ast2700_clk_pll_data pll;
		struct ast2700_clk_mux_data mux;
	} data;
};

struct ast2700_clk_data {
	const struct ast2700_clk_info *clk_info;
	unsigned int nr_clks;
	const int scu;
};

struct ast2700_clk_ctrl {
	const struct ast2700_clk_data *clk_data;
	struct device *dev;
	void __iomem *base;
	spinlock_t lock; /* clk lock */
};

static const struct clk_div_table ast2700_rgmii_div_table[] = {
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

static const struct clk_div_table ast2700_rmii_div_table[] = {
	{ 0x0, 8 },
	{ 0x1, 8 },
	{ 0x2, 12 },
	{ 0x3, 16 },
	{ 0x4, 20 },
	{ 0x5, 24 },
	{ 0x6, 28 },
	{ 0x7, 32 },
	{ 0 }
};

static const struct clk_div_table ast2700_clk_div_table[] = {
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

static const struct clk_div_table ast2700_clk_div_table2[] = {
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

static const struct clk_div_table ast2700_hclk_div_table[] = {
	{ 0x0, 6 },
	{ 0x1, 5 },
	{ 0x2, 4 },
	{ 0x3, 7 },
	{ 0 }
};

static const struct clk_div_table ast2700_clk_uart_div_table[] = {
	{ 0x0, 1 },
	{ 0x1, 13 },
	{ 0 }
};

/* soc 0 */
static const unsigned int psp_parent_ids[] = {
	SCU0_CLK_MPLL,
	SCU0_CLK_HPLL,
	SCU0_CLK_HPLL,
	SCU0_CLK_HPLL,
	SCU0_CLK_MPLL_DIV2,
	SCU0_CLK_HPLL_DIV2,
	SCU0_CLK_HPLL,
	SCU0_CLK_HPLL
};

static const struct clk_hw *psp_parent_hws[ARRAY_SIZE(psp_parent_ids)];

static const unsigned int hclk_parent_ids[] = {
	SCU0_CLK_HPLL,
	SCU0_CLK_MPLL
};

static const struct clk_hw *hclk_parent_hws[ARRAY_SIZE(hclk_parent_ids)];

static const unsigned int emmc_parent_ids[] = {
	SCU0_CLK_MPLL_DIV4,
	SCU0_CLK_HPLL_DIV4
};

static const struct clk_hw *emmc_parent_hws[ARRAY_SIZE(emmc_parent_ids)];

static const unsigned int mphy_parent_ids[] = {
	SCU0_CLK_MPLL,
	SCU0_CLK_HPLL,
	SCU0_CLK_DPLL,
	SCU0_CLK_192M
};

static const struct clk_hw *mphy_parent_hws[ARRAY_SIZE(mphy_parent_ids)];

static const unsigned int u2phy_parent_ids[] = {
	SCU0_CLK_MPLL,
	SCU0_CLK_HPLL
};

static const struct clk_hw *u2phy_parent_hws[ARRAY_SIZE(u2phy_parent_ids)];

static const unsigned int uart_parent_ids[] = {
	SCU0_CLK_24M,
	SCU0_CLK_192M
};

static const struct clk_hw *uart_parent_hws[ARRAY_SIZE(uart_parent_ids)];

/* soc 1 */
static const unsigned int uartx_parent_ids[] = {
	SCU1_CLK_UARTX,
	SCU1_CLK_HUARTX
};

static const struct clk_hw *uartx_parent_hws[ARRAY_SIZE(uartx_parent_ids)];

static const unsigned int uxclk_parent_ids[] = {
	SCU1_CLK_APLL_DIV4,
	SCU1_CLK_APLL_DIV2,
	SCU1_CLK_APLL,
	SCU1_CLK_HPLL
};

static const struct clk_hw *uxclk_parent_hws[ARRAY_SIZE(uxclk_parent_ids)];

static const unsigned int sdclk_parent_ids[] = {
	SCU1_CLK_HPLL,
	SCU1_CLK_APLL
};

static const struct clk_hw *sdclk_parent_hws[ARRAY_SIZE(sdclk_parent_ids)];

#define FIXED_CLK(_id, _name, _rate) \
	{ \
		.id = _id,	\
		.type = CLK_FIXED, \
		.name = _name, \
		.data = { .rate = { .fixed_rate = _rate, } }, \
	}

#define FIXED_DISPLAY_CLK(_id, _name, _reg) \
		{ \
			.id = _id, \
			.type = CLK_FIXED_DISPLAY, \
			.name = _name, \
			.data = { .display_rate = { .reg = _reg } }, \
		}

#define PLL_CLK(_id, _type, _name, _parent_id, _reg) \
	{ \
		.id = _id, \
		.type = _type, \
		.name = _name, \
		.data = { .pll = { \
			.parent_id = _parent_id, \
			.reg		= _reg, \
		} }, \
	}

#define MUX_CLK(_id, _name, _parent_ids, _num_parents, _parent_hws, _reg, _shift, _width) \
		{ \
			.id = _id, \
			.type = CLK_MUX, \
			.name = _name, \
			.data = { \
				.mux = { \
					.parent_ids  = _parent_ids, \
					.parent_hws  = _parent_hws, \
					.num_parents = _num_parents, \
					.reg = (_reg), \
					.bit_shift = _shift, \
					.bit_width = _width, \
				}, \
			}, \
		}

#define DIVIDER_CLK(_id, _name, _parent_id, _reg, _shift, _width, _div_table) \
	{ \
		.id = _id,	\
		.type = CLK_DIVIDER, \
		.name = _name, \
		.data = { \
			.div = { \
				.parent_id = _parent_id, \
				.reg = _reg, \
				.bit_shift = _shift, \
				.bit_width = _width, \
				.div_table = _div_table, \
			}, \
		}, \
	}

#define FIXED_FACTOR_CLK(_id, _name, _parent_id, _mult, _div) \
	{ \
		.id = _id,	\
		.type = CLK_FIXED_FACTOR, \
		.name = _name, \
		.data = { .factor = { .parent_id = _parent_id, .mult = _mult, .div = _div, } }, \
	}

#define GATE_CLK(_id, _type, _name, _parent_id, _reg, _bit, _flags) \
	{ \
		.id = _id,	\
		.type = _type, \
		.name = _name, \
		.data = { \
			.gate = { \
				.parent_id = _parent_id, \
				.reg = _reg, \
				.bit = _bit, \
				.flags = _flags, \
			}, \
		}, \
	}

static const struct ast2700_clk_info ast2700_scu0_clk_info[] __initconst = {
	FIXED_CLK(SCU0_CLKIN, "soc0-clkin", 25 * HZ_PER_MHZ),
	FIXED_CLK(SCU0_CLK_24M, "soc0-clk24Mhz", 24 * HZ_PER_MHZ),
	FIXED_CLK(SCU0_CLK_192M, "soc0-clk192Mhz", 192 * HZ_PER_MHZ),
	FIXED_CLK(SCU0_CLK_U2PHY_CLK12M, "u2phy_clk12m", 12 * HZ_PER_MHZ),
	FIXED_DISPLAY_CLK(SCU0_CLK_D0, "d0clk", SCU0_D0CLK_PARAM),
	FIXED_DISPLAY_CLK(SCU0_CLK_D1, "d1clk", SCU0_D1CLK_PARAM),
	FIXED_DISPLAY_CLK(SCU0_CLK_CRT0, "crt0clk", SCU0_CRT0CLK_PARAM),
	FIXED_DISPLAY_CLK(SCU0_CLK_CRT1, "crt1clk", SCU0_CRT1CLK_PARAM),
	PLL_CLK(SCU0_CLK_HPLL, CLK_HPLL, "soc0-hpll", SCU0_CLKIN, SCU0_HPLL_PARAM),
	PLL_CLK(SCU0_CLK_DPLL, CLK_PLL, "soc0-dpll", SCU0_CLKIN, SCU0_DPLL_PARAM),
	PLL_CLK(SCU0_CLK_MPLL, CLK_PLL, "soc0-mpll", SCU0_CLKIN, SCU0_MPLL_PARAM),
	FIXED_FACTOR_CLK(SCU0_CLK_HPLL_DIV2, "soc0-hpll_div2", SCU0_CLK_HPLL, 1, 2),
	FIXED_FACTOR_CLK(SCU0_CLK_HPLL_DIV4, "soc0-hpll_div4", SCU0_CLK_HPLL, 1, 4),
	FIXED_FACTOR_CLK(SCU0_CLK_MPLL_DIV2, "soc0-mpll_div2", SCU0_CLK_MPLL, 1, 2),
	FIXED_FACTOR_CLK(SCU0_CLK_MPLL_DIV4, "soc0-mpll_div4", SCU0_CLK_MPLL, 1, 4),
	FIXED_FACTOR_CLK(SCU0_CLK_MPLL_DIV8, "soc0-mpll_div8", SCU0_CLK_MPLL, 1, 8),
	FIXED_FACTOR_CLK(SCU0_CLK_AXI1, "axi1clk", SCU0_CLK_MPLL, 1, 4),
	MUX_CLK(SCU0_CLK_PSP, "pspclk", psp_parent_ids, ARRAY_SIZE(psp_parent_ids),
		psp_parent_hws, SCU0_HWSTRAP1, 2, 3),
	FIXED_FACTOR_CLK(SCU0_CLK_AXI0, "axi0clk", SCU0_CLK_PSP, 1, 2),
	MUX_CLK(SCU0_CLK_AHBMUX, "soc0-ahbmux", hclk_parent_ids, ARRAY_SIZE(hclk_parent_ids),
		hclk_parent_hws, SCU0_HWSTRAP1, 7, 1),
	MUX_CLK(SCU0_CLK_EMMCMUX, "emmcsrc-mux", emmc_parent_ids, ARRAY_SIZE(emmc_parent_ids),
		emmc_parent_hws, SCU0_CLK_SEL1, 11, 1),
	MUX_CLK(SCU0_CLK_MPHYSRC, "mphysrc", mphy_parent_ids, ARRAY_SIZE(mphy_parent_ids),
		mphy_parent_hws, SCU0_CLK_SEL2, 18, 2),
	MUX_CLK(SCU0_CLK_U2PHY_REFCLKSRC, "u2phy_refclksrc", u2phy_parent_ids,
		ARRAY_SIZE(u2phy_parent_ids), u2phy_parent_hws, SCU0_CLK_SEL2, 23, 1),
	MUX_CLK(SCU0_CLK_UART, "soc0-uartclk", uart_parent_ids, ARRAY_SIZE(uart_parent_ids),
		uart_parent_hws, SCU0_CLK_SEL2, 14, 1),
	PLL_CLK(SCU0_CLK_MPHY, CLK_MISC, "mphyclk", SCU0_CLK_MPHYSRC, SCU0_MPHYCLK_PARAM),
	PLL_CLK(SCU0_CLK_U2PHY_REFCLK, CLK_MISC, "u2phy_refclk", SCU0_CLK_U2PHY_REFCLKSRC,
		SCU0_CLK_SEL2),
	DIVIDER_CLK(SCU0_CLK_AHB, "soc0-ahb", SCU0_CLK_AHBMUX,
		    SCU0_HWSTRAP1, 5, 2, ast2700_hclk_div_table),
	DIVIDER_CLK(SCU0_CLK_EMMC, "emmcclk", SCU0_CLK_EMMCMUX,
		    SCU0_CLK_SEL1, 12, 3, ast2700_clk_div_table2),
	DIVIDER_CLK(SCU0_CLK_APB, "soc0-apb", SCU0_CLK_AXI0,
		    SCU0_CLK_SEL1, 23, 3, ast2700_clk_div_table2),
	DIVIDER_CLK(SCU0_CLK_HPLL_DIV_AHB, "soc0-hpll-ahb", SCU0_CLK_HPLL,
		    SCU0_HWSTRAP1, 5, 2, ast2700_hclk_div_table),
	DIVIDER_CLK(SCU0_CLK_MPLL_DIV_AHB, "soc0-mpll-ahb", SCU0_CLK_MPLL,
		    SCU0_HWSTRAP1, 5, 2, ast2700_hclk_div_table),
	DIVIDER_CLK(SCU0_CLK_UART4, "uart4clk", SCU0_CLK_UART,
		    SCU0_CLK_SEL2, 30, 1, ast2700_clk_uart_div_table),
	GATE_CLK(SCU0_CLK_GATE_MCLK, CLK_GATE_ASPEED, "mclk-gate", SCU0_CLK_MPLL,
		 SCU0_CLK_STOP, 0, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_ECLK, CLK_GATE_ASPEED, "eclk-gate", -1, SCU0_CLK_STOP, 1, 0),
	GATE_CLK(SCU0_CLK_GATE_2DCLK, CLK_GATE_ASPEED, "gclk-gate", -1, SCU0_CLK_STOP, 2, 0),
	GATE_CLK(SCU0_CLK_GATE_VCLK, CLK_GATE_ASPEED, "vclk-gate", -1, SCU0_CLK_STOP, 3, 0),
	GATE_CLK(SCU0_CLK_GATE_BCLK, CLK_GATE_ASPEED, "bclk-gate", -1,
		 SCU0_CLK_STOP, 4, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_VGA0CLK,  CLK_GATE_ASPEED, "vga0clk-gate", -1,
		 SCU0_CLK_STOP, 5, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_REFCLK,  CLK_GATE_ASPEED, "soc0-refclk-gate", SCU0_CLKIN,
		 SCU0_CLK_STOP, 6, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_PORTBUSB2CLK, CLK_GATE_ASPEED, "portb-usb2clk-gate", -1,
		 SCU0_CLK_STOP, 7, 0),
	GATE_CLK(SCU0_CLK_GATE_UHCICLK, CLK_GATE_ASPEED, "uhciclk-gate", -1, SCU0_CLK_STOP, 9, 0),
	GATE_CLK(SCU0_CLK_GATE_VGA1CLK, CLK_GATE_ASPEED, "vga1clk-gate", -1,
		 SCU0_CLK_STOP, 10, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_DDRPHYCLK, CLK_GATE_ASPEED, "ddrphy-gate", -1,
		 SCU0_CLK_STOP, 11, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_E2M0CLK, CLK_GATE_ASPEED, "e2m0clk-gate", -1,
		 SCU0_CLK_STOP, 12, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_HACCLK, CLK_GATE_ASPEED, "hacclk-gate", -1, SCU0_CLK_STOP, 13, 0),
	GATE_CLK(SCU0_CLK_GATE_PORTAUSB2CLK, CLK_GATE_ASPEED, "porta-usb2clk-gate", -1,
		 SCU0_CLK_STOP, 14, 0),
	GATE_CLK(SCU0_CLK_GATE_UART4CLK, CLK_GATE_ASPEED, "uart4clk-gate", SCU0_CLK_UART4,
		 SCU0_CLK_STOP, 15, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_SLICLK, CLK_GATE_ASPEED, "soc0-sliclk-gate", -1,
		 SCU0_CLK_STOP, 16, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_DACCLK, CLK_GATE_ASPEED, "dacclk-gate", -1,
		 SCU0_CLK_STOP, 17, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_DP, CLK_GATE_ASPEED, "dpclk-gate", -1,
		 SCU0_CLK_STOP, 18, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_E2M1CLK, CLK_GATE_ASPEED, "e2m1clk-gate", -1,
		 SCU0_CLK_STOP, 19, CLK_IS_CRITICAL),
	GATE_CLK(SCU0_CLK_GATE_CRT0CLK, CLK_GATE_ASPEED, "crt0clk-gate", -1,
		 SCU0_CLK_STOP, 20, 0),
	GATE_CLK(SCU0_CLK_GATE_CRT1CLK, CLK_GATE_ASPEED, "crt1clk-gate", -1,
		 SCU0_CLK_STOP, 21, 0),
	GATE_CLK(SCU0_CLK_GATE_ECDSACLK, CLK_GATE_ASPEED, "eccclk-gate", -1,
		 SCU0_CLK_STOP, 23, 0),
	GATE_CLK(SCU0_CLK_GATE_RSACLK, CLK_GATE_ASPEED, "rsaclk-gate", -1,
		 SCU0_CLK_STOP, 24, 0),
	GATE_CLK(SCU0_CLK_GATE_RVAS0CLK, CLK_GATE_ASPEED, "rvas0clk-gate", -1,
		 SCU0_CLK_STOP, 25, 0),
	GATE_CLK(SCU0_CLK_GATE_UFSCLK, CLK_GATE_ASPEED, "ufsclk-gate", -1,
		 SCU0_CLK_STOP, 26, 0),
	GATE_CLK(SCU0_CLK_GATE_EMMCCLK, CLK_GATE_ASPEED, "emmcclk-gate", SCU0_CLK_EMMC,
		 SCU0_CLK_STOP, 27, 0),
	GATE_CLK(SCU0_CLK_GATE_RVAS1CLK, CLK_GATE_ASPEED, "rvas1clk-gate", -1,
		 SCU0_CLK_STOP, 28, 0),
};

static const struct ast2700_clk_info ast2700_scu1_clk_info[] __initconst = {
	FIXED_CLK(SCU1_CLKIN, "soc1-clkin", 25 * HZ_PER_MHZ),
	PLL_CLK(SCU1_CLK_HPLL, CLK_PLL, "soc1-hpll", SCU1_CLKIN, SCU1_HPLL_PARAM),
	PLL_CLK(SCU1_CLK_APLL, CLK_PLL, "soc1-apll", SCU1_CLKIN, SCU1_APLL_PARAM),
	PLL_CLK(SCU1_CLK_DPLL, CLK_PLL, "soc1-dpll", SCU1_CLKIN, SCU1_DPLL_PARAM),
	FIXED_FACTOR_CLK(SCU1_CLK_APLL_DIV2, "soc1-apll_div2", SCU1_CLK_APLL, 1, 2),
	FIXED_FACTOR_CLK(SCU1_CLK_APLL_DIV4, "soc1-apll_div4", SCU1_CLK_APLL, 1, 4),
	FIXED_FACTOR_CLK(SCU1_CLK_CAN, "canclk", SCU1_CLK_APLL, 1, 10),
	DIVIDER_CLK(SCU1_CLK_APB, "soc1-apb", SCU1_CLK_HPLL,
		    SCU1_CLK_SEL1, 18, 3, ast2700_clk_div_table2),
	DIVIDER_CLK(SCU1_CLK_RMII, "rmii", SCU1_CLK_HPLL,
		    SCU1_CLK_SEL1, 21, 3, ast2700_rmii_div_table),
	DIVIDER_CLK(SCU1_CLK_RGMII, "rgmii", SCU1_CLK_HPLL,
		    SCU1_CLK_SEL1, 25, 3, ast2700_rgmii_div_table),
	DIVIDER_CLK(SCU1_CLK_MACHCLK, "machclk", SCU1_CLK_HPLL,
		    SCU1_CLK_SEL1, 29, 3, ast2700_clk_div_table),
	DIVIDER_CLK(SCU1_CLK_APLL_DIVN, "soc1-apll_divn",
		    SCU1_CLK_APLL, SCU1_CLK_SEL2, 8, 3, ast2700_clk_div_table),
	DIVIDER_CLK(SCU1_CLK_AHB, "soc1-ahb", SCU1_CLK_HPLL,
		    SCU1_CLK_SEL2, 20, 3, ast2700_clk_div_table),
	DIVIDER_CLK(SCU1_CLK_I3C, "soc1-i3c", SCU1_CLK_HPLL,
		    SCU1_CLK_SEL2, 23, 3, ast2700_clk_div_table),
	MUX_CLK(SCU1_CLK_SDMUX, "sdclk-mux", sdclk_parent_ids, ARRAY_SIZE(sdclk_parent_ids),
		sdclk_parent_hws, SCU1_CLK_SEL1, 13, 1),
	MUX_CLK(SCU1_CLK_UXCLK, "uxclk", uxclk_parent_ids, ARRAY_SIZE(uxclk_parent_ids),
		uxclk_parent_hws, SCU1_CLK_SEL2, 0, 2),
	MUX_CLK(SCU1_CLK_HUXCLK, "huxclk", uxclk_parent_ids, ARRAY_SIZE(uxclk_parent_ids),
		uxclk_parent_hws, SCU1_CLK_SEL2, 3, 2),
	DIVIDER_CLK(SCU1_CLK_SDCLK, "sdclk", SCU1_CLK_SDMUX,
		    SCU1_CLK_SEL1, 14, 3, ast2700_clk_div_table),
	PLL_CLK(SCU1_CLK_UARTX, CLK_UART_PLL, "uartxclk", SCU1_CLK_UXCLK, SCU1_UXCLK_CTRL),
	PLL_CLK(SCU1_CLK_HUARTX, CLK_UART_PLL, "huartxclk", SCU1_CLK_HUXCLK, SCU1_HUXCLK_CTRL),
	MUX_CLK(SCU1_CLK_UART0, "uart0clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 0, 1),
	MUX_CLK(SCU1_CLK_UART1, "uart1clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 1, 1),
	MUX_CLK(SCU1_CLK_UART2, "uart2clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 2, 1),
	MUX_CLK(SCU1_CLK_UART3, "uart3clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 3, 1),
	MUX_CLK(SCU1_CLK_UART5, "uart5clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 5, 1),
	MUX_CLK(SCU1_CLK_UART6, "uart6clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 6, 1),
	MUX_CLK(SCU1_CLK_UART7, "uart7clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 7, 1),
	MUX_CLK(SCU1_CLK_UART8, "uart8clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 8, 1),
	MUX_CLK(SCU1_CLK_UART9, "uart9clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 9, 1),
	MUX_CLK(SCU1_CLK_UART10, "uart10clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 10, 1),
	MUX_CLK(SCU1_CLK_UART11, "uart11clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 11, 1),
	MUX_CLK(SCU1_CLK_UART12, "uart12clk", uartx_parent_ids, ARRAY_SIZE(uartx_parent_ids),
		uartx_parent_hws, SCU1_CLK_SEL1, 12, 1),
	FIXED_FACTOR_CLK(SCU1_CLK_UART13, "uart13clk", SCU1_CLK_HUARTX, 1, 1),
	FIXED_FACTOR_CLK(SCU1_CLK_UART14, "uart14clk", SCU1_CLK_HUARTX, 1, 1),
	GATE_CLK(SCU1_CLK_MAC0RCLK, CLK_GATE, "mac0rclk-gate", SCU1_CLK_RMII,
		 SCU1_MAC12_CLK_DLY, 29, 0),
	GATE_CLK(SCU1_CLK_MAC1RCLK, CLK_GATE, "mac1rclk-gate", SCU1_CLK_RMII,
		 SCU1_MAC12_CLK_DLY, 30, 0),
	GATE_CLK(SCU1_CLK_GATE_LCLK0, CLK_GATE_ASPEED, "lclk0-gate", -1,
		 SCU1_CLK_STOP, 0, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_LCLK1, CLK_GATE_ASPEED, "lclk1-gate", -1,
		 SCU1_CLK_STOP, 1, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_ESPI0CLK, CLK_GATE_ASPEED, "espi0clk-gate", -1,
		 SCU1_CLK_STOP, 2, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_ESPI1CLK, CLK_GATE_ASPEED, "espi1clk-gate", -1,
		 SCU1_CLK_STOP, 3, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_SDCLK, CLK_GATE_ASPEED, "sdclk-gate", SCU1_CLK_SDCLK,
		 SCU1_CLK_STOP, 4, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_IPEREFCLK, CLK_GATE_ASPEED, "soc1-iperefclk-gate", -1,
		 SCU1_CLK_STOP, 5, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_REFCLK, CLK_GATE_ASPEED, "soc1-refclk-gate", -1,
		 SCU1_CLK_STOP, 6, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_LPCHCLK, CLK_GATE_ASPEED, "lpchclk-gate", -1,
		 SCU1_CLK_STOP, 7, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_MAC0CLK, CLK_GATE_ASPEED, "mac0clk-gate", -1,
		 SCU1_CLK_STOP, 8, 0),
	GATE_CLK(SCU1_CLK_GATE_MAC1CLK, CLK_GATE_ASPEED, "mac1clk-gate", -1,
		 SCU1_CLK_STOP, 9, 0),
	GATE_CLK(SCU1_CLK_GATE_MAC2CLK, CLK_GATE_ASPEED, "mac2clk-gate", -1,
		 SCU1_CLK_STOP, 10, 0),
	GATE_CLK(SCU1_CLK_GATE_UART0CLK, CLK_GATE_ASPEED, "uart0clk-gate", SCU1_CLK_UART0,
		 SCU1_CLK_STOP, 11, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART1CLK, CLK_GATE_ASPEED, "uart1clk-gate", SCU1_CLK_UART1,
		 SCU1_CLK_STOP, 12, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART2CLK, CLK_GATE_ASPEED, "uart2clk-gate", SCU1_CLK_UART2,
		 SCU1_CLK_STOP, 13, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART3CLK, CLK_GATE_ASPEED, "uart3clk-gate", SCU1_CLK_UART3,
		 SCU1_CLK_STOP, 14, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_I2CCLK, CLK_GATE_ASPEED, "i2cclk-gate", -1, SCU1_CLK_STOP, 15, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C0CLK, CLK_GATE_ASPEED, "i3c0clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 16, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C1CLK, CLK_GATE_ASPEED, "i3c1clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 17, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C2CLK, CLK_GATE_ASPEED, "i3c2clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 18, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C3CLK, CLK_GATE_ASPEED, "i3c3clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 19, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C4CLK, CLK_GATE_ASPEED, "i3c4clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 20, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C5CLK, CLK_GATE_ASPEED, "i3c5clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 21, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C6CLK, CLK_GATE_ASPEED, "i3c6clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 22, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C7CLK, CLK_GATE_ASPEED, "i3c7clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 23, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C8CLK, CLK_GATE_ASPEED, "i3c8clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 24, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C9CLK, CLK_GATE_ASPEED, "i3c9clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 25, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C10CLK, CLK_GATE_ASPEED, "i3c10clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 26, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C11CLK, CLK_GATE_ASPEED, "i3c11clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 27, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C12CLK, CLK_GATE_ASPEED, "i3c12clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 28, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C13CLK, CLK_GATE_ASPEED, "i3c13clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 29, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C14CLK, CLK_GATE_ASPEED, "i3c14clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 30, 0),
	GATE_CLK(SCU1_CLK_GATE_I3C15CLK, CLK_GATE_ASPEED, "i3c15clk-gate", SCU1_CLK_I3C,
		 SCU1_CLK_STOP, 31, 0),
	GATE_CLK(SCU1_CLK_GATE_UART5CLK, CLK_GATE_ASPEED, "uart5clk-gate", SCU1_CLK_UART5,
		 SCU1_CLK_STOP2, 0, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART6CLK, CLK_GATE_ASPEED, "uart6clk-gate", SCU1_CLK_UART6,
		 SCU1_CLK_STOP2, 1, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART7CLK, CLK_GATE_ASPEED, "uart7clk-gate", SCU1_CLK_UART7,
		 SCU1_CLK_STOP2, 2, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART8CLK, CLK_GATE_ASPEED, "uart8clk-gate", SCU1_CLK_UART8,
		 SCU1_CLK_STOP2, 3, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UART9CLK, CLK_GATE_ASPEED, "uart9clk-gate", SCU1_CLK_UART9,
		 SCU1_CLK_STOP2, 4, 0),
	GATE_CLK(SCU1_CLK_GATE_UART10CLK, CLK_GATE_ASPEED, "uart10clk-gate", SCU1_CLK_UART10,
		 SCU1_CLK_STOP2, 5, 0),
	GATE_CLK(SCU1_CLK_GATE_UART11CLK, CLK_GATE_ASPEED, "uart11clk-gate", SCU1_CLK_UART11,
		 SCU1_CLK_STOP2, 6, 0),
	GATE_CLK(SCU1_CLK_GATE_UART12CLK, CLK_GATE_ASPEED, "uart12clk-gate", SCU1_CLK_UART12,
		 SCU1_CLK_STOP2, 7, 0),
	GATE_CLK(SCU1_CLK_GATE_FSICLK, CLK_GATE_ASPEED, "fsiclk-gate", -1, SCU1_CLK_STOP2, 8, 0),
	GATE_CLK(SCU1_CLK_GATE_LTPIPHYCLK, CLK_GATE_ASPEED, "ltpiphyclk-gate", -1,
		 SCU1_CLK_STOP2, 9, 0),
	GATE_CLK(SCU1_CLK_GATE_LTPICLK, CLK_GATE_ASPEED, "ltpiclk-gate", -1,
		 SCU1_CLK_STOP2, 10, 0),
	GATE_CLK(SCU1_CLK_GATE_VGALCLK, CLK_GATE_ASPEED, "vgalclk-gate", -1,
		 SCU1_CLK_STOP2, 11, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_UHCICLK, CLK_GATE_ASPEED, "usbuartclk-gate", -1,
		 SCU1_CLK_STOP2, 12, 0),
	GATE_CLK(SCU1_CLK_GATE_CANCLK, CLK_GATE_ASPEED, "canclk-gate", SCU1_CLK_CAN,
		 SCU1_CLK_STOP2, 13, 0),
	GATE_CLK(SCU1_CLK_GATE_PCICLK, CLK_GATE_ASPEED, "pciclk-gate", -1,
		 SCU1_CLK_STOP2, 14, 0),
	GATE_CLK(SCU1_CLK_GATE_SLICLK, CLK_GATE_ASPEED, "soc1-sliclk-gate", -1,
		 SCU1_CLK_STOP2, 15, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_E2MCLK, CLK_GATE_ASPEED, "soc1-e2m-gate", -1,
		 SCU1_CLK_STOP2, 16, CLK_IS_CRITICAL),
	GATE_CLK(SCU1_CLK_GATE_PORTCUSB2CLK, CLK_GATE_ASPEED, "portcusb2-gate", -1,
		 SCU1_CLK_STOP2, 17, 0),
	GATE_CLK(SCU1_CLK_GATE_PORTDUSB2CLK, CLK_GATE_ASPEED, "portdusb2-gate", -1,
		 SCU1_CLK_STOP2, 18, 0),
	GATE_CLK(SCU1_CLK_GATE_LTPI1TXCLK, CLK_GATE_ASPEED, "ltp1tx-gate", -1,
		 SCU1_CLK_STOP2, 19, 0),
};

static struct clk_hw *ast2700_clk_hw_register_fixed_display(void __iomem *reg, const char *name,
							    struct ast2700_clk_ctrl *clk_ctrl)
{
	unsigned int mult, div, r, n;
	u32 xdclk;
	u32 val;

	val = readl(clk_ctrl->base + SCU0_CLK_SEL2);
	if (val & BIT(29))
		xdclk = 800 * HZ_PER_MHZ;
	else
		xdclk = 1000 * HZ_PER_MHZ;

	val = readl(reg);
	r = val & GENMASK(15, 0);
	n = (val >> 16) & GENMASK(15, 0);
	mult = r;
	div = 2 * n;

	return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL, 0, (xdclk * mult) / div);
}

static struct clk_hw *ast2700_clk_hw_register_hpll(void __iomem *reg,
						   const char *name, const struct clk_hw *parent_hw,
						   struct ast2700_clk_ctrl *clk_ctrl)
{
	unsigned int mult, div;
	u32 val;

	val = readl(clk_ctrl->base + SCU0_HWSTRAP1);
	if ((readl(clk_ctrl->base) & REVISION_ID) && (val & BIT(3))) {
		switch ((val & GENMASK(4, 2)) >> 2) {
		case 2:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 1800 * HZ_PER_MHZ);
		case 3:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 1700 * HZ_PER_MHZ);
		case 6:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 1200 * HZ_PER_MHZ);
		case 7:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 800 * HZ_PER_MHZ);
		default:
			return ERR_PTR(-EINVAL);
		}
	} else if ((val & GENMASK(3, 2)) != 0) {
		switch ((val & GENMASK(3, 2)) >> 2) {
		case 1:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 1900 * HZ_PER_MHZ);
		case 2:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 1800 * HZ_PER_MHZ);
		case 3:
			return devm_clk_hw_register_fixed_rate(clk_ctrl->dev, name, NULL,
							       0, 1700 * HZ_PER_MHZ);
		default:
			return ERR_PTR(-EINVAL);
		}
	} else {
		val = readl(reg);

		if (val & BIT(24)) {
			/* Pass through mode */
			mult = 1;
			div = 1;
		} else {
			u32 m = val & 0x1fff;
			u32 n = (val >> 13) & 0x3f;
			u32 p = (val >> 19) & 0xf;

			mult = (m + 1) / (2 * (n + 1));
			div = p + 1;
		}
	}

	return devm_clk_hw_register_fixed_factor_parent_hw(clk_ctrl->dev, name,
							   parent_hw, 0, mult, div);
}

static struct clk_hw *ast2700_clk_hw_register_pll(int clk_idx, void __iomem *reg,
						  const char *name, const struct clk_hw *parent_hw,
						  struct ast2700_clk_ctrl *clk_ctrl)
{
	int scu = clk_ctrl->clk_data->scu;
	unsigned int mult, div;
	u32 val = readl(reg);

	if (val & BIT(24)) {
		/* Pass through mode */
		mult = 1;
		div = 1;
	} else {
		u32 m = val & 0x1fff;
		u32 n = (val >> 13) & 0x3f;
		u32 p = (val >> 19) & 0xf;

		if (scu) {
			mult = (m + 1) / (n + 1);
			div = p + 1;
		} else {
			if (clk_idx == SCU0_CLK_MPLL) {
				mult = m / (n + 1);
				div = p + 1;
			} else {
				mult = (m + 1) / (2 * (n + 1));
				div = p + 1;
			}
		}
	}

	return devm_clk_hw_register_fixed_factor_parent_hw(clk_ctrl->dev, name,
							   parent_hw, 0, mult, div);
}

static struct clk_hw *ast2700_clk_hw_register_uartpll(void __iomem *reg, const char *name,
						      const struct clk_hw *parent_hw,
						      struct ast2700_clk_ctrl *clk_ctrl)
{
	unsigned int mult, div;
	u32 val = readl(reg);
	u32 r = val & 0xff;
	u32 n = (val >> 8) & 0x3ff;

	mult = r;
	div = n * 2;

	return devm_clk_hw_register_fixed_factor_parent_hw(clk_ctrl->dev, name,
							   parent_hw, 0, mult, div);
}

static struct clk_hw *ast2700_clk_hw_register_misc(int clk_idx, void __iomem *reg,
						   const char *name, const struct clk_hw *parent_hw,
						   struct ast2700_clk_ctrl *clk_ctrl)
{
	u32 div = 0;

	if (clk_idx == SCU0_CLK_MPHY) {
		div = readl(reg) + 1;
	} else if (clk_idx == SCU0_CLK_U2PHY_REFCLK) {
		if (readl(clk_ctrl->base) & REVISION_ID)
			div = (GET_USB_REFCLK_DIV(readl(reg)) + 1) << 4;
		else
			div = (GET_USB_REFCLK_DIV(readl(reg)) + 1) << 1;
	} else {
		return ERR_PTR(-EINVAL);
	}

	return devm_clk_hw_register_fixed_factor_parent_hw(clk_ctrl->dev, name,
							   parent_hw, 0, 1, div);
}

static int ast2700_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);
	u32 reg;

	reg = readl(gate->reg);

	return !(reg & clk);
}

static int ast2700_clk_enable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);

	if (readl(gate->reg) & clk)
		writel(clk, gate->reg + 0x04);

	return 0;
}

static void ast2700_clk_disable(struct clk_hw *hw)
{
	struct clk_gate *gate = to_clk_gate(hw);
	u32 clk = BIT(gate->bit_idx);

	/* Clock is set to enable, so use write to set register */
	writel(clk, gate->reg);
}

static const struct clk_ops ast2700_clk_gate_ops = {
	.enable = ast2700_clk_enable,
	.disable = ast2700_clk_disable,
	.is_enabled = ast2700_clk_is_enabled,
};

static struct clk_hw *ast2700_clk_hw_register_gate(struct device *dev, const char *name,
						   const struct clk_hw *parent_hw,
						   void __iomem *reg, u8 clock_idx,
						   unsigned long flags, spinlock_t *lock)
{
	struct clk_init_data init;
	struct clk_gate *gate;
	struct clk_hw *hw;
	int ret = -EINVAL;

	gate = kzalloc_obj(*gate);
	if (!gate)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.ops = &ast2700_clk_gate_ops;
	init.flags = flags;
	init.parent_names = NULL;
	init.parent_hws = parent_hw ? &parent_hw : NULL;
	init.parent_data = NULL;
	init.num_parents = parent_hw ? 1 : 0;

	gate->reg = reg;
	gate->bit_idx = clock_idx;
	gate->flags = 0;
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

static void ast2700_soc1_configure_i3c_clk(struct ast2700_clk_ctrl *clk_ctrl)
{
	if (readl(clk_ctrl->base) & REVISION_ID) {
		u32 val;

		/* I3C 250MHz = HPLL/4 */
		val = readl(clk_ctrl->base + SCU1_CLK_SEL2) & ~SCU1_CLK_I3C_DIV_MASK;
		val |= FIELD_PREP(SCU1_CLK_I3C_DIV_MASK, SCU1_CLK_I3C_DIV(4));
		writel(val, clk_ctrl->base + SCU1_CLK_SEL2);
	}
}

static inline const struct clk_hw *get_parent_hw_or_null(struct clk_hw **hws, int idx)
{
	if (idx < 0)
		return NULL;
	else
		return hws[idx];
}

static int ast2700_soc_clk_probe(struct platform_device *pdev)
{
	const struct ast2700_clk_data *clk_data;
	struct clk_hw_onecell_data *clk_hw_data;
	struct ast2700_clk_ctrl *clk_ctrl;
	struct device *dev = &pdev->dev;
	struct auxiliary_device *adev;
	void __iomem *clk_base;
	struct clk_hw **hws;
	char *reset_name;
	int ret;
	int i;

	clk_ctrl = devm_kzalloc(dev, sizeof(*clk_ctrl), GFP_KERNEL);
	if (!clk_ctrl)
		return -ENOMEM;
	clk_ctrl->dev = dev;
	dev_set_drvdata(&pdev->dev, clk_ctrl);

	spin_lock_init(&clk_ctrl->lock);

	clk_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(clk_base))
		return PTR_ERR(clk_base);

	clk_ctrl->base = clk_base;

	clk_data = device_get_match_data(dev);
	if (!clk_data)
		return -ENODEV;

	clk_ctrl->clk_data = clk_data;
	reset_name = devm_kasprintf(dev, GFP_KERNEL, "reset%d", clk_data->scu);

	clk_hw_data = devm_kzalloc(dev, struct_size(clk_hw_data, hws, clk_data->nr_clks),
				   GFP_KERNEL);
	if (!clk_hw_data)
		return -ENOMEM;

	clk_hw_data->num = clk_data->nr_clks;
	hws = clk_hw_data->hws;

	if (clk_data->scu)
		ast2700_soc1_configure_i3c_clk(clk_ctrl);

	for (i = 0; i < clk_data->nr_clks; i++) {
		const struct ast2700_clk_info *clk = &clk_data->clk_info[i];
		const struct clk_hw *phw = NULL;
		unsigned int id = clk->id;
		void __iomem *reg = NULL;

		if (id >= clk_hw_data->num || hws[id]) {
			dev_err(dev, "clk id %u invalid for %s\n", id, clk->name);
			return -EINVAL;
		}

		if (clk->type == CLK_FIXED) {
			const struct ast2700_clk_fixed_rate_data *fixed_rate = &clk->data.rate;

			hws[id] = devm_clk_hw_register_fixed_rate(dev, clk->name, NULL, 0,
								  fixed_rate->fixed_rate);
		} else if (clk->type == CLK_FIXED_FACTOR) {
			const struct ast2700_clk_fixed_factor_data *factor = &clk->data.factor;

			phw = hws[factor->parent_id];
			hws[id] = devm_clk_hw_register_fixed_factor_parent_hw(dev, clk->name,
									      phw, 0, factor->mult,
									      factor->div);
		} else if (clk->type == CLK_FIXED_DISPLAY) {
			reg = clk_ctrl->base + clk->data.display_rate.reg;

			hws[id] = ast2700_clk_hw_register_fixed_display(reg, clk->name, clk_ctrl);
		} else if (clk->type == CLK_HPLL) {
			const struct ast2700_clk_pll_data *pll = &clk->data.pll;

			reg = clk_ctrl->base + pll->reg;
			phw = hws[pll->parent_id];
			hws[id] = ast2700_clk_hw_register_hpll(reg, clk->name, phw, clk_ctrl);
		} else if (clk->type == CLK_PLL) {
			const struct ast2700_clk_pll_data *pll = &clk->data.pll;

			reg = clk_ctrl->base + pll->reg;
			phw = hws[pll->parent_id];
			hws[id] = ast2700_clk_hw_register_pll(id, reg, clk->name, phw, clk_ctrl);
		} else if (clk->type == CLK_UART_PLL) {
			const struct ast2700_clk_pll_data *pll = &clk->data.pll;

			reg = clk_ctrl->base + pll->reg;
			phw = hws[pll->parent_id];
			hws[id] = ast2700_clk_hw_register_uartpll(reg, clk->name, phw, clk_ctrl);
		} else if (clk->type == CLK_MUX) {
			const struct ast2700_clk_mux_data *mux = &clk->data.mux;

			reg = clk_ctrl->base + mux->reg;
			for (int j = 0; j < mux->num_parents; j++) {
				unsigned int pid = mux->parent_ids[j];

				mux->parent_hws[j] = hws[pid];
			}

			hws[id] = devm_clk_hw_register_mux_parent_hws(dev, clk->name,
								      mux->parent_hws,
								      mux->num_parents, 0,
								      reg, mux->bit_shift,
								      mux->bit_width, 0,
								      &clk_ctrl->lock);
		} else if (clk->type == CLK_MISC) {
			const struct ast2700_clk_pll_data *pll = &clk->data.pll;

			reg = clk_ctrl->base + pll->reg;
			phw = hws[pll->parent_id];
			hws[id] = ast2700_clk_hw_register_misc(id, reg, clk->name, phw, clk_ctrl);
		} else if (clk->type == CLK_DIVIDER) {
			const struct ast2700_clk_div_data *divider = &clk->data.div;

			reg = clk_ctrl->base + divider->reg;
			phw = hws[divider->parent_id];
			hws[id] = clk_hw_register_divider_table_parent_hw(dev, clk->name,
									  phw,
									  0, reg,
									  divider->bit_shift,
									  divider->bit_width, 0,
									  divider->div_table,
									  &clk_ctrl->lock);
		} else if (clk->type == CLK_GATE_ASPEED) {
			const struct ast2700_clk_gate_data *gate = &clk->data.gate;

			phw = get_parent_hw_or_null(hws, gate->parent_id);
			reg = clk_ctrl->base + gate->reg;
			hws[id] = ast2700_clk_hw_register_gate(dev, clk->name, phw, reg, gate->bit,
							       gate->flags, &clk_ctrl->lock);
		} else {
			const struct ast2700_clk_gate_data *gate = &clk->data.gate;

			phw = get_parent_hw_or_null(hws, gate->parent_id);
			reg = clk_ctrl->base + gate->reg;
			hws[id] = devm_clk_hw_register_gate_parent_hw(dev, clk->name, phw,
								      gate->flags, reg, gate->bit,
								      0, &clk_ctrl->lock);
		}

		if (IS_ERR(hws[id]))
			return PTR_ERR(hws[id]);
	}

	ret = devm_of_clk_add_hw_provider(dev, of_clk_hw_onecell_get, clk_hw_data);
	if (ret)
		return ret;

	adev = devm_auxiliary_device_create(dev, reset_name, (__force void *)clk_base);
	if (!adev)
		return -ENODEV;

	return 0;
}

static const struct ast2700_clk_data ast2700_clk0_data = {
	.scu = 0,
	.nr_clks = ARRAY_SIZE(ast2700_scu0_clk_info),
	.clk_info = ast2700_scu0_clk_info,
};

static const struct ast2700_clk_data ast2700_clk1_data = {
	.scu = 1,
	.nr_clks = ARRAY_SIZE(ast2700_scu1_clk_info),
	.clk_info = ast2700_scu1_clk_info,
};

static const struct of_device_id ast2700_scu_match[] = {
	{ .compatible = "aspeed,ast2700-scu0", .data = &ast2700_clk0_data },
	{ .compatible = "aspeed,ast2700-scu1", .data = &ast2700_clk1_data },
	{ /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, ast2700_scu_match);

static struct platform_driver ast2700_scu_driver = {
	.probe = ast2700_soc_clk_probe,
	.driver = {
		.name = "clk-ast2700",
		.of_match_table = ast2700_scu_match,
	},
};

module_platform_driver(ast2700_scu_driver);
