/*
 * arch/arm/mach-tegra/tegra20_clocks.c
 *
 * Copyright (C) 2010 Google, Inc.
 * Copyright (c) 2010-2012 NVIDIA CORPORATION.  All rights reserved.
 *
 * Author:
 *	Colin Cross <ccross@google.com>
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/spinlock.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <linux/clk.h>

#include <mach/iomap.h>

#include "clock.h"
#include "fuse.h"
#include "tegra2_emc.h"

#define RST_DEVICES			0x004
#define RST_DEVICES_SET			0x300
#define RST_DEVICES_CLR			0x304
#define RST_DEVICES_NUM			3

#define CLK_OUT_ENB			0x010
#define CLK_OUT_ENB_SET			0x320
#define CLK_OUT_ENB_CLR			0x324
#define CLK_OUT_ENB_NUM			3

#define CLK_MASK_ARM			0x44
#define MISC_CLK_ENB			0x48

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_MASK		(3<<30)
#define OSC_CTRL_OSC_FREQ_13MHZ		(0<<30)
#define OSC_CTRL_OSC_FREQ_19_2MHZ	(1<<30)
#define OSC_CTRL_OSC_FREQ_12MHZ		(2<<30)
#define OSC_CTRL_OSC_FREQ_26MHZ		(3<<30)
#define OSC_CTRL_MASK			(0x3f2 | OSC_CTRL_OSC_FREQ_MASK)

#define OSC_FREQ_DET			0x58
#define OSC_FREQ_DET_TRIG		(1<<31)

#define OSC_FREQ_DET_STATUS		0x5C
#define OSC_FREQ_DET_BUSY		(1<<31)
#define OSC_FREQ_DET_CNT_MASK		0xFFFF

#define PERIPH_CLK_SOURCE_I2S1		0x100
#define PERIPH_CLK_SOURCE_EMC		0x19c
#define PERIPH_CLK_SOURCE_OSC		0x1fc
#define PERIPH_CLK_SOURCE_NUM \
	((PERIPH_CLK_SOURCE_OSC - PERIPH_CLK_SOURCE_I2S1) / 4)

#define PERIPH_CLK_SOURCE_MASK		(3<<30)
#define PERIPH_CLK_SOURCE_SHIFT		30
#define PERIPH_CLK_SOURCE_PWM_MASK	(7<<28)
#define PERIPH_CLK_SOURCE_PWM_SHIFT	28
#define PERIPH_CLK_SOURCE_ENABLE	(1<<28)
#define PERIPH_CLK_SOURCE_DIVU71_MASK	0xFF
#define PERIPH_CLK_SOURCE_DIVU16_MASK	0xFFFF
#define PERIPH_CLK_SOURCE_DIV_SHIFT	0

#define SDMMC_CLK_INT_FB_SEL		(1 << 23)
#define SDMMC_CLK_INT_FB_DLY_SHIFT	16
#define SDMMC_CLK_INT_FB_DLY_MASK	(0xF << SDMMC_CLK_INT_FB_DLY_SHIFT)

#define PLL_BASE			0x0
#define PLL_BASE_BYPASS			(1<<31)
#define PLL_BASE_ENABLE			(1<<30)
#define PLL_BASE_REF_ENABLE		(1<<29)
#define PLL_BASE_OVERRIDE		(1<<28)
#define PLL_BASE_DIVP_MASK		(0x7<<20)
#define PLL_BASE_DIVP_SHIFT		20
#define PLL_BASE_DIVN_MASK		(0x3FF<<8)
#define PLL_BASE_DIVN_SHIFT		8
#define PLL_BASE_DIVM_MASK		(0x1F)
#define PLL_BASE_DIVM_SHIFT		0

#define PLL_OUT_RATIO_MASK		(0xFF<<8)
#define PLL_OUT_RATIO_SHIFT		8
#define PLL_OUT_OVERRIDE		(1<<2)
#define PLL_OUT_CLKEN			(1<<1)
#define PLL_OUT_RESET_DISABLE		(1<<0)

#define PLL_MISC(c) (((c)->flags & PLL_ALT_MISC_REG) ? 0x4 : 0xc)

#define PLL_MISC_DCCON_SHIFT		20
#define PLL_MISC_CPCON_SHIFT		8
#define PLL_MISC_CPCON_MASK		(0xF<<PLL_MISC_CPCON_SHIFT)
#define PLL_MISC_LFCON_SHIFT		4
#define PLL_MISC_LFCON_MASK		(0xF<<PLL_MISC_LFCON_SHIFT)
#define PLL_MISC_VCOCON_SHIFT		0
#define PLL_MISC_VCOCON_MASK		(0xF<<PLL_MISC_VCOCON_SHIFT)

#define PLLU_BASE_POST_DIV		(1<<20)

#define PLLD_MISC_CLKENABLE		(1<<30)
#define PLLD_MISC_DIV_RST		(1<<23)
#define PLLD_MISC_DCCON_SHIFT		12

#define PLLE_MISC_READY			(1 << 15)

#define PERIPH_CLK_TO_ENB_REG(c)	((c->u.periph.clk_num / 32) * 4)
#define PERIPH_CLK_TO_ENB_SET_REG(c)	((c->u.periph.clk_num / 32) * 8)
#define PERIPH_CLK_TO_ENB_BIT(c)	(1 << (c->u.periph.clk_num % 32))

#define SUPER_CLK_MUX			0x00
#define SUPER_STATE_SHIFT		28
#define SUPER_STATE_MASK		(0xF << SUPER_STATE_SHIFT)
#define SUPER_STATE_STANDBY		(0x0 << SUPER_STATE_SHIFT)
#define SUPER_STATE_IDLE		(0x1 << SUPER_STATE_SHIFT)
#define SUPER_STATE_RUN			(0x2 << SUPER_STATE_SHIFT)
#define SUPER_STATE_IRQ			(0x3 << SUPER_STATE_SHIFT)
#define SUPER_STATE_FIQ			(0x4 << SUPER_STATE_SHIFT)
#define SUPER_SOURCE_MASK		0xF
#define	SUPER_FIQ_SOURCE_SHIFT		12
#define	SUPER_IRQ_SOURCE_SHIFT		8
#define	SUPER_RUN_SOURCE_SHIFT		4
#define	SUPER_IDLE_SOURCE_SHIFT		0

#define SUPER_CLK_DIVIDER		0x04

#define BUS_CLK_DISABLE			(1<<3)
#define BUS_CLK_DIV_MASK		0x3

#define PMC_CTRL			0x0
 #define PMC_CTRL_BLINK_ENB		(1 << 7)

#define PMC_DPD_PADS_ORIDE		0x1c
 #define PMC_DPD_PADS_ORIDE_BLINK_ENB	(1 << 20)

#define PMC_BLINK_TIMER_DATA_ON_SHIFT	0
#define PMC_BLINK_TIMER_DATA_ON_MASK	0x7fff
#define PMC_BLINK_TIMER_ENB		(1 << 15)
#define PMC_BLINK_TIMER_DATA_OFF_SHIFT	16
#define PMC_BLINK_TIMER_DATA_OFF_MASK	0xffff

static void __iomem *reg_clk_base = IO_ADDRESS(TEGRA_CLK_RESET_BASE);
static void __iomem *reg_pmc_base = IO_ADDRESS(TEGRA_PMC_BASE);

/*
 * Some clocks share a register with other clocks.  Any clock op that
 * non-atomically modifies a register used by another clock must lock
 * clock_register_lock first.
 */
