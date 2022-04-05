// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * common clks module for all SiRF SoCs
 *
 * Copyright (c) 2011 - 2014 Cambridge Silicon Radio Limited, a CSR plc group
 * company.
 */

#include <linux/clk.h>

#define KHZ     1000
#define MHZ     (KHZ * KHZ)

static void __iomem *sirfsoc_clk_vbase;
static void __iomem *sirfsoc_rsc_vbase;
static struct clk_onecell_data clk_data;

/*
 * SiRFprimaII clock controller
 * - 2 oscillators: osc-26MHz, rtc-32.768KHz
 * - 3 standard configurable plls: pll1, pll2 & pll3
 * - 2 exclusive plls: usb phy pll and sata phy pll
 * - 8 clock domains: cpu/cpudiv, mem/memdiv, sys/io, dsp, graphic, multimedia,
 *     display and sdphy.
 *     Each clock domain can select its own clock source from five clock sources,
 *     X_XIN, X_XINW, PLL1, PLL2 and PLL3. The domain clock is used as the source
 *     clock of the group clock.
 *     - dsp domain: gps, mf
 *     - io domain: dmac, nand, audio, uart, i2c, spi, usp, pwm, pulse
 *     - sys domain: security
 */

struct clk_pll {
	struct clk_hw hw;
	unsigned short regofs;  /* register offset */
};

#define to_pllclk(_hw) container_of(_hw, struct clk_pll, hw)

struct clk_dmn {
	struct clk_hw hw;
	signed char enable_bit; /* enable bit: 0 ~ 63 */
	unsigned short regofs;  /* register offset */
};

#define to_dmnclk(_hw) container_of(_hw, struct clk_dmn, hw)

struct clk_std {
	struct clk_hw hw;
	signed char enable_bit; /* enable bit: 0 ~ 63 */
};

#define to_stdclk(_hw) container_of(_hw, struct clk_std, hw)

static int std_clk_is_enabled(struct clk_hw *hw);
static int std_clk_enable(struct clk_hw *hw);
static void std_clk_disable(struct clk_hw *hw);

static inline unsigned long clkc_readl(unsigned reg)
{
	return readl(sirfsoc_clk_vbase + reg);
}

static inline void clkc_writel(u32 val, unsigned reg)
{
	writel(val, sirfsoc_clk_vbase + reg);
}

/*
 * std pll
 */

static unsigned long pll_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	unsigned long fin = parent_rate;
	struct clk_pll *clk = to_pllclk(hw);
	u32 regcfg2 = clk->regofs + SIRFSOC_CLKC_PLL1_CFG2 -
		SIRFSOC_CLKC_PLL1_CFG0;

	if (clkc_readl(regcfg2) & BIT(2)) {
		/* pll bypass mode */
		return fin;
	} else {
		/* fout = fin * nf / nr / od */
		u32 cfg0 = clkc_readl(clk->regofs);
		u32 nf = (cfg0 & (BIT(13) - 1)) + 1;
		u32 nr = ((cfg0 >> 13) & (BIT(6) - 1)) + 1;
		u32 od = ((cfg0 >> 19) & (BIT(4) - 1)) + 1;
		WARN_ON(fin % MHZ);
		return fin / MHZ * nf / nr / od * MHZ;
	}
}

static long pll_clk_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	unsigned long fin, nf, nr, od;
	u64 dividend;

	/*
	 * fout = fin * nf / (nr * od);
	 * set od = 1, nr = fin/MHz, so fout = nf * MHz
	 */
	rate = rate - rate % MHZ;

	nf = rate / MHZ;
	if (nf > BIT(13))
		nf = BIT(13);
	if (nf < 1)
		nf = 1;

	fin = *parent_rate;

	nr = fin / MHZ;
	if (nr > BIT(6))
		nr = BIT(6);
	od = 1;

	dividend = (u64)fin * nf;
	do_div(dividend, nr * od);

	return (long)dividend;
}

