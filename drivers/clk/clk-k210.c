// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2019-20 Sean Anderson <seanga2@gmail.com>
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 */
#define pr_fmt(fmt)     "k210-clk: " fmt

#include <linux/io.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_clk.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/clk-provider.h>
#include <linux/bitfield.h>
#include <linux/delay.h>
#include <soc/canaan/k210-sysctl.h>

#include <dt-bindings/clock/k210-clk.h>

struct k210_sysclk;

struct k210_clk {
	int id;
	struct k210_sysclk *ksc;
	struct clk_hw hw;
};

struct k210_clk_cfg {
	const char *name;
	u8 gate_reg;
	u8 gate_bit;
	u8 div_reg;
	u8 div_shift;
	u8 div_width;
	u8 div_type;
	u8 mux_reg;
	u8 mux_bit;
};

enum k210_clk_div_type {
	K210_DIV_NONE,
	K210_DIV_ONE_BASED,
	K210_DIV_DOUBLE_ONE_BASED,
	K210_DIV_POWER_OF_TWO,
};

#define K210_GATE(_reg, _bit)	\
	.gate_reg = (_reg),	\
	.gate_bit = (_bit)

#define K210_DIV(_reg, _shift, _width, _type)	\
	.div_reg = (_reg),			\
	.div_shift = (_shift),			\
	.div_width = (_width),			\
	.div_type = (_type)

#define K210_MUX(_reg, _bit)	\
	.mux_reg = (_reg),	\
	.mux_bit = (_bit)

static struct k210_clk_cfg k210_clk_cfgs[K210_NUM_CLKS] = {
	/* Gated clocks, no mux, no divider */
	[K210_CLK_CPU] = {
		.name = "cpu",
		K210_GATE(K210_SYSCTL_EN_CENT, 0)
	},
	[K210_CLK_DMA] = {
		.name = "dma",
		K210_GATE(K210_SYSCTL_EN_PERI, 1)
	},
	[K210_CLK_FFT] = {
		.name = "fft",
		K210_GATE(K210_SYSCTL_EN_PERI, 4)
	},
	[K210_CLK_GPIO] = {
		.name = "gpio",
		K210_GATE(K210_SYSCTL_EN_PERI, 5)
	},
	[K210_CLK_UART1] = {
		.name = "uart1",
		K210_GATE(K210_SYSCTL_EN_PERI, 16)
	},
	[K210_CLK_UART2] = {
		.name = "uart2",
		K210_GATE(K210_SYSCTL_EN_PERI, 17)
	},
	[K210_CLK_UART3] = {
		.name = "uart3",
		K210_GATE(K210_SYSCTL_EN_PERI, 18)
	},
	[K210_CLK_FPIOA] = {
		.name = "fpioa",
		K210_GATE(K210_SYSCTL_EN_PERI, 20)
	},
	[K210_CLK_SHA] = {
		.name = "sha",
		K210_GATE(K210_SYSCTL_EN_PERI, 26)
	},
	[K210_CLK_AES] = {
		.name = "aes",
		K210_GATE(K210_SYSCTL_EN_PERI, 19)
	},
	[K210_CLK_OTP] = {
		.name = "otp",
		K210_GATE(K210_SYSCTL_EN_PERI, 27)
	},
	[K210_CLK_RTC] = {
		.name = "rtc",
		K210_GATE(K210_SYSCTL_EN_PERI, 29)
	},