static DEFINE_SPINLOCK(clock_register_lock);

/*
 * Some peripheral clocks share an enable bit, so refcount the enable bits
 * in registers CLK_ENABLE_L, CLK_ENABLE_H, and CLK_ENABLE_U
 */
static int tegra_periph_clk_enable_refcount[3 * 32];

#define clk_writel(value, reg) \
	__raw_writel(value, reg_clk_base + (reg))
#define clk_readl(reg) \
	__raw_readl(reg_clk_base + (reg))
#define pmc_writel(value, reg) \
	__raw_writel(value, reg_pmc_base + (reg))
#define pmc_readl(reg) \
	__raw_readl(reg_pmc_base + (reg))

static unsigned long clk_measure_input_freq(void)
{
	u32 clock_autodetect;
	clk_writel(OSC_FREQ_DET_TRIG | 1, OSC_FREQ_DET);
	do {} while (clk_readl(OSC_FREQ_DET_STATUS) & OSC_FREQ_DET_BUSY);
	clock_autodetect = clk_readl(OSC_FREQ_DET_STATUS);
	if (clock_autodetect >= 732 - 3 && clock_autodetect <= 732 + 3) {
		return 12000000;
	} else if (clock_autodetect >= 794 - 3 && clock_autodetect <= 794 + 3) {
		return 13000000;
	} else if (clock_autodetect >= 1172 - 3 && clock_autodetect <= 1172 + 3) {
		return 19200000;
	} else if (clock_autodetect >= 1587 - 3 && clock_autodetect <= 1587 + 3) {
		return 26000000;
	} else {
		pr_err("%s: Unexpected clock autodetect value %d",
						__func__, clock_autodetect);
		BUG();
		return 0;
	}
}

static int clk_div71_get_divider(unsigned long parent_rate, unsigned long rate)
{
	s64 divider_u71 = parent_rate * 2;
	divider_u71 += rate - 1;
	do_div(divider_u71, rate);

	if (divider_u71 - 2 < 0)
		return 0;

	if (divider_u71 - 2 > 255)
		return -EINVAL;

	return divider_u71 - 2;
}

static int clk_div16_get_divider(unsigned long parent_rate, unsigned long rate)
{
	s64 divider_u16;

	divider_u16 = parent_rate;
	divider_u16 += rate - 1;
	do_div(divider_u16, rate);

	if (divider_u16 - 1 < 0)
		return 0;

	if (divider_u16 - 1 > 0xFFFF)
		return -EINVAL;

	return divider_u16 - 1;
}

static unsigned long tegra_clk_fixed_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	return to_clk_tegra(hw)->fixed_rate;
}

struct clk_ops tegra_clk_32k_ops = {
	.recalc_rate = tegra_clk_fixed_recalc_rate,
};

/* clk_m functions */
static unsigned long tegra20_clk_m_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	if (!to_clk_tegra(hw)->fixed_rate)
		to_clk_tegra(hw)->fixed_rate = clk_measure_input_freq();
	return to_clk_tegra(hw)->fixed_rate;
}

static void tegra20_clk_m_init(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 osc_ctrl = clk_readl(OSC_CTRL);
	u32 auto_clock_control = osc_ctrl & ~OSC_CTRL_OSC_FREQ_MASK;

	switch (c->fixed_rate) {
	case 12000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_12MHZ;
		break;
	case 13000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_13MHZ;
		break;
	case 19200000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_19_2MHZ;
		break;
	case 26000000:
		auto_clock_control |= OSC_CTRL_OSC_FREQ_26MHZ;
		break;
	default:
		BUG();
	}
	clk_writel(auto_clock_control, OSC_CTRL);
}

struct clk_ops tegra_clk_m_ops = {
	.init = tegra20_clk_m_init,
	.recalc_rate = tegra20_clk_m_recalc_rate,
};

/* super clock functions */
/* "super clocks" on tegra have two-stage muxes and a clock skipping
 * super divider.  We will ignore the clock skipping divider, since we
 * can't lower the voltage when using the clock skip, but we can if we
 * lower the PLL frequency.
 */