static int pll_clk_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	struct clk_pll *clk = to_pllclk(hw);
	unsigned long fin, nf, nr, od, reg;

	/*
	 * fout = fin * nf / (nr * od);
	 * set od = 1, nr = fin/MHz, so fout = nf * MHz
	 */

	nf = rate / MHZ;
	if (unlikely((rate % MHZ) || nf > BIT(13) || nf < 1))
		return -EINVAL;

	fin = parent_rate;
	BUG_ON(fin < MHZ);

	nr = fin / MHZ;
	BUG_ON((fin % MHZ) || nr > BIT(6));

	od = 1;

	reg = (nf - 1) | ((nr - 1) << 13) | ((od - 1) << 19);
	clkc_writel(reg, clk->regofs);

	reg = clk->regofs + SIRFSOC_CLKC_PLL1_CFG1 - SIRFSOC_CLKC_PLL1_CFG0;
	clkc_writel((nf >> 1) - 1, reg);

	reg = clk->regofs + SIRFSOC_CLKC_PLL1_CFG2 - SIRFSOC_CLKC_PLL1_CFG0;
	while (!(clkc_readl(reg) & BIT(6)))
		cpu_relax();

	return 0;
}

static long cpu_clk_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	/*
	 * SiRF SoC has not cpu clock control,
	 * So bypass to it's parent pll.
	 */
	struct clk_hw *parent_clk = clk_hw_get_parent(hw);
	struct clk_hw *pll_parent_clk = clk_hw_get_parent(parent_clk);
	unsigned long pll_parent_rate = clk_hw_get_rate(pll_parent_clk);
	return pll_clk_round_rate(parent_clk, rate, &pll_parent_rate);
}

static unsigned long cpu_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)
{
	/*
	 * SiRF SoC has not cpu clock control,
	 * So return the parent pll rate.
	 */
	struct clk_hw *parent_clk = clk_hw_get_parent(hw);
	return clk_hw_get_rate(parent_clk);
}

static const struct clk_ops std_pll_ops = {
	.recalc_rate = pll_clk_recalc_rate,
	.round_rate = pll_clk_round_rate,
	.set_rate = pll_clk_set_rate,
};

static const char * const pll_clk_parents[] = {
	"osc",
};