	/* Gated divider clocks */
	[K210_CLK_SRAM0] = {
		.name = "sram0",
		K210_GATE(K210_SYSCTL_EN_CENT, 1),
		K210_DIV(K210_SYSCTL_THR0, 0, 4, K210_DIV_ONE_BASED)
	},
	[K210_CLK_SRAM1] = {
		.name = "sram1",
		K210_GATE(K210_SYSCTL_EN_CENT, 2),
		K210_DIV(K210_SYSCTL_THR0, 4, 4, K210_DIV_ONE_BASED)
	},
	[K210_CLK_ROM] = {
		.name = "rom",
		K210_GATE(K210_SYSCTL_EN_PERI, 0),
		K210_DIV(K210_SYSCTL_THR0, 16, 4, K210_DIV_ONE_BASED)
	},
	[K210_CLK_DVP] = {
		.name = "dvp",
		K210_GATE(K210_SYSCTL_EN_PERI, 3),
		K210_DIV(K210_SYSCTL_THR0, 12, 4, K210_DIV_ONE_BASED)
	},
	[K210_CLK_APB0] = {
		.name = "apb0",
		K210_GATE(K210_SYSCTL_EN_CENT, 3),
		K210_DIV(K210_SYSCTL_SEL0, 3, 3, K210_DIV_ONE_BASED)
	},
	[K210_CLK_APB1] = {
		.name = "apb1",
		K210_GATE(K210_SYSCTL_EN_CENT, 4),
		K210_DIV(K210_SYSCTL_SEL0, 6, 3, K210_DIV_ONE_BASED)
	},
	[K210_CLK_APB2] = {
		.name = "apb2",
		K210_GATE(K210_SYSCTL_EN_CENT, 5),
		K210_DIV(K210_SYSCTL_SEL0, 9, 3, K210_DIV_ONE_BASED)
	},
	[K210_CLK_AI] = {
		.name = "ai",
		K210_GATE(K210_SYSCTL_EN_PERI, 2),
		K210_DIV(K210_SYSCTL_THR0, 8, 4, K210_DIV_ONE_BASED)
	},
	[K210_CLK_SPI0] = {
		.name = "spi0",
		K210_GATE(K210_SYSCTL_EN_PERI, 6),
		K210_DIV(K210_SYSCTL_THR1, 0, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_SPI1] = {
		.name = "spi1",
		K210_GATE(K210_SYSCTL_EN_PERI, 7),
		K210_DIV(K210_SYSCTL_THR1, 8, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_SPI2] = {
		.name = "spi2",
		K210_GATE(K210_SYSCTL_EN_PERI, 8),
		K210_DIV(K210_SYSCTL_THR1, 16, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2C0] = {
		.name = "i2c0",
		K210_GATE(K210_SYSCTL_EN_PERI, 13),
		K210_DIV(K210_SYSCTL_THR5, 8, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2C1] = {
		.name = "i2c1",
		K210_GATE(K210_SYSCTL_EN_PERI, 14),
		K210_DIV(K210_SYSCTL_THR5, 16, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2C2] = {
		.name = "i2c2",
		K210_GATE(K210_SYSCTL_EN_PERI, 15),
		K210_DIV(K210_SYSCTL_THR5, 24, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_WDT0] = {
		.name = "wdt0",
		K210_GATE(K210_SYSCTL_EN_PERI, 24),
		K210_DIV(K210_SYSCTL_THR6, 0, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_WDT1] = {
		.name = "wdt1",
		K210_GATE(K210_SYSCTL_EN_PERI, 25),
		K210_DIV(K210_SYSCTL_THR6, 8, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2S0] = {
		.name = "i2s0",
		K210_GATE(K210_SYSCTL_EN_PERI, 10),
		K210_DIV(K210_SYSCTL_THR3, 0, 16, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2S1] = {
		.name = "i2s1",
		K210_GATE(K210_SYSCTL_EN_PERI, 11),
		K210_DIV(K210_SYSCTL_THR3, 16, 16, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2S2] = {
		.name = "i2s2",
		K210_GATE(K210_SYSCTL_EN_PERI, 12),
		K210_DIV(K210_SYSCTL_THR4, 0, 16, K210_DIV_DOUBLE_ONE_BASED)
	},

	/* Divider clocks, no gate, no mux */
	[K210_CLK_I2S0_M] = {
		.name = "i2s0_m",
		K210_DIV(K210_SYSCTL_THR4, 16, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2S1_M] = {
		.name = "i2s1_m",
		K210_DIV(K210_SYSCTL_THR4, 24, 8, K210_DIV_DOUBLE_ONE_BASED)
	},
	[K210_CLK_I2S2_M] = {
		.name = "i2s2_m",
		K210_DIV(K210_SYSCTL_THR4, 0, 8, K210_DIV_DOUBLE_ONE_BASED)
	},

	/* Muxed gated divider clocks */
	[K210_CLK_SPI3] = {
		.name = "spi3",
		K210_GATE(K210_SYSCTL_EN_PERI, 9),
		K210_DIV(K210_SYSCTL_THR1, 24, 8, K210_DIV_DOUBLE_ONE_BASED),
		K210_MUX(K210_SYSCTL_SEL0, 12)
	},
	[K210_CLK_TIMER0] = {
		.name = "timer0",
		K210_GATE(K210_SYSCTL_EN_PERI, 21),
		K210_DIV(K210_SYSCTL_THR2,  0, 8, K210_DIV_DOUBLE_ONE_BASED),
		K210_MUX(K210_SYSCTL_SEL0, 13)
	},
	[K210_CLK_TIMER1] = {
		.name = "timer1",
		K210_GATE(K210_SYSCTL_EN_PERI, 22),
		K210_DIV(K210_SYSCTL_THR2, 8, 8, K210_DIV_DOUBLE_ONE_BASED),
		K210_MUX(K210_SYSCTL_SEL0, 14)
	},
	[K210_CLK_TIMER2] = {
		.name = "timer2",
		K210_GATE(K210_SYSCTL_EN_PERI, 23),
		K210_DIV(K210_SYSCTL_THR2, 16, 8, K210_DIV_DOUBLE_ONE_BASED),
		K210_MUX(K210_SYSCTL_SEL0, 15)
	},
};

/*
 * PLL control register bits.
 */
#define K210_PLL_CLKR		GENMASK(3, 0)
#define K210_PLL_CLKF		GENMASK(9, 4)
#define K210_PLL_CLKOD		GENMASK(13, 10)
#define K210_PLL_BWADJ		GENMASK(19, 14)
#define K210_PLL_RESET		(1 << 20)
#define K210_PLL_PWRD		(1 << 21)
#define K210_PLL_INTFB		(1 << 22)
#define K210_PLL_BYPASS		(1 << 23)
#define K210_PLL_TEST		(1 << 24)
#define K210_PLL_EN		(1 << 25)
#define K210_PLL_SEL		GENMASK(27, 26) /* PLL2 only */

/*
 * PLL lock register bits.
 */
#define K210_PLL_LOCK		0
#define K210_PLL_CLEAR_SLIP	2
#define K210_PLL_TEST_OUT	3

/*
 * Clock selector register bits.
 */
#define K210_ACLK_SEL		BIT(0)
#define K210_ACLK_DIV		GENMASK(2, 1)

/*
 * PLLs.
 */
enum k210_pll_id {
	K210_PLL0, K210_PLL1, K210_PLL2, K210_PLL_NUM
};

struct k210_pll {
	enum k210_pll_id id;
	struct k210_sysclk *ksc;
	void __iomem *base;
	void __iomem *reg;
	void __iomem *lock;
	u8 lock_shift;
	u8 lock_width;
	struct clk_hw hw;
};
#define to_k210_pll(_hw)	container_of(_hw, struct k210_pll, hw)

/*
 * PLLs configuration: by default PLL0 runs at 780 MHz and PLL1 at 299 MHz.
 * The first 2 SRAM banks depend on ACLK/CPU clock which is by default PLL0
 * rate divided by 2. Set PLL1 to 390 MHz so that the third SRAM bank has the
 * same clock as the first 2.
 */
struct k210_pll_cfg {
	u32 reg;
	u8 lock_shift;
	u8 lock_width;
	u32 r;
	u32 f;
	u32 od;
	u32 bwadj;
};

static struct k210_pll_cfg k210_plls_cfg[] = {
	{ K210_SYSCTL_PLL0,  0, 2, 0, 59, 1, 59 }, /* 780 MHz */
	{ K210_SYSCTL_PLL1,  8, 1, 0, 59, 3, 59 }, /* 390 MHz */
	{ K210_SYSCTL_PLL2, 16, 1, 0, 22, 1, 22 }, /* 299 MHz */
};

/**
 * struct k210_sysclk - sysclk driver data
 * @regs: system controller registers start address
 * @clk_lock: clock setting spinlock
 * @plls: SoC PLLs descriptors
 * @aclk: ACLK clock
 * @clks: All other clocks
 */
struct k210_sysclk {
	void __iomem			*regs;
	spinlock_t			clk_lock;
	struct k210_pll			plls[K210_PLL_NUM];
	struct clk_hw			aclk;
	struct k210_clk			clks[K210_NUM_CLKS];
};

#define to_k210_sysclk(_hw)	container_of(_hw, struct k210_sysclk, aclk)

/*
 * Set ACLK parent selector: 0 for IN0, 1 for PLL0.
 */
static void k210_aclk_set_selector(void __iomem *regs, u8 sel)
{
	u32 reg = readl(regs + K210_SYSCTL_SEL0);

	if (sel)
		reg |= K210_ACLK_SEL;
	else
		reg &= K210_ACLK_SEL;
	writel(reg, regs + K210_SYSCTL_SEL0);
}

static void k210_init_pll(void __iomem *regs, enum k210_pll_id pllid,
			  struct k210_pll *pll)
{
	pll->id = pllid;
	pll->reg = regs + k210_plls_cfg[pllid].reg;
	pll->lock = regs + K210_SYSCTL_PLL_LOCK;
	pll->lock_shift = k210_plls_cfg[pllid].lock_shift;
	pll->lock_width = k210_plls_cfg[pllid].lock_width;
}

static void k210_pll_wait_for_lock(struct k210_pll *pll)
{
	u32 reg, mask = GENMASK(pll->lock_shift + pll->lock_width - 1,
				pll->lock_shift);

	while (true) {
		reg = readl(pll->lock);
		if ((reg & mask) == mask)
			break;

		reg |= BIT(pll->lock_shift + K210_PLL_CLEAR_SLIP);
		writel(reg, pll->lock);
	}
}

static bool k210_pll_hw_is_enabled(struct k210_pll *pll)
{
	u32 reg = readl(pll->reg);
	u32 mask = K210_PLL_PWRD | K210_PLL_EN;

	if (reg & K210_PLL_RESET)
		return false;

	return (reg & mask) == mask;
}

static void k210_pll_enable_hw(void __iomem *regs, struct k210_pll *pll)
{
	struct k210_pll_cfg *pll_cfg = &k210_plls_cfg[pll->id];
	u32 reg;

	if (k210_pll_hw_is_enabled(pll))
		return;

	/*
	 * For PLL0, we need to re-parent ACLK to IN0 to keep the CPU cores and
	 * SRAM running.
	 */
	if (pll->id == K210_PLL0)
		k210_aclk_set_selector(regs, 0);

	/* Set PLL factors */
	reg = readl(pll->reg);
	reg &= ~GENMASK(19, 0);
	reg |= FIELD_PREP(K210_PLL_CLKR, pll_cfg->r);
	reg |= FIELD_PREP(K210_PLL_CLKF, pll_cfg->f);
	reg |= FIELD_PREP(K210_PLL_CLKOD, pll_cfg->od);
	reg |= FIELD_PREP(K210_PLL_BWADJ, pll_cfg->bwadj);
	reg |= K210_PLL_PWRD;
	writel(reg, pll->reg);

	/*
	 * Reset the PLL: ensure reset is low before asserting it.
	 * The magic NOPs come from the Kendryte reference SDK.
	 */
	reg &= ~K210_PLL_RESET;
	writel(reg, pll->reg);
	reg |= K210_PLL_RESET;
	writel(reg, pll->reg);
	nop();
	nop();
	reg &= ~K210_PLL_RESET;
	writel(reg, pll->reg);

	k210_pll_wait_for_lock(pll);

	reg &= ~K210_PLL_BYPASS;
	reg |= K210_PLL_EN;
	writel(reg, pll->reg);

	if (pll->id == K210_PLL0)
		k210_aclk_set_selector(regs, 1);
}

static int k210_pll_enable(struct clk_hw *hw)
{
	struct k210_pll *pll = to_k210_pll(hw);
	struct k210_sysclk *ksc = pll->ksc;
	unsigned long flags;

	spin_lock_irqsave(&ksc->clk_lock, flags);

	k210_pll_enable_hw(ksc->regs, pll);

	spin_unlock_irqrestore(&ksc->clk_lock, flags);

	return 0;
}

static void k210_pll_disable(struct clk_hw *hw)
{
	struct k210_pll *pll = to_k210_pll(hw);
	struct k210_sysclk *ksc = pll->ksc;
	unsigned long flags;
	u32 reg;

	/*
	 * Bypassing before powering off is important so child clocks do not
	 * stop working. This is especially important for pll0, the indirect
	 * parent of the cpu clock.
	 */
	spin_lock_irqsave(&ksc->clk_lock, flags);
	reg = readl(pll->reg);
	reg |= K210_PLL_BYPASS;
	writel(reg, pll->reg);

	reg &= ~K210_PLL_PWRD;
	reg &= ~K210_PLL_EN;
	writel(reg, pll->reg);
	spin_unlock_irqrestore(&ksc->clk_lock, flags);
}

static int k210_pll_is_enabled(struct clk_hw *hw)
{
	return k210_pll_hw_is_enabled(to_k210_pll(hw));
}

static unsigned long k210_pll_get_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	struct k210_pll *pll = to_k210_pll(hw);
	u32 reg = readl(pll->reg);
	u32 r, f, od;

	if (reg & K210_PLL_BYPASS)
		return parent_rate;

	if (!(reg & K210_PLL_PWRD))
		return 0;

	r = FIELD_GET(K210_PLL_CLKR, reg) + 1;
	f = FIELD_GET(K210_PLL_CLKF, reg) + 1;
	od = FIELD_GET(K210_PLL_CLKOD, reg) + 1;

	return div_u64((u64)parent_rate * f, r * od);
}

static const struct clk_ops k210_pll_ops = {
	.enable		= k210_pll_enable,
	.disable	= k210_pll_disable,
	.is_enabled	= k210_pll_is_enabled,
	.recalc_rate	= k210_pll_get_rate,
};

static int k210_pll2_set_parent(struct clk_hw *hw, u8 index)
{
	struct k210_pll *pll = to_k210_pll(hw);
	struct k210_sysclk *ksc = pll->ksc;
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&ksc->clk_lock, flags);

	reg = readl(pll->reg);
	reg &= ~K210_PLL_SEL;
	reg |= FIELD_PREP(K210_PLL_SEL, index);
	writel(reg, pll->reg);

	spin_unlock_irqrestore(&ksc->clk_lock, flags);

	return 0;
}

static u8 k210_pll2_get_parent(struct clk_hw *hw)
{
	struct k210_pll *pll = to_k210_pll(hw);
	u32 reg = readl(pll->reg);

	return FIELD_GET(K210_PLL_SEL, reg);
}

static const struct clk_ops k210_pll2_ops = {
	.enable		= k210_pll_enable,
	.disable	= k210_pll_disable,
	.is_enabled	= k210_pll_is_enabled,
	.recalc_rate	= k210_pll_get_rate,
	.set_parent	= k210_pll2_set_parent,
	.get_parent	= k210_pll2_get_parent,
};

static int __init k210_register_pll(struct device_node *np,
				    struct k210_sysclk *ksc,
				    enum k210_pll_id pllid, const char *name,
				    int num_parents, const struct clk_ops *ops)
{
	struct k210_pll *pll = &ksc->plls[pllid];
	struct clk_init_data init = {};
	const struct clk_parent_data parent_data[] = {
		{ /* .index = 0 for in0 */ },
		{ .hw = &ksc->plls[K210_PLL0].hw },
		{ .hw = &ksc->plls[K210_PLL1].hw },
	};

	init.name = name;
	init.parent_data = parent_data;
	init.num_parents = num_parents;
	init.ops = ops;

	pll->hw.init = &init;
	pll->ksc = ksc;

	return of_clk_hw_register(np, &pll->hw);
}

static int __init k210_register_plls(struct device_node *np,
				     struct k210_sysclk *ksc)
{
	int i, ret;

	for (i = 0; i < K210_PLL_NUM; i++)
		k210_init_pll(ksc->regs, i, &ksc->plls[i]);

	/* PLL0 and PLL1 only have IN0 as parent */
	ret = k210_register_pll(np, ksc, K210_PLL0, "pll0", 1, &k210_pll_ops);
	if (ret) {
		pr_err("%pOFP: register PLL0 failed\n", np);
		return ret;
	}
	ret = k210_register_pll(np, ksc, K210_PLL1, "pll1", 1, &k210_pll_ops);
	if (ret) {
		pr_err("%pOFP: register PLL1 failed\n", np);
		return ret;
	}

	/* PLL2 has IN0, PLL0 and PLL1 as parents */
	ret = k210_register_pll(np, ksc, K210_PLL2, "pll2", 3, &k210_pll2_ops);
	if (ret) {
		pr_err("%pOFP: register PLL2 failed\n", np);
		return ret;
	}

	return 0;
}

static int k210_aclk_set_parent(struct clk_hw *hw, u8 index)
{
	struct k210_sysclk *ksc = to_k210_sysclk(hw);
	unsigned long flags;

	spin_lock_irqsave(&ksc->clk_lock, flags);

	k210_aclk_set_selector(ksc->regs, index);

	spin_unlock_irqrestore(&ksc->clk_lock, flags);

	return 0;
}

static u8 k210_aclk_get_parent(struct clk_hw *hw)
{
	struct k210_sysclk *ksc = to_k210_sysclk(hw);
	u32 sel;

	sel = readl(ksc->regs + K210_SYSCTL_SEL0) & K210_ACLK_SEL;

	return sel ? 1 : 0;
}

static unsigned long k210_aclk_get_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct k210_sysclk *ksc = to_k210_sysclk(hw);
	u32 reg = readl(ksc->regs + K210_SYSCTL_SEL0);
	unsigned int shift;

	if (!(reg & 0x1))
		return parent_rate;

	shift = FIELD_GET(K210_ACLK_DIV, reg);

	return parent_rate / (2UL << shift);
}

static const struct clk_ops k210_aclk_ops = {
	.set_parent	= k210_aclk_set_parent,
	.get_parent	= k210_aclk_get_parent,
	.recalc_rate	= k210_aclk_get_rate,
};

/*
 * ACLK has IN0 and PLL0 as parents.
 */
static int __init k210_register_aclk(struct device_node *np,
				     struct k210_sysclk *ksc)
{
	struct clk_init_data init = {};
	const struct clk_parent_data parent_data[] = {
		{ /* .index = 0 for in0 */ },
		{ .hw = &ksc->plls[K210_PLL0].hw },
	};
	int ret;

	init.name = "aclk";
	init.parent_data = parent_data;
	init.num_parents = 2;
	init.ops = &k210_aclk_ops;
	ksc->aclk.init = &init;

	ret = of_clk_hw_register(np, &ksc->aclk);
	if (ret) {
		pr_err("%pOFP: register aclk failed\n", np);
		return ret;
	}

	return 0;
}

#define to_k210_clk(_hw)	container_of(_hw, struct k210_clk, hw)

static int k210_clk_enable(struct clk_hw *hw)
{
	struct k210_clk *kclk = to_k210_clk(hw);
	struct k210_sysclk *ksc = kclk->ksc;
	struct k210_clk_cfg *cfg = &k210_clk_cfgs[kclk->id];
	unsigned long flags;
	u32 reg;

	if (!cfg->gate_reg)
		return 0;

	spin_lock_irqsave(&ksc->clk_lock, flags);
	reg = readl(ksc->regs + cfg->gate_reg);
	reg |= BIT(cfg->gate_bit);
	writel(reg, ksc->regs + cfg->gate_reg);
	spin_unlock_irqrestore(&ksc->clk_lock, flags);

	return 0;
}

static void k210_clk_disable(struct clk_hw *hw)
{
	struct k210_clk *kclk = to_k210_clk(hw);
	struct k210_sysclk *ksc = kclk->ksc;
	struct k210_clk_cfg *cfg = &k210_clk_cfgs[kclk->id];
	unsigned long flags;
	u32 reg;

	if (!cfg->gate_reg)
		return;

	spin_lock_irqsave(&ksc->clk_lock, flags);
	reg = readl(ksc->regs + cfg->gate_reg);
	reg &= ~BIT(cfg->gate_bit);
	writel(reg, ksc->regs + cfg->gate_reg);
	spin_unlock_irqrestore(&ksc->clk_lock, flags);
}

static int k210_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct k210_clk *kclk = to_k210_clk(hw);
	struct k210_sysclk *ksc = kclk->ksc;
	struct k210_clk_cfg *cfg = &k210_clk_cfgs[kclk->id];
	unsigned long flags;
	u32 reg;

	spin_lock_irqsave(&ksc->clk_lock, flags);
	reg = readl(ksc->regs + cfg->mux_reg);
	if (index)
		reg |= BIT(cfg->mux_bit);
	else
		reg &= ~BIT(cfg->mux_bit);
	writel(reg, ksc->regs + cfg->mux_reg);
	spin_unlock_irqrestore(&ksc->clk_lock, flags);

	return 0;
}

static u8 k210_clk_get_parent(struct clk_hw *hw)
{
	struct k210_clk *kclk = to_k210_clk(hw);
	struct k210_sysclk *ksc = kclk->ksc;
	struct k210_clk_cfg *cfg = &k210_clk_cfgs[kclk->id];
	unsigned long flags;
	u32 reg, idx;

	spin_lock_irqsave(&ksc->clk_lock, flags);
	reg = readl(ksc->regs + cfg->mux_reg);
	idx = (reg & BIT(cfg->mux_bit)) ? 1 : 0;
	spin_unlock_irqrestore(&ksc->clk_lock, flags);

	return idx;
}

static unsigned long k210_clk_get_rate(struct clk_hw *hw,
				       unsigned long parent_rate)
{
	struct k210_clk *kclk = to_k210_clk(hw);
	struct k210_sysclk *ksc = kclk->ksc;
	struct k210_clk_cfg *cfg = &k210_clk_cfgs[kclk->id];
	u32 reg, div_val;

	if (!cfg->div_reg)
		return parent_rate;

	reg = readl(ksc->regs + cfg->div_reg);
	div_val = (reg >> cfg->div_shift) & GENMASK(cfg->div_width - 1, 0);

	switch (cfg->div_type) {
	case K210_DIV_ONE_BASED:
		return parent_rate / (div_val + 1);
	case K210_DIV_DOUBLE_ONE_BASED:
		return parent_rate / ((div_val + 1) * 2);
	case K210_DIV_POWER_OF_TWO:
		return parent_rate / (2UL << div_val);
	case K210_DIV_NONE:
	default:
		return 0;
	}
}

static const struct clk_ops k210_clk_mux_ops = {
	.enable		= k210_clk_enable,
	.disable	= k210_clk_disable,
	.set_parent	= k210_clk_set_parent,
	.get_parent	= k210_clk_get_parent,
	.recalc_rate	= k210_clk_get_rate,
};

static const struct clk_ops k210_clk_ops = {
	.enable		= k210_clk_enable,
	.disable	= k210_clk_disable,
	.recalc_rate	= k210_clk_get_rate,
};

static void __init k210_register_clk(struct device_node *np,
				     struct k210_sysclk *ksc, int id,
				     const struct clk_parent_data *parent_data,
				     int num_parents, unsigned long flags)
{
	struct k210_clk *kclk = &ksc->clks[id];
	struct clk_init_data init = {};
	int ret;

	init.name = k210_clk_cfgs[id].name;
	init.flags = flags;
	init.parent_data = parent_data;
	init.num_parents = num_parents;
	if (num_parents > 1)
		init.ops = &k210_clk_mux_ops;
	else
		init.ops = &k210_clk_ops;

	kclk->id = id;
	kclk->ksc = ksc;
	kclk->hw.init = &init;

	ret = of_clk_hw_register(np, &kclk->hw);
	if (ret) {
		pr_err("%pOFP: register clock %s failed\n",
		       np, k210_clk_cfgs[id].name);
		kclk->id = -1;
	}
}

/*
 * All muxed clocks have IN0 and PLL0 as parents.
 */
static inline void __init k210_register_mux_clk(struct device_node *np,
						struct k210_sysclk *ksc, int id)
{
	const struct clk_parent_data parent_data[2] = {
		{ /* .index = 0 for in0 */ },
		{ .hw = &ksc->plls[K210_PLL0].hw }
	};

	k210_register_clk(np, ksc, id, parent_data, 2, 0);
}

static inline void __init k210_register_in0_child(struct device_node *np,
						struct k210_sysclk *ksc, int id)
{
	const struct clk_parent_data parent_data = {
		/* .index = 0 for in0 */
	};

	k210_register_clk(np, ksc, id, &parent_data, 1, 0);
}

static inline void __init k210_register_pll_child(struct device_node *np,
						struct k210_sysclk *ksc, int id,
						enum k210_pll_id pllid,
						unsigned long flags)
{
	const struct clk_parent_data parent_data = {
		.hw = &ksc->plls[pllid].hw,
	};

	k210_register_clk(np, ksc, id, &parent_data, 1, flags);
}

static inline void __init k210_register_aclk_child(struct device_node *np,
						struct k210_sysclk *ksc, int id,
						unsigned long flags)
{
	const struct clk_parent_data parent_data = {
		.hw = &ksc->aclk,
	};

	k210_register_clk(np, ksc, id, &parent_data, 1, flags);
}

static inline void __init k210_register_clk_child(struct device_node *np,
						struct k210_sysclk *ksc, int id,
						int parent_id)
{
	const struct clk_parent_data parent_data = {
		.hw = &ksc->clks[parent_id].hw,
	};

	k210_register_clk(np, ksc, id, &parent_data, 1, 0);
}

static struct clk_hw *k210_clk_hw_onecell_get(struct of_phandle_args *clkspec,
					      void *data)
{
	struct k210_sysclk *ksc = data;
	unsigned int idx = clkspec->args[0];

	if (idx >= K210_NUM_CLKS)
		return ERR_PTR(-EINVAL);

	return &ksc->clks[idx].hw;
}

static void __init k210_clk_init(struct device_node *np)
{
	struct device_node *sysctl_np;
	struct k210_sysclk *ksc;
	int i, ret;

	ksc = kzalloc(sizeof(*ksc), GFP_KERNEL);
	if (!ksc)
		return;

	spin_lock_init(&ksc->clk_lock);
	sysctl_np = of_get_parent(np);
	ksc->regs = of_iomap(sysctl_np, 0);
	of_node_put(sysctl_np);
	if (!ksc->regs) {
		pr_err("%pOFP: failed to map registers\n", np);
		return;
	}

	ret = k210_register_plls(np, ksc);
	if (ret)
		return;

	ret = k210_register_aclk(np, ksc);
	if (ret)
		return;

	/*
	 * Critical clocks: there are no consumers of the SRAM clocks,
	 * including the AI clock for the third SRAM bank. The CPU clock
	 * is only referenced by the uarths serial device and so would be
	 * disabled if the serial console is disabled to switch to another
	 * console. Mark all these clocks as critical so that they are never
	 * disabled by the core clock management.
	 */
	k210_register_aclk_child(np, ksc, K210_CLK_CPU, CLK_IS_CRITICAL);
	k210_register_aclk_child(np, ksc, K210_CLK_SRAM0, CLK_IS_CRITICAL);
	k210_register_aclk_child(np, ksc, K210_CLK_SRAM1, CLK_IS_CRITICAL);
	k210_register_pll_child(np, ksc, K210_CLK_AI, K210_PLL1,
				CLK_IS_CRITICAL);

	/* Clocks with aclk as source */
	k210_register_aclk_child(np, ksc, K210_CLK_DMA, 0);
	k210_register_aclk_child(np, ksc, K210_CLK_FFT, 0);
	k210_register_aclk_child(np, ksc, K210_CLK_ROM, 0);
	k210_register_aclk_child(np, ksc, K210_CLK_DVP, 0);
	k210_register_aclk_child(np, ksc, K210_CLK_APB0, 0);
	k210_register_aclk_child(np, ksc, K210_CLK_APB1, 0);
	k210_register_aclk_child(np, ksc, K210_CLK_APB2, 0);

	/* Clocks with PLL0 as source */
	k210_register_pll_child(np, ksc, K210_CLK_SPI0, K210_PLL0, 0);
	k210_register_pll_child(np, ksc, K210_CLK_SPI1, K210_PLL0, 0);
	k210_register_pll_child(np, ksc, K210_CLK_SPI2, K210_PLL0, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2C0, K210_PLL0, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2C1, K210_PLL0, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2C2, K210_PLL0, 0);

	/* Clocks with PLL2 as source */
	k210_register_pll_child(np, ksc, K210_CLK_I2S0, K210_PLL2, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2S1, K210_PLL2, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2S2, K210_PLL2, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2S0_M, K210_PLL2, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2S1_M, K210_PLL2, 0);
	k210_register_pll_child(np, ksc, K210_CLK_I2S2_M, K210_PLL2, 0);

	/* Clocks with IN0 as source */
	k210_register_in0_child(np, ksc, K210_CLK_WDT0);
	k210_register_in0_child(np, ksc, K210_CLK_WDT1);
	k210_register_in0_child(np, ksc, K210_CLK_RTC);

	/* Clocks with APB0 as source */
	k210_register_clk_child(np, ksc, K210_CLK_GPIO, K210_CLK_APB0);
	k210_register_clk_child(np, ksc, K210_CLK_UART1, K210_CLK_APB0);
	k210_register_clk_child(np, ksc, K210_CLK_UART2, K210_CLK_APB0);
	k210_register_clk_child(np, ksc, K210_CLK_UART3, K210_CLK_APB0);
	k210_register_clk_child(np, ksc, K210_CLK_FPIOA, K210_CLK_APB0);
	k210_register_clk_child(np, ksc, K210_CLK_SHA, K210_CLK_APB0);

	/* Clocks with APB1 as source */
	k210_register_clk_child(np, ksc, K210_CLK_AES, K210_CLK_APB1);
	k210_register_clk_child(np, ksc, K210_CLK_OTP, K210_CLK_APB1);

	/* Mux clocks with in0 or pll0 as source */
	k210_register_mux_clk(np, ksc, K210_CLK_SPI3);
	k210_register_mux_clk(np, ksc, K210_CLK_TIMER0);
	k210_register_mux_clk(np, ksc, K210_CLK_TIMER1);
	k210_register_mux_clk(np, ksc, K210_CLK_TIMER2);

	/* Check for registration errors */
	for (i = 0; i < K210_NUM_CLKS; i++) {
		if (ksc->clks[i].id != i)
			return;
	}

	ret = of_clk_add_hw_provider(np, k210_clk_hw_onecell_get, ksc);
	if (ret) {
		pr_err("%pOFP: add clock provider failed %d\n", np, ret);
		return;
	}

	pr_info("%pOFP: CPU running at %lu MHz\n",
		np, clk_hw_get_rate(&ksc->clks[K210_CLK_CPU].hw) / 1000000);
}

CLK_OF_DECLARE(k210_clk, "canaan,k210-clk", k210_clk_init);

/*
 * Enable PLL1 to be able to use the AI SRAM.
 */
void __init k210_clk_early_init(void __iomem *regs)
{
	struct k210_pll pll1;

	/* Make sure ACLK selector is set to PLL0 */
	k210_aclk_set_selector(regs, 1);

	/* Startup PLL1 to enable the aisram bank for general memory use */
	k210_init_pll(regs, K210_PLL1, &pll1);
	k210_pll_enable_hw(regs, &pll1);
}