static int tegra20_super_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;

	val = clk_readl(c->reg + SUPER_CLK_MUX);
	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	c->state = ON;
	return c->state;
}

static int tegra20_super_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	clk_writel(0, c->reg + SUPER_CLK_DIVIDER);
	return 0;
}

static void tegra20_super_clk_disable(struct clk_hw *hw)
{
	pr_debug("%s on clock %s\n", __func__, __clk_get_name(hw->clk));

	/* oops - don't disable the CPU clock! */
	BUG();
}

static u8 tegra20_super_clk_get_parent(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	int val = clk_readl(c->reg + SUPER_CLK_MUX);
	int source;
	int shift;

	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	source = (val >> shift) & SUPER_SOURCE_MASK;
	return source;
}

static int tegra20_super_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg + SUPER_CLK_MUX);
	int shift;

	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	val &= ~(SUPER_SOURCE_MASK << shift);
	val |= index << shift;

	clk_writel(val, c->reg);

	return 0;
}

/* FIX ME: Need to switch parents to change the source PLL rate */
static unsigned long tegra20_super_clk_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	return prate;
}

static long tegra20_super_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	return *prate;
}

static int tegra20_super_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	return 0;
}

struct clk_ops tegra_super_ops = {
	.is_enabled = tegra20_super_clk_is_enabled,
	.enable = tegra20_super_clk_enable,
	.disable = tegra20_super_clk_disable,
	.set_parent = tegra20_super_clk_set_parent,
	.get_parent = tegra20_super_clk_get_parent,
	.set_rate = tegra20_super_clk_set_rate,
	.round_rate = tegra20_super_clk_round_rate,
	.recalc_rate = tegra20_super_clk_recalc_rate,
};

static unsigned long tegra20_twd_clk_recalc_rate(struct clk_hw *hw,
		unsigned long parent_rate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u64 rate = parent_rate;

	if (c->mul != 0 && c->div != 0) {
		rate *= c->mul;
		rate += c->div - 1; /* round up */
		do_div(rate, c->div);
	}

	return rate;
}

struct clk_ops tegra_twd_ops = {
	.recalc_rate = tegra20_twd_clk_recalc_rate,
};

static u8 tegra20_cop_clk_get_parent(struct clk_hw *hw)
{
	return 0;
}

struct clk_ops tegra_cop_ops = {
	.get_parent = tegra20_cop_clk_get_parent,
};

/* virtual cop clock functions. Used to acquire the fake 'cop' clock to
 * reset the COP block (i.e. AVP) */
void tegra2_cop_clk_reset(struct clk_hw *hw, bool assert)
{
	unsigned long reg = assert ? RST_DEVICES_SET : RST_DEVICES_CLR;

	pr_debug("%s %s\n", __func__, assert ? "assert" : "deassert");
	clk_writel(1 << 1, reg);
}

/* bus clock functions */
static int tegra20_bus_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg);

	c->state = ((val >> c->reg_shift) & BUS_CLK_DISABLE) ? OFF : ON;
	return c->state;
}

static int tegra20_bus_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&clock_register_lock, flags);

	val = clk_readl(c->reg);
	val &= ~(BUS_CLK_DISABLE << c->reg_shift);
	clk_writel(val, c->reg);

	spin_unlock_irqrestore(&clock_register_lock, flags);

	return 0;
}

static void tegra20_bus_clk_disable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;
	u32 val;

	spin_lock_irqsave(&clock_register_lock, flags);

	val = clk_readl(c->reg);
	val |= BUS_CLK_DISABLE << c->reg_shift;
	clk_writel(val, c->reg);

	spin_unlock_irqrestore(&clock_register_lock, flags);
}

static unsigned long tegra20_bus_clk_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg);
	u64 rate = prate;

	c->div = ((val >> c->reg_shift) & BUS_CLK_DIV_MASK) + 1;
	c->mul = 1;

	if (c->mul != 0 && c->div != 0) {
		rate *= c->mul;
		rate += c->div - 1; /* round up */
		do_div(rate, c->div);
	}
	return rate;
}

static int tegra20_bus_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	int ret = -EINVAL;
	unsigned long flags;
	u32 val;
	int i;

	spin_lock_irqsave(&clock_register_lock, flags);

	val = clk_readl(c->reg);
	for (i = 1; i <= 4; i++) {
		if (rate == parent_rate / i) {
			val &= ~(BUS_CLK_DIV_MASK << c->reg_shift);
			val |= (i - 1) << c->reg_shift;
			clk_writel(val, c->reg);
			c->div = i;
			c->mul = 1;
			ret = 0;
			break;
		}
	}

	spin_unlock_irqrestore(&clock_register_lock, flags);

	return ret;
}

static long tegra20_bus_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	unsigned long parent_rate = *prate;
	s64 divider;

	if (rate >= parent_rate)
		return rate;

	divider = parent_rate;
	divider += rate - 1;
	do_div(divider, rate);

	if (divider < 0)
		return divider;

	if (divider > 4)
		divider = 4;
	do_div(parent_rate, divider);

	return parent_rate;
}

struct clk_ops tegra_bus_ops = {
	.is_enabled = tegra20_bus_clk_is_enabled,
	.enable = tegra20_bus_clk_enable,
	.disable = tegra20_bus_clk_disable,
	.set_rate = tegra20_bus_clk_set_rate,
	.round_rate = tegra20_bus_clk_round_rate,
	.recalc_rate = tegra20_bus_clk_recalc_rate,
};

/* Blink output functions */
static int tegra20_blink_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;

	val = pmc_readl(PMC_CTRL);
	c->state = (val & PMC_CTRL_BLINK_ENB) ? ON : OFF;
	return c->state;
}