static const struct clk_init_data clk_pll1_init = {
	.name = "pll1",
	.ops = &std_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static const struct clk_init_data clk_pll2_init = {
	.name = "pll2",
	.ops = &std_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static const struct clk_init_data clk_pll3_init = {
	.name = "pll3",
	.ops = &std_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_pll clk_pll1 = {
	.regofs = SIRFSOC_CLKC_PLL1_CFG0,
	.hw = {
		.init = &clk_pll1_init,
	},
};

static struct clk_pll clk_pll2 = {
	.regofs = SIRFSOC_CLKC_PLL2_CFG0,
	.hw = {
		.init = &clk_pll2_init,
	},
};

static struct clk_pll clk_pll3 = {
	.regofs = SIRFSOC_CLKC_PLL3_CFG0,
	.hw = {
		.init = &clk_pll3_init,
	},
};

/*
 * usb uses specified pll
 */

static int usb_pll_clk_enable(struct clk_hw *hw)
{
	u32 reg = readl(sirfsoc_rsc_vbase + SIRFSOC_USBPHY_PLL_CTRL);
	reg &= ~(SIRFSOC_USBPHY_PLL_POWERDOWN | SIRFSOC_USBPHY_PLL_BYPASS);
	writel(reg, sirfsoc_rsc_vbase + SIRFSOC_USBPHY_PLL_CTRL);
	while (!(readl(sirfsoc_rsc_vbase + SIRFSOC_USBPHY_PLL_CTRL) &
			SIRFSOC_USBPHY_PLL_LOCK))
		cpu_relax();

	return 0;
}

static void usb_pll_clk_disable(struct clk_hw *clk)
{
	u32 reg = readl(sirfsoc_rsc_vbase + SIRFSOC_USBPHY_PLL_CTRL);
	reg |= (SIRFSOC_USBPHY_PLL_POWERDOWN | SIRFSOC_USBPHY_PLL_BYPASS);
	writel(reg, sirfsoc_rsc_vbase + SIRFSOC_USBPHY_PLL_CTRL);
}

static unsigned long usb_pll_clk_recalc_rate(struct clk_hw *hw, unsigned long parent_rate)
{
	u32 reg = readl(sirfsoc_rsc_vbase + SIRFSOC_USBPHY_PLL_CTRL);
	return (reg & SIRFSOC_USBPHY_PLL_BYPASS) ? parent_rate : 48*MHZ;
}

static const struct clk_ops usb_pll_ops = {
	.enable = usb_pll_clk_enable,
	.disable = usb_pll_clk_disable,
	.recalc_rate = usb_pll_clk_recalc_rate,
};

static const struct clk_init_data clk_usb_pll_init = {
	.name = "usb_pll",
	.ops = &usb_pll_ops,
	.parent_names = pll_clk_parents,
	.num_parents = ARRAY_SIZE(pll_clk_parents),
};

static struct clk_hw usb_pll_clk_hw = {
	.init = &clk_usb_pll_init,
};

/*
 * clock domains - cpu, mem, sys/io, dsp, gfx
 */

static const char * const dmn_clk_parents[] = {
	"rtc",
	"osc",
	"pll1",
	"pll2",
	"pll3",
};

static u8 dmn_clk_get_parent(struct clk_hw *hw)
{
	struct clk_dmn *clk = to_dmnclk(hw);
	u32 cfg = clkc_readl(clk->regofs);
	const char *name = clk_hw_get_name(hw);

	/* parent of io domain can only be pll3 */
	if (strcmp(name, "io") == 0)
		return 4;

	WARN_ON((cfg & (BIT(3) - 1)) > 4);

	return cfg & (BIT(3) - 1);
}

static int dmn_clk_set_parent(struct clk_hw *hw, u8 parent)
{
	struct clk_dmn *clk = to_dmnclk(hw);
	u32 cfg = clkc_readl(clk->regofs);
	const char *name = clk_hw_get_name(hw);

	/* parent of io domain can only be pll3 */
	if (strcmp(name, "io") == 0)
		return -EINVAL;

	cfg &= ~(BIT(3) - 1);
	clkc_writel(cfg | parent, clk->regofs);
	/* BIT(3) - switching status: 1 - busy, 0 - done */
	while (clkc_readl(clk->regofs) & BIT(3))
		cpu_relax();

	return 0;
}

static unsigned long dmn_clk_recalc_rate(struct clk_hw *hw,
	unsigned long parent_rate)

{
	unsigned long fin = parent_rate;
	struct clk_dmn *clk = to_dmnclk(hw);

	u32 cfg = clkc_readl(clk->regofs);

	if (cfg & BIT(24)) {
		/* fcd bypass mode */
		return fin;
	} else {
		/*
		 * wait count: bit[19:16], hold count: bit[23:20]
		 */
		u32 wait = (cfg >> 16) & (BIT(4) - 1);
		u32 hold = (cfg >> 20) & (BIT(4) - 1);

		return fin / (wait + hold + 2);
	}
}

static long dmn_clk_round_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long *parent_rate)
{
	unsigned long fin;
	unsigned ratio, wait, hold;
	const char *name = clk_hw_get_name(hw);
	unsigned bits = (strcmp(name, "mem") == 0) ? 3 : 4;

	fin = *parent_rate;
	ratio = fin / rate;

	if (ratio < 2)
		ratio = 2;
	if (ratio > BIT(bits + 1))
		ratio = BIT(bits + 1);

	wait = (ratio >> 1) - 1;
	hold = ratio - wait - 2;

	return fin / (wait + hold + 2);
}

static int dmn_clk_set_rate(struct clk_hw *hw, unsigned long rate,
	unsigned long parent_rate)
{
	struct clk_dmn *clk = to_dmnclk(hw);
	unsigned long fin;
	unsigned ratio, wait, hold, reg;
	const char *name = clk_hw_get_name(hw);
	unsigned bits = (strcmp(name, "mem") == 0) ? 3 : 4;

	fin = parent_rate;
	ratio = fin / rate;

	if (unlikely(ratio < 2 || ratio > BIT(bits + 1)))
		return -EINVAL;

	WARN_ON(fin % rate);

	wait = (ratio >> 1) - 1;
	hold = ratio - wait - 2;

	reg = clkc_readl(clk->regofs);
	reg &= ~(((BIT(bits) - 1) << 16) | ((BIT(bits) - 1) << 20));
	reg |= (wait << 16) | (hold << 20) | BIT(25);
	clkc_writel(reg, clk->regofs);

	/* waiting FCD been effective */
	while (clkc_readl(clk->regofs) & BIT(25))
		cpu_relax();

	return 0;
}

static int cpu_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret1, ret2;
	struct clk *cur_parent;

	if (rate == clk_get_rate(clk_pll1.hw.clk)) {
		ret1 = clk_set_parent(hw->clk, clk_pll1.hw.clk);
		return ret1;
	}

	if (rate == clk_get_rate(clk_pll2.hw.clk)) {
		ret1 = clk_set_parent(hw->clk, clk_pll2.hw.clk);
		return ret1;
	}

	if (rate == clk_get_rate(clk_pll3.hw.clk)) {
		ret1 = clk_set_parent(hw->clk, clk_pll3.hw.clk);
		return ret1;
	}

	cur_parent = clk_get_parent(hw->clk);

	/* switch to tmp pll before setting parent clock's rate */
	if (cur_parent ==  clk_pll1.hw.clk) {
		ret1 = clk_set_parent(hw->clk, clk_pll2.hw.clk);
		BUG_ON(ret1);
	}

	ret2 = clk_set_rate(clk_pll1.hw.clk, rate);

	ret1 = clk_set_parent(hw->clk, clk_pll1.hw.clk);

	return ret2 ? ret2 : ret1;
}

static const struct clk_ops msi_ops = {
	.set_rate = dmn_clk_set_rate,
	.round_rate = dmn_clk_round_rate,
	.recalc_rate = dmn_clk_recalc_rate,
	.set_parent = dmn_clk_set_parent,
	.get_parent = dmn_clk_get_parent,
};

static const struct clk_init_data clk_mem_init = {
	.name = "mem",
	.ops = &msi_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_mem = {
	.regofs = SIRFSOC_CLKC_MEM_CFG,
	.hw = {
		.init = &clk_mem_init,
	},
};

static const struct clk_init_data clk_sys_init = {
	.name = "sys",
	.ops = &msi_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
	.flags = CLK_SET_RATE_GATE,
};

static struct clk_dmn clk_sys = {
	.regofs = SIRFSOC_CLKC_SYS_CFG,
	.hw = {
		.init = &clk_sys_init,
	},
};

static const struct clk_init_data clk_io_init = {
	.name = "io",
	.ops = &msi_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_io = {
	.regofs = SIRFSOC_CLKC_IO_CFG,
	.hw = {
		.init = &clk_io_init,
	},
};

static const struct clk_ops cpu_ops = {
	.set_parent = dmn_clk_set_parent,
	.get_parent = dmn_clk_get_parent,
	.set_rate = cpu_clk_set_rate,
	.round_rate = cpu_clk_round_rate,
	.recalc_rate = cpu_clk_recalc_rate,
};

static const struct clk_init_data clk_cpu_init = {
	.name = "cpu",
	.ops = &cpu_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
	.flags = CLK_SET_RATE_PARENT,
};

static struct clk_dmn clk_cpu = {
	.regofs = SIRFSOC_CLKC_CPU_CFG,
	.hw = {
		.init = &clk_cpu_init,
	},
};

static const struct clk_ops dmn_ops = {
	.is_enabled = std_clk_is_enabled,
	.enable = std_clk_enable,
	.disable = std_clk_disable,
	.set_rate = dmn_clk_set_rate,
	.round_rate = dmn_clk_round_rate,
	.recalc_rate = dmn_clk_recalc_rate,
	.set_parent = dmn_clk_set_parent,
	.get_parent = dmn_clk_get_parent,
};

/* dsp, gfx, mm, lcd and vpp domain */

static const struct clk_init_data clk_dsp_init = {
	.name = "dsp",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_dsp = {
	.regofs = SIRFSOC_CLKC_DSP_CFG,
	.enable_bit = 0,
	.hw = {
		.init = &clk_dsp_init,
	},
};

static const struct clk_init_data clk_gfx_init = {
	.name = "gfx",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_gfx = {
	.regofs = SIRFSOC_CLKC_GFX_CFG,
	.enable_bit = 8,
	.hw = {
		.init = &clk_gfx_init,
	},
};

static const struct clk_init_data clk_mm_init = {
	.name = "mm",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_mm = {
	.regofs = SIRFSOC_CLKC_MM_CFG,
	.enable_bit = 9,
	.hw = {
		.init = &clk_mm_init,
	},
};

/*
 * for atlas6, gfx2d holds the bit of prima2's clk_mm
 */
#define clk_gfx2d clk_mm

static const struct clk_init_data clk_lcd_init = {
	.name = "lcd",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_lcd = {
	.regofs = SIRFSOC_CLKC_LCD_CFG,
	.enable_bit = 10,
	.hw = {
		.init = &clk_lcd_init,
	},
};

static const struct clk_init_data clk_vpp_init = {
	.name = "vpp",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static struct clk_dmn clk_vpp = {
	.regofs = SIRFSOC_CLKC_LCD_CFG,
	.enable_bit = 11,
	.hw = {
		.init = &clk_vpp_init,
	},
};

static const struct clk_init_data clk_mmc01_init = {
	.name = "mmc01",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static const struct clk_init_data clk_mmc23_init = {
	.name = "mmc23",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

static const struct clk_init_data clk_mmc45_init = {
	.name = "mmc45",
	.ops = &dmn_ops,
	.parent_names = dmn_clk_parents,
	.num_parents = ARRAY_SIZE(dmn_clk_parents),
};

/*
 * peripheral controllers in io domain
 */

static int std_clk_is_enabled(struct clk_hw *hw)
{
	u32 reg;
	int bit;
	struct clk_std *clk = to_stdclk(hw);

	bit = clk->enable_bit % 32;
	reg = clk->enable_bit / 32;
	reg = SIRFSOC_CLKC_CLK_EN0 + reg * sizeof(reg);

	return !!(clkc_readl(reg) & BIT(bit));
}

static int std_clk_enable(struct clk_hw *hw)
{
	u32 val, reg;
	int bit;
	struct clk_std *clk = to_stdclk(hw);

	BUG_ON(clk->enable_bit < 0 || clk->enable_bit > 63);

	bit = clk->enable_bit % 32;
	reg = clk->enable_bit / 32;
	reg = SIRFSOC_CLKC_CLK_EN0 + reg * sizeof(reg);

	val = clkc_readl(reg) | BIT(bit);
	clkc_writel(val, reg);
	return 0;
}

static void std_clk_disable(struct clk_hw *hw)
{
	u32 val, reg;
	int bit;
	struct clk_std *clk = to_stdclk(hw);

	BUG_ON(clk->enable_bit < 0 || clk->enable_bit > 63);

	bit = clk->enable_bit % 32;
	reg = clk->enable_bit / 32;
	reg = SIRFSOC_CLKC_CLK_EN0 + reg * sizeof(reg);

	val = clkc_readl(reg) & ~BIT(bit);
	clkc_writel(val, reg);
}

static const char * const std_clk_io_parents[] = {
	"io",
};

static const struct clk_ops ios_ops = {
	.is_enabled = std_clk_is_enabled,
	.enable = std_clk_enable,
	.disable = std_clk_disable,
};

static const struct clk_init_data clk_cphif_init = {
	.name = "cphif",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_cphif = {
	.enable_bit = 20,
	.hw = {
		.init = &clk_cphif_init,
	},
};

static const struct clk_init_data clk_dmac0_init = {
	.name = "dmac0",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_dmac0 = {
	.enable_bit = 32,
	.hw = {
		.init = &clk_dmac0_init,
	},
};

static const struct clk_init_data clk_dmac1_init = {
	.name = "dmac1",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_dmac1 = {
	.enable_bit = 33,
	.hw = {
		.init = &clk_dmac1_init,
	},
};

static const struct clk_init_data clk_audio_init = {
	.name = "audio",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_audio = {
	.enable_bit = 35,
	.hw = {
		.init = &clk_audio_init,
	},
};

static const struct clk_init_data clk_uart0_init = {
	.name = "uart0",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_uart0 = {
	.enable_bit = 36,
	.hw = {
		.init = &clk_uart0_init,
	},
};

static const struct clk_init_data clk_uart1_init = {
	.name = "uart1",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_uart1 = {
	.enable_bit = 37,
	.hw = {
		.init = &clk_uart1_init,
	},
};

static const struct clk_init_data clk_uart2_init = {
	.name = "uart2",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_uart2 = {
	.enable_bit = 38,
	.hw = {
		.init = &clk_uart2_init,
	},
};

static const struct clk_init_data clk_usp0_init = {
	.name = "usp0",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_usp0 = {
	.enable_bit = 39,
	.hw = {
		.init = &clk_usp0_init,
	},
};

static const struct clk_init_data clk_usp1_init = {
	.name = "usp1",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_usp1 = {
	.enable_bit = 40,
	.hw = {
		.init = &clk_usp1_init,
	},
};

static const struct clk_init_data clk_usp2_init = {
	.name = "usp2",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_usp2 = {
	.enable_bit = 41,
	.hw = {
		.init = &clk_usp2_init,
	},
};

static const struct clk_init_data clk_vip_init = {
	.name = "vip",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_vip = {
	.enable_bit = 42,
	.hw = {
		.init = &clk_vip_init,
	},
};

static const struct clk_init_data clk_spi0_init = {
	.name = "spi0",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_spi0 = {
	.enable_bit = 43,
	.hw = {
		.init = &clk_spi0_init,
	},
};

static const struct clk_init_data clk_spi1_init = {
	.name = "spi1",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_spi1 = {
	.enable_bit = 44,
	.hw = {
		.init = &clk_spi1_init,
	},
};

static const struct clk_init_data clk_tsc_init = {
	.name = "tsc",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_tsc = {
	.enable_bit = 45,
	.hw = {
		.init = &clk_tsc_init,
	},
};

static const struct clk_init_data clk_i2c0_init = {
	.name = "i2c0",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_i2c0 = {
	.enable_bit = 46,
	.hw = {
		.init = &clk_i2c0_init,
	},
};

static const struct clk_init_data clk_i2c1_init = {
	.name = "i2c1",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_i2c1 = {
	.enable_bit = 47,
	.hw = {
		.init = &clk_i2c1_init,
	},
};

static const struct clk_init_data clk_pwmc_init = {
	.name = "pwmc",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_pwmc = {
	.enable_bit = 48,
	.hw = {
		.init = &clk_pwmc_init,
	},
};

static const struct clk_init_data clk_efuse_init = {
	.name = "efuse",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_efuse = {
	.enable_bit = 49,
	.hw = {
		.init = &clk_efuse_init,
	},
};

static const struct clk_init_data clk_pulse_init = {
	.name = "pulse",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_pulse = {
	.enable_bit = 50,
	.hw = {
		.init = &clk_pulse_init,
	},
};

static const char * const std_clk_dsp_parents[] = {
	"dsp",
};

static const struct clk_init_data clk_gps_init = {
	.name = "gps",
	.ops = &ios_ops,
	.parent_names = std_clk_dsp_parents,
	.num_parents = ARRAY_SIZE(std_clk_dsp_parents),
};

static struct clk_std clk_gps = {
	.enable_bit = 1,
	.hw = {
		.init = &clk_gps_init,
	},
};

static const struct clk_init_data clk_mf_init = {
	.name = "mf",
	.ops = &ios_ops,
	.parent_names = std_clk_io_parents,
	.num_parents = ARRAY_SIZE(std_clk_io_parents),
};

static struct clk_std clk_mf = {
	.enable_bit = 2,
	.hw = {
		.init = &clk_mf_init,
	},
};

static const char * const std_clk_sys_parents[] = {
	"sys",
};

static const struct clk_init_data clk_security_init = {
	.name = "security",
	.ops = &ios_ops,
	.parent_names = std_clk_sys_parents,
	.num_parents = ARRAY_SIZE(std_clk_sys_parents),
};

static struct clk_std clk_security = {
	.enable_bit = 19,
	.hw = {
		.init = &clk_security_init,
	},
};

static const char * const std_clk_usb_parents[] = {
	"usb_pll",
};

static const struct clk_init_data clk_usb0_init = {
	.name = "usb0",
	.ops = &ios_ops,
	.parent_names = std_clk_usb_parents,
	.num_parents = ARRAY_SIZE(std_clk_usb_parents),
};

static struct clk_std clk_usb0 = {
	.enable_bit = 16,
	.hw = {
		.init = &clk_usb0_init,
	},
};

static const struct clk_init_data clk_usb1_init = {
	.name = "usb1",
	.ops = &ios_ops,
	.parent_names = std_clk_usb_parents,
	.num_parents = ARRAY_SIZE(std_clk_usb_parents),
};

static struct clk_std clk_usb1 = {
	.enable_bit = 17,
	.hw = {
		.init = &clk_usb1_init,
	},
};
