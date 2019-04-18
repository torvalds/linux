/*
 * Clk driver for NXP LPC18xx/LPC43xx Clock Generation Unit (CGU)
 *
 * Copyright (C) 2015 Joachim Eastwood <manabian@gmail.com>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2. This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include <dt-bindings/clock/lpc18xx-cgu.h>

/* Clock Generation Unit (CGU) registers */
#define LPC18XX_CGU_XTAL_OSC_CTRL	0x018
#define LPC18XX_CGU_PLL0USB_STAT	0x01c
#define LPC18XX_CGU_PLL0USB_CTRL	0x020
#define LPC18XX_CGU_PLL0USB_MDIV	0x024
#define LPC18XX_CGU_PLL0USB_NP_DIV	0x028
#define LPC18XX_CGU_PLL0AUDIO_STAT	0x02c
#define LPC18XX_CGU_PLL0AUDIO_CTRL	0x030
#define LPC18XX_CGU_PLL0AUDIO_MDIV	0x034
#define LPC18XX_CGU_PLL0AUDIO_NP_DIV	0x038
#define LPC18XX_CGU_PLL0AUDIO_FRAC	0x03c
#define LPC18XX_CGU_PLL1_STAT		0x040
#define LPC18XX_CGU_PLL1_CTRL		0x044
#define  LPC18XX_PLL1_CTRL_FBSEL	BIT(6)
#define  LPC18XX_PLL1_CTRL_DIRECT	BIT(7)
#define LPC18XX_CGU_IDIV_CTRL(n)	(0x048 + (n) * sizeof(u32))
#define LPC18XX_CGU_BASE_CLK(id)	(0x05c + (id) * sizeof(u32))
#define LPC18XX_CGU_PLL_CTRL_OFFSET	0x4

/* PLL0 bits common to both audio and USB PLL */
#define LPC18XX_PLL0_STAT_LOCK		BIT(0)
#define LPC18XX_PLL0_CTRL_PD		BIT(0)
#define LPC18XX_PLL0_CTRL_BYPASS	BIT(1)
#define LPC18XX_PLL0_CTRL_DIRECTI	BIT(2)
#define LPC18XX_PLL0_CTRL_DIRECTO	BIT(3)
#define LPC18XX_PLL0_CTRL_CLKEN		BIT(4)
#define LPC18XX_PLL0_MDIV_MDEC_MASK	0x1ffff
#define LPC18XX_PLL0_MDIV_SELP_SHIFT	17
#define LPC18XX_PLL0_MDIV_SELI_SHIFT	22
#define LPC18XX_PLL0_MSEL_MAX		BIT(15)

/* Register value that gives PLL0 post/pre dividers equal to 1 */
#define LPC18XX_PLL0_NP_DIVS_1		0x00302062

enum {
	CLK_SRC_OSC32,
	CLK_SRC_IRC,
	CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK,
	CLK_SRC_GP_CLKIN,
	CLK_SRC_RESERVED1,
	CLK_SRC_OSC,
	CLK_SRC_PLL0USB,
	CLK_SRC_PLL0AUDIO,
	CLK_SRC_PLL1,
	CLK_SRC_RESERVED2,
	CLK_SRC_RESERVED3,
	CLK_SRC_IDIVA,
	CLK_SRC_IDIVB,
	CLK_SRC_IDIVC,
	CLK_SRC_IDIVD,
	CLK_SRC_IDIVE,
	CLK_SRC_MAX
};

static const char *clk_src_names[CLK_SRC_MAX] = {
	[CLK_SRC_OSC32]		= "osc32",
	[CLK_SRC_IRC]		= "irc",
	[CLK_SRC_ENET_RX_CLK]	= "enet_rx_clk",
	[CLK_SRC_ENET_TX_CLK]	= "enet_tx_clk",
	[CLK_SRC_GP_CLKIN]	= "gp_clkin",
	[CLK_SRC_OSC]		= "osc",
	[CLK_SRC_PLL0USB]	= "pll0usb",
	[CLK_SRC_PLL0AUDIO]	= "pll0audio",
	[CLK_SRC_PLL1]		= "pll1",
	[CLK_SRC_IDIVA]		= "idiva",
	[CLK_SRC_IDIVB]		= "idivb",
	[CLK_SRC_IDIVC]		= "idivc",
	[CLK_SRC_IDIVD]		= "idivd",
	[CLK_SRC_IDIVE]		= "idive",
};