static unsigned long tegra20_blink_clk_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u64 rate = prate;
	u32 val;

	c->mul = 1;
	val = pmc_readl(c->reg);

	if (val & PMC_BLINK_TIMER_ENB) {
		unsigned int on_off;

		on_off = (val >> PMC_BLINK_TIMER_DATA_ON_SHIFT) &
			PMC_BLINK_TIMER_DATA_ON_MASK;
		val >>= PMC_BLINK_TIMER_DATA_OFF_SHIFT;
		val &= PMC_BLINK_TIMER_DATA_OFF_MASK;
		on_off += val;
		/* each tick in the blink timer is 4 32KHz clocks */
		c->div = on_off * 4;
	} else {
		c->div = 1;
	}

	if (c->mul != 0 && c->div != 0) {
		rate *= c->mul;
		rate += c->div - 1; /* round up */
		do_div(rate, c->div);
	}
	return rate;
}

static int tegra20_blink_clk_enable(struct clk_hw *hw)
{
	u32 val;

	val = pmc_readl(PMC_DPD_PADS_ORIDE);
	pmc_writel(val | PMC_DPD_PADS_ORIDE_BLINK_ENB, PMC_DPD_PADS_ORIDE);

	val = pmc_readl(PMC_CTRL);
	pmc_writel(val | PMC_CTRL_BLINK_ENB, PMC_CTRL);

	return 0;
}

static void tegra20_blink_clk_disable(struct clk_hw *hw)
{
	u32 val;

	val = pmc_readl(PMC_CTRL);
	pmc_writel(val & ~PMC_CTRL_BLINK_ENB, PMC_CTRL);

	val = pmc_readl(PMC_DPD_PADS_ORIDE);
	pmc_writel(val & ~PMC_DPD_PADS_ORIDE_BLINK_ENB, PMC_DPD_PADS_ORIDE);
}

static int tegra20_blink_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_tegra *c = to_clk_tegra(hw);

	if (rate >= parent_rate) {
		c->div = 1;
		pmc_writel(0, c->reg);
	} else {
		unsigned int on_off;
		u32 val;

		on_off = DIV_ROUND_UP(parent_rate / 8, rate);
		c->div = on_off * 8;

		val = (on_off & PMC_BLINK_TIMER_DATA_ON_MASK) <<
			PMC_BLINK_TIMER_DATA_ON_SHIFT;
		on_off &= PMC_BLINK_TIMER_DATA_OFF_MASK;
		on_off <<= PMC_BLINK_TIMER_DATA_OFF_SHIFT;
		val |= on_off;
		val |= PMC_BLINK_TIMER_ENB;
		pmc_writel(val, c->reg);
	}

	return 0;
}

static long tegra20_blink_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	int div;
	int mul;
	long round_rate = *prate;

	mul = 1;

	if (rate >= *prate) {
		div = 1;
	} else {
		div = DIV_ROUND_UP(*prate / 8, rate);
		div *= 8;
	}

	round_rate *= mul;
	round_rate += div - 1;
	do_div(round_rate, div);

	return round_rate;
}

struct clk_ops tegra_blink_clk_ops = {
	.is_enabled = tegra20_blink_clk_is_enabled,
	.enable = tegra20_blink_clk_enable,
	.disable = tegra20_blink_clk_disable,
	.set_rate = tegra20_blink_clk_set_rate,
	.round_rate = tegra20_blink_clk_round_rate,
	.recalc_rate = tegra20_blink_clk_recalc_rate,
};

/* PLL Functions */
static int tegra20_pll_clk_wait_for_lock(struct clk_tegra *c)
{
	udelay(c->u.pll.lock_delay);
	return 0;
}

static int tegra20_pll_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg + PLL_BASE);

	c->state = (val & PLL_BASE_ENABLE) ? ON : OFF;
	return c->state;
}

static unsigned long tegra20_pll_clk_recalc_rate(struct clk_hw *hw,
				unsigned long prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg + PLL_BASE);
	u64 rate = prate;

	if (c->flags & PLL_FIXED && !(val & PLL_BASE_OVERRIDE)) {
		const struct clk_pll_freq_table *sel;
		for (sel = c->u.pll.freq_table; sel->input_rate != 0; sel++) {
			if (sel->input_rate == prate &&
				sel->output_rate == c->u.pll.fixed_rate) {
				c->mul = sel->n;
				c->div = sel->m * sel->p;
				break;
			}
		}
		pr_err("Clock %s has unknown fixed frequency\n",
			__clk_get_name(hw->clk));
		BUG();
	} else if (val & PLL_BASE_BYPASS) {
		c->mul = 1;
		c->div = 1;
	} else {
		c->mul = (val & PLL_BASE_DIVN_MASK) >> PLL_BASE_DIVN_SHIFT;
		c->div = (val & PLL_BASE_DIVM_MASK) >> PLL_BASE_DIVM_SHIFT;
		if (c->flags & PLLU)
			c->div *= (val & PLLU_BASE_POST_DIV) ? 1 : 2;
		else
			c->div *= (val & PLL_BASE_DIVP_MASK) ? 2 : 1;
	}

	if (c->mul != 0 && c->div != 0) {
		rate *= c->mul;
		rate += c->div - 1; /* round up */
		do_div(rate, c->div);
	}
	return rate;
}

static int tegra20_pll_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;
	pr_debug("%s on clock %s\n", __func__, __clk_get_name(hw->clk));

	val = clk_readl(c->reg + PLL_BASE);
	val &= ~PLL_BASE_BYPASS;
	val |= PLL_BASE_ENABLE;
	clk_writel(val, c->reg + PLL_BASE);

	tegra20_pll_clk_wait_for_lock(c);

	return 0;
}