static const char *clk_base_names[BASE_CLK_MAX] = {
	[BASE_SAFE_CLK]		= "base_safe_clk",
	[BASE_USB0_CLK]		= "base_usb0_clk",
	[BASE_PERIPH_CLK]	= "base_periph_clk",
	[BASE_USB1_CLK]		= "base_usb1_clk",
	[BASE_CPU_CLK]		= "base_cpu_clk",
	[BASE_SPIFI_CLK]	= "base_spifi_clk",
	[BASE_SPI_CLK]		= "base_spi_clk",
	[BASE_PHY_RX_CLK]	= "base_phy_rx_clk",
	[BASE_PHY_TX_CLK]	= "base_phy_tx_clk",
	[BASE_APB1_CLK]		= "base_apb1_clk",
	[BASE_APB3_CLK]		= "base_apb3_clk",
	[BASE_LCD_CLK]		= "base_lcd_clk",
	[BASE_ADCHS_CLK]	= "base_adchs_clk",
	[BASE_SDIO_CLK]		= "base_sdio_clk",
	[BASE_SSP0_CLK]		= "base_ssp0_clk",
	[BASE_SSP1_CLK]		= "base_ssp1_clk",
	[BASE_UART0_CLK]	= "base_uart0_clk",
	[BASE_UART1_CLK]	= "base_uart1_clk",
	[BASE_UART2_CLK]	= "base_uart2_clk",
	[BASE_UART3_CLK]	= "base_uart3_clk",
	[BASE_OUT_CLK]		= "base_out_clk",
	[BASE_AUDIO_CLK]	= "base_audio_clk",
	[BASE_CGU_OUT0_CLK]	= "base_cgu_out0_clk",
	[BASE_CGU_OUT1_CLK]	= "base_cgu_out1_clk",
};

static u32 lpc18xx_cgu_pll0_src_ids[] = {
	CLK_SRC_OSC32, CLK_SRC_IRC, CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK, CLK_SRC_GP_CLKIN, CLK_SRC_OSC,
	CLK_SRC_PLL1, CLK_SRC_IDIVA, CLK_SRC_IDIVB, CLK_SRC_IDIVC,
	CLK_SRC_IDIVD, CLK_SRC_IDIVE,
};

static u32 lpc18xx_cgu_pll1_src_ids[] = {
	CLK_SRC_OSC32, CLK_SRC_IRC, CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK, CLK_SRC_GP_CLKIN, CLK_SRC_OSC,
	CLK_SRC_PLL0USB, CLK_SRC_PLL0AUDIO, CLK_SRC_IDIVA,
	CLK_SRC_IDIVB, CLK_SRC_IDIVC, CLK_SRC_IDIVD, CLK_SRC_IDIVE,
};

static u32 lpc18xx_cgu_idiva_src_ids[] = {
	CLK_SRC_OSC32, CLK_SRC_IRC, CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK, CLK_SRC_GP_CLKIN, CLK_SRC_OSC,
	CLK_SRC_PLL0USB, CLK_SRC_PLL0AUDIO, CLK_SRC_PLL1
};

static u32 lpc18xx_cgu_idivbcde_src_ids[] = {
	CLK_SRC_OSC32, CLK_SRC_IRC, CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK, CLK_SRC_GP_CLKIN, CLK_SRC_OSC,
	CLK_SRC_PLL0AUDIO, CLK_SRC_PLL1, CLK_SRC_IDIVA,
};

static u32 lpc18xx_cgu_base_irc_src_ids[] = {CLK_SRC_IRC};

static u32 lpc18xx_cgu_base_usb0_src_ids[] = {CLK_SRC_PLL0USB};

static u32 lpc18xx_cgu_base_common_src_ids[] = {
	CLK_SRC_OSC32, CLK_SRC_IRC, CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK, CLK_SRC_GP_CLKIN, CLK_SRC_OSC,
	CLK_SRC_PLL0AUDIO, CLK_SRC_PLL1, CLK_SRC_IDIVA,
	CLK_SRC_IDIVB, CLK_SRC_IDIVC, CLK_SRC_IDIVD, CLK_SRC_IDIVE,
};