static void tegra20_pll_clk_disable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;
	pr_debug("%s on clock %s\n", __func__, __clk_get_name(hw->clk));

	val = clk_readl(c->reg);
	val &= ~(PLL_BASE_BYPASS | PLL_BASE_ENABLE);
	clk_writel(val, c->reg);
}

static int tegra20_pll_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long input_rate = parent_rate;
	const struct clk_pll_freq_table *sel;
	u32 val;

	pr_debug("%s: %s %lu\n", __func__, __clk_get_name(hw->clk), rate);

	if (c->flags & PLL_FIXED) {
		int ret = 0;
		if (rate != c->u.pll.fixed_rate) {
			pr_err("%s: Can not change %s fixed rate %lu to %lu\n",
				__func__, __clk_get_name(hw->clk),
				c->u.pll.fixed_rate, rate);
			ret = -EINVAL;
		}
		return ret;
	}

	for (sel = c->u.pll.freq_table; sel->input_rate != 0; sel++) {
		if (sel->input_rate == input_rate && sel->output_rate == rate) {
			c->mul = sel->n;
			c->div = sel->m * sel->p;

			val = clk_readl(c->reg + PLL_BASE);
			if (c->flags & PLL_FIXED)
				val |= PLL_BASE_OVERRIDE;
			val &= ~(PLL_BASE_DIVP_MASK | PLL_BASE_DIVN_MASK |
				 PLL_BASE_DIVM_MASK);
			val |= (sel->m << PLL_BASE_DIVM_SHIFT) |
				(sel->n << PLL_BASE_DIVN_SHIFT);
			BUG_ON(sel->p < 1 || sel->p > 2);
			if (c->flags & PLLU) {
				if (sel->p == 1)
					val |= PLLU_BASE_POST_DIV;
			} else {
				if (sel->p == 2)
					val |= 1 << PLL_BASE_DIVP_SHIFT;
			}
			clk_writel(val, c->reg + PLL_BASE);

			if (c->flags & PLL_HAS_CPCON) {
				val = clk_readl(c->reg + PLL_MISC(c));
				val &= ~PLL_MISC_CPCON_MASK;
				val |= sel->cpcon << PLL_MISC_CPCON_SHIFT;
				clk_writel(val, c->reg + PLL_MISC(c));
			}

			if (c->state == ON)
				tegra20_pll_clk_enable(hw);
			return 0;
		}
	}
	return -EINVAL;
}

static long tegra20_pll_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	const struct clk_pll_freq_table *sel;
	unsigned long input_rate = *prate;
	u64 output_rate = *prate;
	int mul;
	int div;

	if (c->flags & PLL_FIXED)
		return c->u.pll.fixed_rate;

	for (sel = c->u.pll.freq_table; sel->input_rate != 0; sel++)
		if (sel->input_rate == input_rate && sel->output_rate == rate) {
			mul = sel->n;
			div = sel->m * sel->p;
			break;
		}

	if (sel->input_rate == 0)
		return -EINVAL;

	output_rate *= mul;
	output_rate += div - 1; /* round up */
	do_div(output_rate, div);

	return output_rate;
}

struct clk_ops tegra_pll_ops = {
	.is_enabled = tegra20_pll_clk_is_enabled,
	.enable = tegra20_pll_clk_enable,
	.disable = tegra20_pll_clk_disable,
	.set_rate = tegra20_pll_clk_set_rate,
	.recalc_rate = tegra20_pll_clk_recalc_rate,
	.round_rate = tegra20_pll_clk_round_rate,
};

static void tegra20_pllx_clk_init(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);

	if (tegra_sku_id == 7)
		c->max_rate = 750000000;
}

struct clk_ops tegra_pllx_ops = {
	.init = tegra20_pllx_clk_init,
	.is_enabled = tegra20_pll_clk_is_enabled,
	.enable = tegra20_pll_clk_enable,
	.disable = tegra20_pll_clk_disable,
	.set_rate = tegra20_pll_clk_set_rate,
	.recalc_rate = tegra20_pll_clk_recalc_rate,
	.round_rate = tegra20_pll_clk_round_rate,
};

static int tegra20_plle_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;

	pr_debug("%s on clock %s\n", __func__, __clk_get_name(hw->clk));

	mdelay(1);

	val = clk_readl(c->reg + PLL_BASE);
	if (!(val & PLLE_MISC_READY))
		return -EBUSY;

	val = clk_readl(c->reg + PLL_BASE);
	val |= PLL_BASE_ENABLE | PLL_BASE_BYPASS;
	clk_writel(val, c->reg + PLL_BASE);

	return 0;
}

struct clk_ops tegra_plle_ops = {
	.is_enabled = tegra20_pll_clk_is_enabled,
	.enable = tegra20_plle_clk_enable,
	.set_rate = tegra20_pll_clk_set_rate,
	.recalc_rate = tegra20_pll_clk_recalc_rate,
	.round_rate = tegra20_pll_clk_round_rate,
};

/* Clock divider ops */
static int tegra20_pll_div_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg);

	val >>= c->reg_shift;
	c->state = (val & PLL_OUT_CLKEN) ? ON : OFF;
	if (!(val & PLL_OUT_RESET_DISABLE))
		c->state = OFF;
	return c->state;
}

static unsigned long tegra20_pll_div_clk_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u64 rate = prate;
	u32 val = clk_readl(c->reg);
	u32 divu71;

	val >>= c->reg_shift;

	if (c->flags & DIV_U71) {
		divu71 = (val & PLL_OUT_RATIO_MASK) >> PLL_OUT_RATIO_SHIFT;
		c->div = (divu71 + 2);
		c->mul = 2;
	} else if (c->flags & DIV_2) {
		c->div = 2;
		c->mul = 1;
	} else {
		c->div = 1;
		c->mul = 1;
	}

	rate *= c->mul;
	rate += c->div - 1; /* round up */
	do_div(rate, c->div);

	return rate;
}

static int tegra20_pll_div_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;
	u32 new_val;
	u32 val;

	pr_debug("%s: %s\n", __func__, __clk_get_name(hw->clk));

	if (c->flags & DIV_U71) {
		spin_lock_irqsave(&clock_register_lock, flags);
		val = clk_readl(c->reg);
		new_val = val >> c->reg_shift;
		new_val &= 0xFFFF;

		new_val |= PLL_OUT_CLKEN | PLL_OUT_RESET_DISABLE;

		val &= ~(0xFFFF << c->reg_shift);
		val |= new_val << c->reg_shift;
		clk_writel(val, c->reg);
		spin_unlock_irqrestore(&clock_register_lock, flags);
		return 0;
	} else if (c->flags & DIV_2) {
		BUG_ON(!(c->flags & PLLD));
		spin_lock_irqsave(&clock_register_lock, flags);
		val = clk_readl(c->reg);
		val &= ~PLLD_MISC_DIV_RST;
		clk_writel(val, c->reg);
		spin_unlock_irqrestore(&clock_register_lock, flags);
		return 0;
	}
	return -EINVAL;
}

static void tegra20_pll_div_clk_disable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;
	u32 new_val;
	u32 val;

	pr_debug("%s: %s\n", __func__, __clk_get_name(hw->clk));

	if (c->flags & DIV_U71) {
		spin_lock_irqsave(&clock_register_lock, flags);
		val = clk_readl(c->reg);
		new_val = val >> c->reg_shift;
		new_val &= 0xFFFF;

		new_val &= ~(PLL_OUT_CLKEN | PLL_OUT_RESET_DISABLE);

		val &= ~(0xFFFF << c->reg_shift);
		val |= new_val << c->reg_shift;
		clk_writel(val, c->reg);
		spin_unlock_irqrestore(&clock_register_lock, flags);
	} else if (c->flags & DIV_2) {
		BUG_ON(!(c->flags & PLLD));
		spin_lock_irqsave(&clock_register_lock, flags);
		val = clk_readl(c->reg);
		val |= PLLD_MISC_DIV_RST;
		clk_writel(val, c->reg);
		spin_unlock_irqrestore(&clock_register_lock, flags);
	}
}

static int tegra20_pll_div_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;
	int divider_u71;
	u32 new_val;
	u32 val;

	pr_debug("%s: %s %lu\n", __func__, __clk_get_name(hw->clk), rate);

	if (c->flags & DIV_U71) {
		divider_u71 = clk_div71_get_divider(parent_rate, rate);
		if (divider_u71 >= 0) {
			spin_lock_irqsave(&clock_register_lock, flags);
			val = clk_readl(c->reg);
			new_val = val >> c->reg_shift;
			new_val &= 0xFFFF;
			if (c->flags & DIV_U71_FIXED)
				new_val |= PLL_OUT_OVERRIDE;
			new_val &= ~PLL_OUT_RATIO_MASK;
			new_val |= divider_u71 << PLL_OUT_RATIO_SHIFT;

			val &= ~(0xFFFF << c->reg_shift);
			val |= new_val << c->reg_shift;
			clk_writel(val, c->reg);
			c->div = divider_u71 + 2;
			c->mul = 2;
			spin_unlock_irqrestore(&clock_register_lock, flags);
			return 0;
		}
	} else if (c->flags & DIV_2) {
		if (parent_rate == rate * 2)
			return 0;
	}
	return -EINVAL;
}

static long tegra20_pll_div_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long parent_rate = *prate;
	int divider;

	pr_debug("%s: %s %lu\n", __func__, __clk_get_name(hw->clk), rate);

	if (c->flags & DIV_U71) {
		divider = clk_div71_get_divider(parent_rate, rate);
		if (divider < 0)
			return divider;
		return DIV_ROUND_UP(parent_rate * 2, divider + 2);
	} else if (c->flags & DIV_2) {
		return DIV_ROUND_UP(parent_rate, 2);
	}
	return -EINVAL;
}

struct clk_ops tegra_pll_div_ops = {
	.is_enabled = tegra20_pll_div_clk_is_enabled,
	.enable = tegra20_pll_div_clk_enable,
	.disable = tegra20_pll_div_clk_disable,
	.set_rate = tegra20_pll_div_clk_set_rate,
	.round_rate = tegra20_pll_div_clk_round_rate,
	.recalc_rate = tegra20_pll_div_clk_recalc_rate,
};

/* Periph clk ops */

static int tegra20_periph_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);

	c->state = ON;

	if (!c->u.periph.clk_num)
		goto out;

	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;

	if (!(c->flags & PERIPH_NO_RESET))
		if (clk_readl(RST_DEVICES + PERIPH_CLK_TO_ENB_REG(c)) &
				PERIPH_CLK_TO_ENB_BIT(c))
			c->state = OFF;

out:
	return c->state;
}

static int tegra20_periph_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;
	u32 val;

	pr_debug("%s on clock %s\n", __func__, __clk_get_name(hw->clk));

	if (!c->u.periph.clk_num)
		return 0;

	tegra_periph_clk_enable_refcount[c->u.periph.clk_num]++;
	if (tegra_periph_clk_enable_refcount[c->u.periph.clk_num] > 1)
		return 0;

	spin_lock_irqsave(&clock_register_lock, flags);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_SET + PERIPH_CLK_TO_ENB_SET_REG(c));
	if (!(c->flags & PERIPH_NO_RESET) && !(c->flags & PERIPH_MANUAL_RESET))
		clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
			RST_DEVICES_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));
	if (c->flags & PERIPH_EMC_ENB) {
		/* The EMC peripheral clock has 2 extra enable bits */
		/* FIXME: Do they need to be disabled? */
		val = clk_readl(c->reg);
		val |= 0x3 << 24;
		clk_writel(val, c->reg);
	}

	spin_unlock_irqrestore(&clock_register_lock, flags);

	return 0;
}