static u32 lpc18xx_cgu_base_all_src_ids[] = {
	CLK_SRC_OSC32, CLK_SRC_IRC, CLK_SRC_ENET_RX_CLK,
	CLK_SRC_ENET_TX_CLK, CLK_SRC_GP_CLKIN, CLK_SRC_OSC,
	CLK_SRC_PLL0USB, CLK_SRC_PLL0AUDIO, CLK_SRC_PLL1,
	CLK_SRC_IDIVA, CLK_SRC_IDIVB, CLK_SRC_IDIVC,
	CLK_SRC_IDIVD, CLK_SRC_IDIVE,
};

struct lpc18xx_cgu_src_clk_div {
	u8 clk_id;
	u8 n_parents;
	struct clk_divider	div;
	struct clk_mux		mux;
	struct clk_gate		gate;
};

#define LPC1XX_CGU_SRC_CLK_DIV(_id, _width, _table)	\
{							\
	.clk_id = CLK_SRC_ ##_id,			\
	.n_parents = ARRAY_SIZE(lpc18xx_cgu_ ##_table),	\
	.div = {					\
		.shift = 2,				\
		.width = _width,			\
	},						\
	.mux = {					\
		.mask = 0x1f,				\
		.shift = 24,				\
		.table = lpc18xx_cgu_ ##_table,		\
	},						\
	.gate = {					\
		.bit_idx = 0,				\
		.flags = CLK_GATE_SET_TO_DISABLE,	\
	},						\
}

static struct lpc18xx_cgu_src_clk_div lpc18xx_cgu_src_clk_divs[] = {
	LPC1XX_CGU_SRC_CLK_DIV(IDIVA, 2, idiva_src_ids),
	LPC1XX_CGU_SRC_CLK_DIV(IDIVB, 4, idivbcde_src_ids),
	LPC1XX_CGU_SRC_CLK_DIV(IDIVC, 4, idivbcde_src_ids),
	LPC1XX_CGU_SRC_CLK_DIV(IDIVD, 4, idivbcde_src_ids),
	LPC1XX_CGU_SRC_CLK_DIV(IDIVE, 8, idivbcde_src_ids),
};

struct lpc18xx_cgu_base_clk {
	u8 clk_id;
	u8 n_parents;
	struct clk_mux mux;
	struct clk_gate gate;
};

#define LPC1XX_CGU_BASE_CLK(_id, _table, _flags)	\
{							\
	.clk_id = BASE_ ##_id ##_CLK,			\
	.n_parents = ARRAY_SIZE(lpc18xx_cgu_ ##_table),	\
	.mux = {					\
		.mask = 0x1f,				\
		.shift = 24,				\
		.table = lpc18xx_cgu_ ##_table,		\
		.flags = _flags,			\
	},						\
	.gate = {					\
		.bit_idx = 0,				\
		.flags = CLK_GATE_SET_TO_DISABLE,	\
	},						\
}

static struct lpc18xx_cgu_base_clk lpc18xx_cgu_base_clks[] = {
	LPC1XX_CGU_BASE_CLK(SAFE,	base_irc_src_ids, CLK_MUX_READ_ONLY),
	LPC1XX_CGU_BASE_CLK(USB0,	base_usb0_src_ids,   0),
	LPC1XX_CGU_BASE_CLK(PERIPH,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(USB1,	base_all_src_ids,    0),
	LPC1XX_CGU_BASE_CLK(CPU,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(SPIFI,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(SPI,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(PHY_RX,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(PHY_TX,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(APB1,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(APB3,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(LCD,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(ADCHS,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(SDIO,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(SSP0,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(SSP1,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(UART0,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(UART1,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(UART2,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(UART3,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(OUT,	base_all_src_ids,    0),
	{ /* 21 reserved */ },
	{ /* 22 reserved */ },
	{ /* 23 reserved */ },
	{ /* 24 reserved */ },
	LPC1XX_CGU_BASE_CLK(AUDIO,	base_common_src_ids, 0),
	LPC1XX_CGU_BASE_CLK(CGU_OUT0,	base_all_src_ids,    0),
	LPC1XX_CGU_BASE_CLK(CGU_OUT1,	base_all_src_ids,    0),
};

struct lpc18xx_pll {
	struct		clk_hw hw;
	void __iomem	*reg;
	spinlock_t	*lock;
	u8		flags;
};

#define to_lpc_pll(hw) container_of(hw, struct lpc18xx_pll, hw)

struct lpc18xx_cgu_pll_clk {
	u8 clk_id;
	u8 n_parents;
	u8 reg_offset;
	struct clk_mux mux;
	struct clk_gate gate;
	struct lpc18xx_pll pll;
	const struct clk_ops *pll_ops;
};

#define LPC1XX_CGU_CLK_PLL(_id, _table, _pll_ops)	\
{							\
	.clk_id = CLK_SRC_ ##_id,			\
	.n_parents = ARRAY_SIZE(lpc18xx_cgu_ ##_table),	\
	.reg_offset = LPC18XX_CGU_ ##_id ##_STAT,	\
	.mux = {					\
		.mask = 0x1f,				\
		.shift = 24,				\
		.table = lpc18xx_cgu_ ##_table,		\
	},						\
	.gate = {					\
		.bit_idx = 0,				\
		.flags = CLK_GATE_SET_TO_DISABLE,	\
	},						\
	.pll_ops = &lpc18xx_ ##_pll_ops,		\
}

/*
 * PLL0 uses a special register value encoding. The compute functions below
 * are taken or derived from the LPC1850 user manual (section 12.6.3.3).
 */

/* Compute PLL0 multiplier from decoded version */
static u32 lpc18xx_pll0_mdec2msel(u32 x)
{
	int i;

	switch (x) {
	case 0x18003: return 1;
	case 0x10003: return 2;
	default:
		for (i = LPC18XX_PLL0_MSEL_MAX + 1; x != 0x4000 && i > 0; i--)
			x = ((x ^ x >> 14) & 1) | (x << 1 & 0x7fff);
		return i;
	}
}
/* Compute PLL0 decoded multiplier from binary version */
static u32 lpc18xx_pll0_msel2mdec(u32 msel)
{
	u32 i, x = 0x4000;

	switch (msel) {
	case 0: return 0;
	case 1: return 0x18003;
	case 2: return 0x10003;
	default:
		for (i = msel; i <= LPC18XX_PLL0_MSEL_MAX; i++)
			x = ((x ^ x >> 1) & 1) << 14 | (x >> 1 & 0xffff);
		return x;
	}
}

/* Compute PLL0 bandwidth SELI reg from multiplier */
static u32 lpc18xx_pll0_msel2seli(u32 msel)
{
	u32 tmp;

	if (msel > 16384) return 1;
	if (msel >  8192) return 2;
	if (msel >  2048) return 4;
	if (msel >=  501) return 8;
	if (msel >=   60) {
		tmp = 1024 / (msel + 9);
		return ((1024 == (tmp * (msel + 9))) == 0) ? tmp * 4 : (tmp + 1) * 4;
	}

	return (msel & 0x3c) + 4;
}

/* Compute PLL0 bandwidth SELP reg from multiplier */
static u32 lpc18xx_pll0_msel2selp(u32 msel)
{
	if (msel < 60)
		return (msel >> 1) + 1;

	return 31;
}

static unsigned long lpc18xx_pll0_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct lpc18xx_pll *pll = to_lpc_pll(hw);
	u32 ctrl, mdiv, msel, npdiv;

	ctrl = readl(pll->reg + LPC18XX_CGU_PLL0USB_CTRL);
	mdiv = readl(pll->reg + LPC18XX_CGU_PLL0USB_MDIV);
	npdiv = readl(pll->reg + LPC18XX_CGU_PLL0USB_NP_DIV);

	if (ctrl & LPC18XX_PLL0_CTRL_BYPASS)
		return parent_rate;

	if (npdiv != LPC18XX_PLL0_NP_DIVS_1) {
		pr_warn("%s: pre/post dividers not supported\n", __func__);
		return 0;
	}

	msel = lpc18xx_pll0_mdec2msel(mdiv & LPC18XX_PLL0_MDIV_MDEC_MASK);
	if (msel)
		return 2 * msel * parent_rate;

	pr_warn("%s: unable to calculate rate\n", __func__);

	return 0;
}

static long lpc18xx_pll0_round_rate(struct clk_hw *hw, unsigned long rate,
				    unsigned long *prate)
{
	unsigned long m;

	if (*prate < rate) {
		pr_warn("%s: pll dividers not supported\n", __func__);
		return -EINVAL;
	}

	m = DIV_ROUND_UP_ULL(*prate, rate * 2);
	if (m <= 0 && m > LPC18XX_PLL0_MSEL_MAX) {
		pr_warn("%s: unable to support rate %lu\n", __func__, rate);
		return -EINVAL;
	}

	return 2 * *prate * m;
}

static int lpc18xx_pll0_set_rate(struct clk_hw *hw, unsigned long rate,
				 unsigned long parent_rate)
{
	struct lpc18xx_pll *pll = to_lpc_pll(hw);
	u32 ctrl, stat, m;
	int retry = 3;

	if (parent_rate < rate) {
		pr_warn("%s: pll dividers not supported\n", __func__);
		return -EINVAL;
	}

	m = DIV_ROUND_UP_ULL(parent_rate, rate * 2);
	if (m <= 0 && m > LPC18XX_PLL0_MSEL_MAX) {
		pr_warn("%s: unable to support rate %lu\n", __func__, rate);
		return -EINVAL;
	}

	m  = lpc18xx_pll0_msel2mdec(m);
	m |= lpc18xx_pll0_msel2selp(m) << LPC18XX_PLL0_MDIV_SELP_SHIFT;
	m |= lpc18xx_pll0_msel2seli(m) << LPC18XX_PLL0_MDIV_SELI_SHIFT;

	/* Power down PLL, disable clk output and dividers */
	ctrl = readl(pll->reg + LPC18XX_CGU_PLL0USB_CTRL);
	ctrl |= LPC18XX_PLL0_CTRL_PD;
	ctrl &= ~(LPC18XX_PLL0_CTRL_BYPASS | LPC18XX_PLL0_CTRL_DIRECTI |
		  LPC18XX_PLL0_CTRL_DIRECTO | LPC18XX_PLL0_CTRL_CLKEN);
	writel(ctrl, pll->reg + LPC18XX_CGU_PLL0USB_CTRL);

	/* Configure new PLL settings */
	writel(m, pll->reg + LPC18XX_CGU_PLL0USB_MDIV);
	writel(LPC18XX_PLL0_NP_DIVS_1, pll->reg + LPC18XX_CGU_PLL0USB_NP_DIV);

	/* Power up PLL and wait for lock */
	ctrl &= ~LPC18XX_PLL0_CTRL_PD;
	writel(ctrl, pll->reg + LPC18XX_CGU_PLL0USB_CTRL);
	do {
		udelay(10);
		stat = readl(pll->reg + LPC18XX_CGU_PLL0USB_STAT);
		if (stat & LPC18XX_PLL0_STAT_LOCK) {
			ctrl |= LPC18XX_PLL0_CTRL_CLKEN;
			writel(ctrl, pll->reg + LPC18XX_CGU_PLL0USB_CTRL);

			return 0;
		}
	} while (retry--);

	pr_warn("%s: unable to lock pll\n", __func__);

	return -EINVAL;
}

static const struct clk_ops lpc18xx_pll0_ops = {
	.recalc_rate	= lpc18xx_pll0_recalc_rate,
	.round_rate	= lpc18xx_pll0_round_rate,
	.set_rate	= lpc18xx_pll0_set_rate,
};

static unsigned long lpc18xx_pll1_recalc_rate(struct clk_hw *hw,
					      unsigned long parent_rate)
{
	struct lpc18xx_pll *pll = to_lpc_pll(hw);
	u16 msel, nsel, psel;
	bool direct, fbsel;
	u32 stat, ctrl;

	stat = readl(pll->reg + LPC18XX_CGU_PLL1_STAT);
	ctrl = readl(pll->reg + LPC18XX_CGU_PLL1_CTRL);

	direct = (ctrl & LPC18XX_PLL1_CTRL_DIRECT) ? true : false;
	fbsel = (ctrl & LPC18XX_PLL1_CTRL_FBSEL) ? true : false;

	msel = ((ctrl >> 16) & 0xff) + 1;
	nsel = ((ctrl >> 12) & 0x3) + 1;

	if (direct || fbsel)
		return msel * (parent_rate / nsel);

	psel = (ctrl >>  8) & 0x3;
	psel = 1 << psel;

	return (msel / (2 * psel)) * (parent_rate / nsel);
}

static const struct clk_ops lpc18xx_pll1_ops = {
	.recalc_rate = lpc18xx_pll1_recalc_rate,
};

static int lpc18xx_cgu_gate_enable(struct clk_hw *hw)
{
	return clk_gate_ops.enable(hw);
}

static void lpc18xx_cgu_gate_disable(struct clk_hw *hw)
{
	clk_gate_ops.disable(hw);
}

static int lpc18xx_cgu_gate_is_enabled(struct clk_hw *hw)
{
	const struct clk_hw *parent;

	/*
	 * The consumer of base clocks needs know if the
	 * base clock is really enabled before it can be
	 * accessed. It is therefore necessary to verify
	 * this all the way up.
	 */
	parent = clk_hw_get_parent(hw);
	if (!parent)
		return 0;

	if (!clk_hw_is_enabled(parent))
		return 0;

	return clk_gate_ops.is_enabled(hw);
}

static const struct clk_ops lpc18xx_gate_ops = {
	.enable = lpc18xx_cgu_gate_enable,
	.disable = lpc18xx_cgu_gate_disable,
	.is_enabled = lpc18xx_cgu_gate_is_enabled,
};

static struct lpc18xx_cgu_pll_clk lpc18xx_cgu_src_clk_plls[] = {
	LPC1XX_CGU_CLK_PLL(PLL0USB,	pll0_src_ids, pll0_ops),
	LPC1XX_CGU_CLK_PLL(PLL0AUDIO,	pll0_src_ids, pll0_ops),
	LPC1XX_CGU_CLK_PLL(PLL1,	pll1_src_ids, pll1_ops),
};

static void lpc18xx_fill_parent_names(const char **parent, u32 *id, int size)
{
	int i;

	for (i = 0; i < size; i++)
		parent[i] = clk_src_names[id[i]];
}

static struct clk *lpc18xx_cgu_register_div(struct lpc18xx_cgu_src_clk_div *clk,
					    void __iomem *base, int n)
{
	void __iomem *reg = base + LPC18XX_CGU_IDIV_CTRL(n);
	const char *name = clk_src_names[clk->clk_id];
	const char *parents[CLK_SRC_MAX];

	clk->div.reg = reg;
	clk->mux.reg = reg;
	clk->gate.reg = reg;

	lpc18xx_fill_parent_names(parents, clk->mux.table, clk->n_parents);

	return clk_register_composite(NULL, name, parents, clk->n_parents,
				      &clk->mux.hw, &clk_mux_ops,
				      &clk->div.hw, &clk_divider_ops,
				      &clk->gate.hw, &lpc18xx_gate_ops, 0);
}


static struct clk *lpc18xx_register_base_clk(struct lpc18xx_cgu_base_clk *clk,
					     void __iomem *reg_base, int n)
{
	void __iomem *reg = reg_base + LPC18XX_CGU_BASE_CLK(n);
	const char *name = clk_base_names[clk->clk_id];
	const char *parents[CLK_SRC_MAX];

	if (clk->n_parents == 0)
		return ERR_PTR(-ENOENT);

	clk->mux.reg = reg;
	clk->gate.reg = reg;

	lpc18xx_fill_parent_names(parents, clk->mux.table, clk->n_parents);

	/* SAFE_CLK can not be turned off */
	if (n == BASE_SAFE_CLK)
		return clk_register_composite(NULL, name, parents, clk->n_parents,
					      &clk->mux.hw, &clk_mux_ops,
					      NULL, NULL, NULL, NULL, 0);

	return clk_register_composite(NULL, name, parents, clk->n_parents,
				      &clk->mux.hw, &clk_mux_ops,
				      NULL,  NULL,
				      &clk->gate.hw, &lpc18xx_gate_ops, 0);
}


static struct clk *lpc18xx_cgu_register_pll(struct lpc18xx_cgu_pll_clk *clk,
					    void __iomem *base)
{
	const char *name = clk_src_names[clk->clk_id];
	const char *parents[CLK_SRC_MAX];

	clk->pll.reg  = base;
	clk->mux.reg  = base + clk->reg_offset + LPC18XX_CGU_PLL_CTRL_OFFSET;
	clk->gate.reg = base + clk->reg_offset + LPC18XX_CGU_PLL_CTRL_OFFSET;

	lpc18xx_fill_parent_names(parents, clk->mux.table, clk->n_parents);

	return clk_register_composite(NULL, name, parents, clk->n_parents,
				      &clk->mux.hw, &clk_mux_ops,
				      &clk->pll.hw, clk->pll_ops,
				      &clk->gate.hw, &lpc18xx_gate_ops, 0);
}

static void __init lpc18xx_cgu_register_source_clks(struct device_node *np,
						    void __iomem *base)
{
	const char *parents[CLK_SRC_MAX];
	struct clk *clk;
	int i;

	/* Register the internal 12 MHz RC oscillator (IRC) */
	clk = clk_register_fixed_rate(NULL, clk_src_names[CLK_SRC_IRC],
				      NULL, 0, 12000000);
	if (IS_ERR(clk))
		pr_warn("%s: failed to register irc clk\n", __func__);

	/* Register crystal oscillator controlller */
	parents[0] = of_clk_get_parent_name(np, 0);
	clk = clk_register_gate(NULL, clk_src_names[CLK_SRC_OSC], parents[0],
				0, base + LPC18XX_CGU_XTAL_OSC_CTRL,
				0, CLK_GATE_SET_TO_DISABLE, NULL);
	if (IS_ERR(clk))
		pr_warn("%s: failed to register osc clk\n", __func__);

	/* Register all PLLs */
	for (i = 0; i < ARRAY_SIZE(lpc18xx_cgu_src_clk_plls); i++) {
		clk = lpc18xx_cgu_register_pll(&lpc18xx_cgu_src_clk_plls[i],
						   base);
		if (IS_ERR(clk))
			pr_warn("%s: failed to register pll (%d)\n", __func__, i);
	}

	/* Register all clock dividers A-E */
	for (i = 0; i < ARRAY_SIZE(lpc18xx_cgu_src_clk_divs); i++) {
		clk = lpc18xx_cgu_register_div(&lpc18xx_cgu_src_clk_divs[i],
					       base, i);
		if (IS_ERR(clk))
			pr_warn("%s: failed to register div %d\n", __func__, i);
	}
}

static struct clk *clk_base[BASE_CLK_MAX];
static struct clk_onecell_data clk_base_data = {
	.clks = clk_base,
	.clk_num = BASE_CLK_MAX,
};

static void __init lpc18xx_cgu_register_base_clks(void __iomem *reg_base)
{
	int i;

	for (i = BASE_SAFE_CLK; i < BASE_CLK_MAX; i++) {
		clk_base[i] = lpc18xx_register_base_clk(&lpc18xx_cgu_base_clks[i],
							reg_base, i);
		if (IS_ERR(clk_base[i]) && PTR_ERR(clk_base[i]) != -ENOENT)
			pr_warn("%s: register base clk %d failed\n", __func__, i);
	}
}

static void __init lpc18xx_cgu_init(struct device_node *np)
{
	void __iomem *reg_base;

	reg_base = of_iomap(np, 0);
	if (!reg_base) {
		pr_warn("%s: failed to map address range\n", __func__);
		return;
	}

	lpc18xx_cgu_register_source_clks(np, reg_base);
	lpc18xx_cgu_register_base_clks(reg_base);

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_base_data);
}
CLK_OF_DECLARE(lpc18xx_cgu, "nxp,lpc1850-cgu", lpc18xx_cgu_init);