static void tegra20_periph_clk_disable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long flags;

	pr_debug("%s on clock %s\n", __func__, __clk_get_name(hw->clk));

	if (!c->u.periph.clk_num)
		return;

	tegra_periph_clk_enable_refcount[c->u.periph.clk_num]--;

	if (tegra_periph_clk_enable_refcount[c->u.periph.clk_num] > 0)
		return;

	spin_lock_irqsave(&clock_register_lock, flags);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));

	spin_unlock_irqrestore(&clock_register_lock, flags);
}

void tegra2_periph_clk_reset(struct clk_hw *hw, bool assert)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long base = assert ? RST_DEVICES_SET : RST_DEVICES_CLR;

	pr_debug("%s %s on clock %s\n", __func__,
		assert ? "assert" : "deassert", __clk_get_name(hw->clk));

	BUG_ON(!c->u.periph.clk_num);

	if (!(c->flags & PERIPH_NO_RESET))
		clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
			   base + PERIPH_CLK_TO_ENB_SET_REG(c));
}

static int tegra20_periph_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;
	u32 mask;
	u32 shift;

	pr_debug("%s: %s %d\n", __func__, __clk_get_name(hw->clk), index);

	if (c->flags & MUX_PWM) {
		shift = PERIPH_CLK_SOURCE_PWM_SHIFT;
		mask = PERIPH_CLK_SOURCE_PWM_MASK;
	} else {
		shift = PERIPH_CLK_SOURCE_SHIFT;
		mask = PERIPH_CLK_SOURCE_MASK;
	}

	val = clk_readl(c->reg);
	val &= ~mask;
	val |= (index) << shift;

	clk_writel(val, c->reg);

	return 0;
}

static u8 tegra20_periph_clk_get_parent(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg);
	u32 mask;
	u32 shift;

	if (c->flags & MUX_PWM) {
		shift = PERIPH_CLK_SOURCE_PWM_SHIFT;
		mask = PERIPH_CLK_SOURCE_PWM_MASK;
	} else {
		shift = PERIPH_CLK_SOURCE_SHIFT;
		mask = PERIPH_CLK_SOURCE_MASK;
	}

	if (c->flags & MUX)
		return (val & mask) >> shift;
	else
		return 0;
}

static unsigned long tegra20_periph_clk_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long rate = prate;
	u32 val = clk_readl(c->reg);

	if (c->flags & DIV_U71) {
		u32 divu71 = val & PERIPH_CLK_SOURCE_DIVU71_MASK;
		c->div = divu71 + 2;
		c->mul = 2;
	} else if (c->flags & DIV_U16) {
		u32 divu16 = val & PERIPH_CLK_SOURCE_DIVU16_MASK;
		c->div = divu16 + 1;
		c->mul = 1;
	} else {
		c->div = 1;
		c->mul = 1;
		return rate;
	}

	if (c->mul != 0 && c->div != 0) {
		rate *= c->mul;
		rate += c->div - 1; /* round up */
		do_div(rate, c->div);
	}

	return rate;
}

static int tegra20_periph_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;
	int divider;

	val = clk_readl(c->reg);

	if (c->flags & DIV_U71) {
		divider = clk_div71_get_divider(parent_rate, rate);

		if (divider >= 0) {
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_DIVU71_MASK;
			val |= divider;
			clk_writel(val, c->reg);
			c->div = divider + 2;
			c->mul = 2;
			return 0;
		}
	} else if (c->flags & DIV_U16) {
		divider = clk_div16_get_divider(parent_rate, rate);
		if (divider >= 0) {
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_DIVU16_MASK;
			val |= divider;
			clk_writel(val, c->reg);
			c->div = divider + 1;
			c->mul = 1;
			return 0;
		}
	} else if (parent_rate <= rate) {
		c->div = 1;
		c->mul = 1;
		return 0;
	}

	return -EINVAL;
}

static long tegra20_periph_clk_round_rate(struct clk_hw *hw,
	unsigned long rate, unsigned long *prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	unsigned long parent_rate = __clk_get_rate(__clk_get_parent(hw->clk));
	int divider;

	pr_debug("%s: %s %lu\n", __func__, __clk_get_name(hw->clk), rate);

	if (prate)
		parent_rate = *prate;

	if (c->flags & DIV_U71) {
		divider = clk_div71_get_divider(parent_rate, rate);
		if (divider < 0)
			return divider;

		return DIV_ROUND_UP(parent_rate * 2, divider + 2);
	} else if (c->flags & DIV_U16) {
		divider = clk_div16_get_divider(parent_rate, rate);
		if (divider < 0)
			return divider;
		return DIV_ROUND_UP(parent_rate, divider + 1);
	}
	return -EINVAL;
}

struct clk_ops tegra_periph_clk_ops = {
	.is_enabled = tegra20_periph_clk_is_enabled,
	.enable = tegra20_periph_clk_enable,
	.disable = tegra20_periph_clk_disable,
	.set_parent = tegra20_periph_clk_set_parent,
	.get_parent = tegra20_periph_clk_get_parent,
	.set_rate = tegra20_periph_clk_set_rate,
	.round_rate = tegra20_periph_clk_round_rate,
	.recalc_rate = tegra20_periph_clk_recalc_rate,
};

/* External memory controller clock ops */
static void tegra20_emc_clk_init(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	c->max_rate = __clk_get_rate(hw->clk);
}

static long tegra20_emc_clk_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	long emc_rate;
	long clk_rate;

	/*
	 * The slowest entry in the EMC clock table that is at least as
	 * fast as rate.
	 */
	emc_rate = tegra_emc_round_rate(rate);
	if (emc_rate < 0)
		return c->max_rate;

	/*
	 * The fastest rate the PLL will generate that is at most the
	 * requested rate.
	 */
	clk_rate = tegra20_periph_clk_round_rate(hw, emc_rate, NULL);

	/*
	 * If this fails, and emc_rate > clk_rate, it's because the maximum
	 * rate in the EMC tables is larger than the maximum rate of the EMC
	 * clock. The EMC clock's max rate is the rate it was running when the
	 * kernel booted. Such a mismatch is probably due to using the wrong
	 * BCT, i.e. using a Tegra20 BCT with an EMC table written for Tegra25.
	 */
	WARN_ONCE(emc_rate != clk_rate,
		"emc_rate %ld != clk_rate %ld",
		emc_rate, clk_rate);

	return emc_rate;
}

static int tegra20_emc_clk_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	int ret;

	/*
	 * The Tegra2 memory controller has an interlock with the clock
	 * block that allows memory shadowed registers to be updated,
	 * and then transfer them to the main registers at the same
	 * time as the clock update without glitches.
	 */
	ret = tegra_emc_set_rate(rate);
	if (ret < 0)
		return ret;

	ret = tegra20_periph_clk_set_rate(hw, rate, parent_rate);
	udelay(1);

	return ret;
}

struct clk_ops tegra_emc_clk_ops = {
	.init = tegra20_emc_clk_init,
	.is_enabled = tegra20_periph_clk_is_enabled,
	.enable = tegra20_periph_clk_enable,
	.disable = tegra20_periph_clk_disable,
	.set_parent = tegra20_periph_clk_set_parent,
	.get_parent = tegra20_periph_clk_get_parent,
	.set_rate = tegra20_emc_clk_set_rate,
	.round_rate = tegra20_emc_clk_round_rate,
	.recalc_rate = tegra20_periph_clk_recalc_rate,
};

/* Clock doubler ops */
static int tegra20_clk_double_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);

	c->state = ON;

	if (!c->u.periph.clk_num)
		goto out;

	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;

out:
	return c->state;
};

static unsigned long tegra20_clk_double_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u64 rate = prate;

	c->mul = 2;
	c->div = 1;

	rate *= c->mul;
	rate += c->div - 1; /* round up */
	do_div(rate, c->div);

	return rate;
}

static long tegra20_clk_double_round_rate(struct clk_hw *hw, unsigned long rate,
				unsigned long *prate)
{
	unsigned long output_rate = *prate;

	do_div(output_rate, 2);
	return output_rate;
}

static int tegra20_clk_double_set_rate(struct clk_hw *hw, unsigned long rate,
		unsigned long parent_rate)
{
	if (rate != 2 * parent_rate)
		return -EINVAL;
	return 0;
}

struct clk_ops tegra_clk_double_ops = {
	.is_enabled = tegra20_clk_double_is_enabled,
	.enable = tegra20_periph_clk_enable,
	.disable = tegra20_periph_clk_disable,
	.set_rate = tegra20_clk_double_set_rate,
	.recalc_rate = tegra20_clk_double_recalc_rate,
	.round_rate = tegra20_clk_double_round_rate,
};

/* Audio sync clock ops */
static int tegra20_audio_sync_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg);

	c->state = (val & (1<<4)) ? OFF : ON;
	return c->state;
}

static int tegra20_audio_sync_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);

	clk_writel(0, c->reg);
	return 0;
}

static void tegra20_audio_sync_clk_disable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	clk_writel(1, c->reg);
}

static u8 tegra20_audio_sync_clk_get_parent(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val = clk_readl(c->reg);
	int source;

	source = val & 0xf;
	return source;
}

static int tegra20_audio_sync_clk_set_parent(struct clk_hw *hw, u8 index)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	u32 val;

	val = clk_readl(c->reg);
	val &= ~0xf;
	val |= index;

	clk_writel(val, c->reg);

	return 0;
}

struct clk_ops tegra_audio_sync_clk_ops = {
	.is_enabled = tegra20_audio_sync_clk_is_enabled,
	.enable = tegra20_audio_sync_clk_enable,
	.disable = tegra20_audio_sync_clk_disable,
	.set_parent = tegra20_audio_sync_clk_set_parent,
	.get_parent = tegra20_audio_sync_clk_get_parent,
};

/* cdev1 and cdev2 (dap_mclk1 and dap_mclk2) ops */

static int tegra20_cdev_clk_is_enabled(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	/* We could un-tristate the cdev1 or cdev2 pingroup here; this is
	 * currently done in the pinmux code. */
	c->state = ON;

	BUG_ON(!c->u.periph.clk_num);

	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;
	return c->state;
}

static int tegra20_cdev_clk_enable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	BUG_ON(!c->u.periph.clk_num);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_SET + PERIPH_CLK_TO_ENB_SET_REG(c));
	return 0;
}

static void tegra20_cdev_clk_disable(struct clk_hw *hw)
{
	struct clk_tegra *c = to_clk_tegra(hw);
	BUG_ON(!c->u.periph.clk_num);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));
}

static unsigned long tegra20_cdev_recalc_rate(struct clk_hw *hw,
			unsigned long prate)
{
	return to_clk_tegra(hw)->fixed_rate;
}

struct clk_ops tegra_cdev_clk_ops = {
	.is_enabled = tegra20_cdev_clk_is_enabled,
	.enable = tegra20_cdev_clk_enable,
	.disable = tegra20_cdev_clk_disable,
	.recalc_rate = tegra20_cdev_recalc_rate,
};
