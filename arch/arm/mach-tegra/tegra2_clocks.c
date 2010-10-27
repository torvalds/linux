/*
 * arch/arm/mach-tegra/tegra2_clocks.c
 *
 * Copyright (C) 2010 Google, Inc.
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
#include <linux/hrtimer.h>

#include <asm/clkdev.h>

#include <mach/iomap.h>

#include "clock.h"

#define RST_DEVICES			0x004
#define RST_DEVICES_SET			0x300
#define RST_DEVICES_CLR			0x304

#define CLK_OUT_ENB			0x010
#define CLK_OUT_ENB_SET			0x320
#define CLK_OUT_ENB_CLR			0x324

#define OSC_CTRL			0x50
#define OSC_CTRL_OSC_FREQ_MASK		(3<<30)
#define OSC_CTRL_OSC_FREQ_13MHZ		(0<<30)
#define OSC_CTRL_OSC_FREQ_19_2MHZ	(1<<30)
#define OSC_CTRL_OSC_FREQ_12MHZ		(2<<30)
#define OSC_CTRL_OSC_FREQ_26MHZ		(3<<30)

#define OSC_FREQ_DET			0x58
#define OSC_FREQ_DET_TRIG		(1<<31)

#define OSC_FREQ_DET_STATUS		0x5C
#define OSC_FREQ_DET_BUSY		(1<<31)
#define OSC_FREQ_DET_CNT_MASK		0xFFFF

#define PERIPH_CLK_SOURCE_MASK		(3<<30)
#define PERIPH_CLK_SOURCE_SHIFT		30
#define PERIPH_CLK_SOURCE_ENABLE	(1<<28)
#define PERIPH_CLK_SOURCE_DIV_MASK	0xFF
#define PERIPH_CLK_SOURCE_DIV_SHIFT	0

#define PLL_BASE			0x0
#define PLL_BASE_BYPASS			(1<<31)
#define PLL_BASE_ENABLE			(1<<30)
#define PLL_BASE_REF_ENABLE		(1<<29)
#define PLL_BASE_OVERRIDE		(1<<28)
#define PLL_BASE_LOCK			(1<<27)
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

#define PLL_MISC(c)			(((c)->flags & PLL_ALT_MISC_REG) ? 0x4 : 0xc)
#define PLL_MISC_DCCON_SHIFT		20
#define PLL_MISC_LOCK_ENABLE		(1<<18)
#define PLL_MISC_CPCON_SHIFT		8
#define PLL_MISC_CPCON_MASK		(0xF<<PLL_MISC_CPCON_SHIFT)
#define PLL_MISC_LFCON_SHIFT		4
#define PLL_MISC_LFCON_MASK		(0xF<<PLL_MISC_LFCON_SHIFT)
#define PLL_MISC_VCOCON_SHIFT		0
#define PLL_MISC_VCOCON_MASK		(0xF<<PLL_MISC_VCOCON_SHIFT)

#define PLLD_MISC_CLKENABLE		(1<<30)
#define PLLD_MISC_DIV_RST		(1<<23)
#define PLLD_MISC_DCCON_SHIFT		12

#define PERIPH_CLK_TO_ENB_REG(c)	((c->clk_num / 32) * 4)
#define PERIPH_CLK_TO_ENB_SET_REG(c)	((c->clk_num / 32) * 8)
#define PERIPH_CLK_TO_ENB_BIT(c)	(1 << (c->clk_num % 32))

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

static void __iomem *reg_clk_base = IO_ADDRESS(TEGRA_CLK_RESET_BASE);

#define clk_writel(value, reg) \
	__raw_writel(value, (u32)reg_clk_base + (reg))
#define clk_readl(reg) \
	__raw_readl((u32)reg_clk_base + (reg))

unsigned long clk_measure_input_freq(void)
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
		pr_err("%s: Unexpected clock autodetect value %d", __func__, clock_autodetect);
		BUG();
		return 0;
	}
}

static int clk_div71_get_divider(struct clk *c, unsigned long rate)
{
	unsigned long divider_u71;

	divider_u71 = DIV_ROUND_UP(c->rate * 2, rate);

	if (divider_u71 - 2 > 255 || divider_u71 - 2 < 0)
		return -EINVAL;

	return divider_u71 - 2;
}

static unsigned long tegra2_clk_recalculate_rate(struct clk *c)
{
	unsigned long rate;
	rate = c->parent->rate;

	if (c->mul != 0 && c->div != 0)
		c->rate = rate * c->mul / c->div;
	else
		c->rate = rate;
	return c->rate;
}


/* clk_m functions */
static unsigned long tegra2_clk_m_autodetect_rate(struct clk *c)
{
	u32 auto_clock_control = clk_readl(OSC_CTRL) & ~OSC_CTRL_OSC_FREQ_MASK;

	c->rate = clk_measure_input_freq();
	switch (c->rate) {
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
		pr_err("%s: Unexpected clock rate %ld", __func__, c->rate);
		BUG();
	}
	clk_writel(auto_clock_control, OSC_CTRL);
	return c->rate;
}

static void tegra2_clk_m_init(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	tegra2_clk_m_autodetect_rate(c);
}

static int tegra2_clk_m_enable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	return 0;
}

static void tegra2_clk_m_disable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	BUG();
}

static struct clk_ops tegra_clk_m_ops = {
	.init		= tegra2_clk_m_init,
	.enable		= tegra2_clk_m_enable,
	.disable	= tegra2_clk_m_disable,
};

/* super clock functions */
/* "super clocks" on tegra have two-stage muxes and a clock skipping
 * super divider.  We will ignore the clock skipping divider, since we
 * can't lower the voltage when using the clock skip, but we can if we
 * lower the PLL frequency.
 */
static void tegra2_super_clk_init(struct clk *c)
{
	u32 val;
	int source;
	int shift;
	const struct clk_mux_sel *sel;
	val = clk_readl(c->reg + SUPER_CLK_MUX);
	c->state = ON;
	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	source = (val >> shift) & SUPER_SOURCE_MASK;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->value == source)
			break;
	}
	BUG_ON(sel->input == NULL);
	c->parent = sel->input;
	tegra2_clk_recalculate_rate(c);
}

static int tegra2_super_clk_enable(struct clk *c)
{
	clk_writel(0, c->reg + SUPER_CLK_DIVIDER);
	return 0;
}

static void tegra2_super_clk_disable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);

	/* oops - don't disable the CPU clock! */
	BUG();
}

static int tegra2_super_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	int shift;
	val = clk_readl(c->reg + SUPER_CLK_MUX);;
	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			clk_reparent(c, p);
			val &= ~(SUPER_SOURCE_MASK << shift);
			val |= sel->value << shift;
			clk_writel(val, c->reg);
			c->rate = c->parent->rate;
			return 0;
		}
	}
	return -EINVAL;
}

static struct clk_ops tegra_super_ops = {
	.init			= tegra2_super_clk_init,
	.enable			= tegra2_super_clk_enable,
	.disable		= tegra2_super_clk_disable,
	.set_parent		= tegra2_super_clk_set_parent,
	.recalculate_rate	= tegra2_clk_recalculate_rate,
};

/* bus clock functions */
static void tegra2_bus_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	c->state = ((val >> c->reg_shift) & BUS_CLK_DISABLE) ? OFF : ON;
	c->div = ((val >> c->reg_shift) & BUS_CLK_DIV_MASK) + 1;
	c->mul = 1;
	tegra2_clk_recalculate_rate(c);
}

static int tegra2_bus_clk_enable(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	val &= ~(BUS_CLK_DISABLE << c->reg_shift);
	clk_writel(val, c->reg);
	return 0;
}

static void tegra2_bus_clk_disable(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	val |= BUS_CLK_DISABLE << c->reg_shift;
	clk_writel(val, c->reg);
}

static int tegra2_bus_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val = clk_readl(c->reg);
	unsigned long parent_rate = c->parent->rate;
	int i;
	for (i = 1; i <= 4; i++) {
		if (rate == parent_rate / i) {
			val &= ~(BUS_CLK_DIV_MASK << c->reg_shift);
			val |= (i - 1) << c->reg_shift;
			clk_writel(val, c->reg);
			c->div = i;
			c->mul = 1;
			return 0;
		}
	}
	return -EINVAL;
}

static struct clk_ops tegra_bus_ops = {
	.init			= tegra2_bus_clk_init,
	.enable			= tegra2_bus_clk_enable,
	.disable		= tegra2_bus_clk_disable,
	.set_rate		= tegra2_bus_clk_set_rate,
	.recalculate_rate	= tegra2_clk_recalculate_rate,
};

/* PLL Functions */
static unsigned long tegra2_pll_clk_recalculate_rate(struct clk *c)
{
	u64 rate;
	rate = c->parent->rate;
	rate *= c->n;
	do_div(rate, c->m);
	if (c->p == 2)
		rate >>= 1;
	c->rate = rate;
	return c->rate;
}

static int tegra2_pll_clk_wait_for_lock(struct clk *c)
{
	ktime_t before;

	before = ktime_get();
	while (!(clk_readl(c->reg + PLL_BASE) & PLL_BASE_LOCK)) {
		if (ktime_us_delta(ktime_get(), before) > 5000) {
			pr_err("Timed out waiting for lock bit on pll %s",
				c->name);
			return -1;
		}
	}

	return 0;
}

static void tegra2_pll_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg + PLL_BASE);

	c->state = (val & PLL_BASE_ENABLE) ? ON : OFF;

	if (c->flags & PLL_FIXED && !(val & PLL_BASE_OVERRIDE)) {
		pr_warning("Clock %s has unknown fixed frequency\n", c->name);
		c->n = 1;
		c->m = 0;
		c->p = 1;
	} else if (val & PLL_BASE_BYPASS) {
		c->n = 1;
		c->m = 1;
		c->p = 1;
	} else {
		c->n = (val & PLL_BASE_DIVN_MASK) >> PLL_BASE_DIVN_SHIFT;
		c->m = (val & PLL_BASE_DIVM_MASK) >> PLL_BASE_DIVM_SHIFT;
		c->p = (val & PLL_BASE_DIVP_MASK) ? 2 : 1;
	}

	val = clk_readl(c->reg + PLL_MISC(c));
	if (c->flags & PLL_HAS_CPCON)
		c->cpcon = (val & PLL_MISC_CPCON_MASK) >> PLL_MISC_CPCON_SHIFT;

	tegra2_pll_clk_recalculate_rate(c);
}

static int tegra2_pll_clk_enable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

	val = clk_readl(c->reg + PLL_BASE);
	val &= ~PLL_BASE_BYPASS;
	val |= PLL_BASE_ENABLE;
	clk_writel(val, c->reg + PLL_BASE);

	val = clk_readl(c->reg + PLL_MISC(c));
	val |= PLL_MISC_LOCK_ENABLE;
	clk_writel(val, c->reg + PLL_MISC(c));

	tegra2_pll_clk_wait_for_lock(c);

	return 0;
}

static void tegra2_pll_clk_disable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

	val = clk_readl(c->reg);
	val &= ~(PLL_BASE_BYPASS | PLL_BASE_ENABLE);
	clk_writel(val, c->reg);
}

static int tegra2_pll_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	unsigned long input_rate;
	const struct clk_pll_table *sel;

	pr_debug("%s: %s %lu\n", __func__, c->name, rate);
	BUG_ON(c->refcnt != 0);

	input_rate = c->parent->rate;
	for (sel = c->pll_table; sel->input_rate != 0; sel++) {
		if (sel->input_rate == input_rate && sel->output_rate == rate) {
			c->n = sel->n;
			c->m = sel->m;
			c->p = sel->p;
			c->cpcon = sel->cpcon;

			val = clk_readl(c->reg + PLL_BASE);
			if (c->flags & PLL_FIXED)
				val |= PLL_BASE_OVERRIDE;
			val &= ~(PLL_BASE_DIVP_MASK | PLL_BASE_DIVN_MASK |
				 PLL_BASE_DIVM_MASK);
			val |= (c->m << PLL_BASE_DIVM_SHIFT) |
				(c->n << PLL_BASE_DIVN_SHIFT);
			BUG_ON(c->p > 2);
			if (c->p == 2)
				val |= 1 << PLL_BASE_DIVP_SHIFT;
			clk_writel(val, c->reg + PLL_BASE);

			if (c->flags & PLL_HAS_CPCON) {
				val = c->cpcon << PLL_MISC_CPCON_SHIFT;
				val |= PLL_MISC_LOCK_ENABLE;
				clk_writel(val, c->reg + PLL_MISC(c));
			}

			if (c->state == ON)
				tegra2_pll_clk_enable(c);

			c->rate = rate;
			return 0;
		}
	}
	return -EINVAL;
}

static struct clk_ops tegra_pll_ops = {
	.init			= tegra2_pll_clk_init,
	.enable			= tegra2_pll_clk_enable,
	.disable		= tegra2_pll_clk_disable,
	.set_rate		= tegra2_pll_clk_set_rate,
	.recalculate_rate	= tegra2_pll_clk_recalculate_rate,
};

/* Clock divider ops */
static void tegra2_pll_div_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	u32 divu71;
	val >>= c->reg_shift;
	c->state = (val & PLL_OUT_CLKEN) ? ON : OFF;
	if (!(val & PLL_OUT_RESET_DISABLE))
		c->state = OFF;

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

	tegra2_clk_recalculate_rate(c);
}

static int tegra2_pll_div_clk_enable(struct clk *c)
{
	u32 val;
	u32 new_val;

	pr_debug("%s: %s\n", __func__, c->name);
	if (c->flags & DIV_U71) {
		val = clk_readl(c->reg);
		new_val = val >> c->reg_shift;
		new_val &= 0xFFFF;

		new_val |= PLL_OUT_CLKEN | PLL_OUT_RESET_DISABLE;

		val &= ~(0xFFFF << c->reg_shift);
		val |= new_val << c->reg_shift;
		clk_writel(val, c->reg);
		return 0;
	} else if (c->flags & DIV_2) {
		BUG_ON(!(c->flags & PLLD));
		val = clk_readl(c->reg);
		val &= ~PLLD_MISC_DIV_RST;
		clk_writel(val, c->reg);
		return 0;
	}
	return -EINVAL;
}

static void tegra2_pll_div_clk_disable(struct clk *c)
{
	u32 val;
	u32 new_val;

	pr_debug("%s: %s\n", __func__, c->name);
	if (c->flags & DIV_U71) {
		val = clk_readl(c->reg);
		new_val = val >> c->reg_shift;
		new_val &= 0xFFFF;

		new_val &= ~(PLL_OUT_CLKEN | PLL_OUT_RESET_DISABLE);

		val &= ~(0xFFFF << c->reg_shift);
		val |= new_val << c->reg_shift;
		clk_writel(val, c->reg);
	} else if (c->flags & DIV_2) {
		BUG_ON(!(c->flags & PLLD));
		val = clk_readl(c->reg);
		val |= PLLD_MISC_DIV_RST;
		clk_writel(val, c->reg);
	}
}

static int tegra2_pll_div_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	u32 new_val;
	int divider_u71;
	pr_debug("%s: %s %lu\n", __func__, c->name, rate);
	if (c->flags & DIV_U71) {
		divider_u71 = clk_div71_get_divider(c->parent, rate);
		if (divider_u71 >= 0) {
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
			tegra2_clk_recalculate_rate(c);
			return 0;
		}
	} else if (c->flags & DIV_2) {
		if (c->parent->rate == rate * 2) {
			c->rate = rate;
			return 0;
		}
	}
	return -EINVAL;
}


static struct clk_ops tegra_pll_div_ops = {
	.init			= tegra2_pll_div_clk_init,
	.enable			= tegra2_pll_div_clk_enable,
	.disable		= tegra2_pll_div_clk_disable,
	.set_rate		= tegra2_pll_div_clk_set_rate,
	.recalculate_rate	= tegra2_clk_recalculate_rate,
};

/* Periph clk ops */

static void tegra2_periph_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	const struct clk_mux_sel *mux = 0;
	const struct clk_mux_sel *sel;
	if (c->flags & MUX) {
		for (sel = c->inputs; sel->input != NULL; sel++) {
			if (val >> PERIPH_CLK_SOURCE_SHIFT == sel->value)
				mux = sel;
		}
		BUG_ON(!mux);

		c->parent = mux->input;
	} else {
		c->parent = c->inputs[0].input;
	}

	if (c->flags & DIV_U71) {
		u32 divu71 = val & PERIPH_CLK_SOURCE_DIV_MASK;
		c->div = divu71 + 2;
		c->mul = 2;
	} else {
		c->div = 1;
		c->mul = 1;
	}

	c->state = ON;
	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;
	if (!(c->flags & PERIPH_NO_RESET))
		if (clk_readl(RST_DEVICES + PERIPH_CLK_TO_ENB_REG(c)) &
				PERIPH_CLK_TO_ENB_BIT(c))
			c->state = OFF;
	tegra2_clk_recalculate_rate(c);
}

static int tegra2_periph_clk_enable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

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
	return 0;
}

static void tegra2_periph_clk_disable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));
}

void tegra2_periph_reset_deassert(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	if (!(c->flags & PERIPH_NO_RESET))
		clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
			RST_DEVICES_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));
}

void tegra2_periph_reset_assert(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);
	if (!(c->flags & PERIPH_NO_RESET))
		clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
			RST_DEVICES_SET + PERIPH_CLK_TO_ENB_SET_REG(c));
}


static int tegra2_periph_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	pr_debug("%s: %s %s\n", __func__, c->name, p->name);
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			clk_reparent(c, p);
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_MASK;
			val |= (sel->value) << PERIPH_CLK_SOURCE_SHIFT;
			clk_writel(val, c->reg);
			c->rate = c->parent->rate;
			return 0;
		}
	}

	return -EINVAL;
}

static int tegra2_periph_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	int divider_u71;
	pr_debug("%s: %lu\n", __func__, rate);
	if (c->flags & DIV_U71) {
		divider_u71 = clk_div71_get_divider(c->parent, rate);
		if (divider_u71 >= 0) {
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_DIV_MASK;
			val |= divider_u71;
			clk_writel(val, c->reg);
			c->div = divider_u71 + 2;
			c->mul = 2;
			tegra2_clk_recalculate_rate(c);
			return 0;
		}
	}
	return -EINVAL;
}

static struct clk_ops tegra_periph_clk_ops = {
	.init			= &tegra2_periph_clk_init,
	.enable			= &tegra2_periph_clk_enable,
	.disable		= &tegra2_periph_clk_disable,
	.set_parent		= &tegra2_periph_clk_set_parent,
	.set_rate		= &tegra2_periph_clk_set_rate,
	.recalculate_rate	= &tegra2_clk_recalculate_rate,
};

/* Clock doubler ops */
static void tegra2_clk_double_init(struct clk *c)
{
	c->mul = 2;
	c->div = 1;
	c->state = ON;
	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;
	tegra2_clk_recalculate_rate(c);
};

static struct clk_ops tegra_clk_double_ops = {
	.init			= &tegra2_clk_double_init,
	.enable			= &tegra2_periph_clk_enable,
	.disable		= &tegra2_periph_clk_disable,
	.recalculate_rate	= &tegra2_clk_recalculate_rate,
};

/* Clock definitions */
static struct clk tegra_clk_32k = {
	.name = "clk_32k",
	.rate = 32678,
	.ops  = NULL,
};

static struct clk_pll_table tegra_pll_s_table[] = {
	{32768, 12000000, 366, 1, 1, 0},
	{32768, 13000000, 397, 1, 1, 0},
	{32768, 19200000, 586, 1, 1, 0},
	{32768, 26000000, 793, 1, 1, 0},
	{0, 0, 0, 0, 0, 0},
};

static struct clk tegra_pll_s = {
	.name      = "pll_s",
	.flags     = PLL_ALT_MISC_REG,
	.ops       = &tegra_pll_ops,
	.reg       = 0xf0,
	.input_min = 32768,
	.input_max = 32768,
	.parent    = &tegra_clk_32k,
	.cf_min    = 0, /* FIXME */
	.cf_max    = 0, /* FIXME */
	.vco_min   = 12000000,
	.vco_max   = 26000000,
	.pll_table = tegra_pll_s_table,
};

static struct clk_mux_sel tegra_clk_m_sel[] = {
	{ .input = &tegra_clk_32k, .value = 0},
	{ .input = &tegra_pll_s,  .value = 1},
	{ 0, 0},
};
static struct clk tegra_clk_m = {
	.name      = "clk_m",
	.flags     = ENABLE_ON_INIT,
	.ops       = &tegra_clk_m_ops,
	.inputs    = tegra_clk_m_sel,
	.reg       = 0x1fc,
	.reg_mask  = (1<<28),
	.reg_shift = 28,
};

static struct clk_pll_table tegra_pll_c_table[] = {
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_c = {
	.name      = "pll_c",
	.flags	   = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0x80,
	.input_min = 2000000,
	.input_max = 31000000,
	.parent    = &tegra_clk_m,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 20000000,
	.vco_max   = 1400000000,
	.pll_table = tegra_pll_c_table,
};

static struct clk tegra_pll_c_out1 = {
	.name      = "pll_c_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_c,
	.reg       = 0x84,
	.reg_shift = 0,
};

static struct clk_pll_table tegra_pll_m_table[] = {
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_m = {
	.name      = "pll_m",
	.flags     = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0x90,
	.input_min = 2000000,
	.input_max = 31000000,
	.parent    = &tegra_clk_m,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 20000000,
	.vco_max   = 1200000000,
	.pll_table = tegra_pll_m_table,
};

static struct clk tegra_pll_m_out1 = {
	.name      = "pll_m_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_m,
	.reg       = 0x94,
	.reg_shift = 0,
};

static struct clk_pll_table tegra_pll_p_table[] = {
	{ 12000000, 216000000, 432, 12, 2, 8},
	{ 13000000, 216000000, 432, 13, 2, 8},
	{ 19200000, 216000000, 90,   4, 2, 1},
	{ 26000000, 216000000, 432, 26, 2, 8},
	{ 12000000, 432000000, 432, 12, 1, 8},
	{ 13000000, 432000000, 432, 13, 1, 8},
	{ 19200000, 432000000, 90,   4, 1, 1},
	{ 26000000, 432000000, 432, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_p = {
	.name      = "pll_p",
	.flags     = ENABLE_ON_INIT | PLL_FIXED | PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0xa0,
	.input_min = 2000000,
	.input_max = 31000000,
	.parent    = &tegra_clk_m,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 20000000,
	.vco_max   = 1400000000,
	.pll_table = tegra_pll_p_table,
};

static struct clk tegra_pll_p_out1 = {
	.name      = "pll_p_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa4,
	.reg_shift = 0,
};

static struct clk tegra_pll_p_out2 = {
	.name      = "pll_p_out2",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa4,
	.reg_shift = 16,
};

static struct clk tegra_pll_p_out3 = {
	.name      = "pll_p_out3",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa8,
	.reg_shift = 0,
};

static struct clk tegra_pll_p_out4 = {
	.name      = "pll_p_out4",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa8,
	.reg_shift = 16,
};

static struct clk_pll_table tegra_pll_a_table[] = {
	{ 28800000, 56448000, 49, 25, 1, 1},
	{ 28800000, 73728000, 64, 25, 1, 1},
	{ 28800000, 11289600, 49, 25, 1, 1},
	{ 28800000, 12288000, 64, 25, 1, 1},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_a = {
	.name      = "pll_a",
	.flags     = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0xb0,
	.input_min = 2000000,
	.input_max = 31000000,
	.parent    = &tegra_pll_p_out1,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 20000000,
	.vco_max   = 1400000000,
	.pll_table = tegra_pll_a_table,
};

static struct clk tegra_pll_a_out0 = {
	.name      = "pll_a_out0",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_a,
	.reg       = 0xb4,
	.reg_shift = 0,
};

static struct clk_pll_table tegra_pll_d_table[] = {
	{ 12000000, 1000000000, 1000, 12, 1, 12},
	{ 13000000, 1000000000, 1000, 13, 1, 12},
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 12},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_d = {
	.name      = "pll_d",
	.flags     = PLL_HAS_CPCON | PLLD,
	.ops       = &tegra_pll_ops,
	.reg       = 0xd0,
	.input_min = 2000000,
	.input_max = 40000000,
	.parent    = &tegra_clk_m,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 40000000,
	.vco_max   = 1000000000,
	.pll_table = tegra_pll_d_table,
};

static struct clk tegra_pll_d_out0 = {
	.name      = "pll_d_out0",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_2 | PLLD,
	.parent    = &tegra_pll_d,
};

static struct clk_pll_table tegra_pll_u_table[] = {
	{ 12000000, 480000000, 960, 12, 1, 0},
	{ 13000000, 480000000, 960, 13, 1, 0},
	{ 19200000, 480000000, 200, 4,  1, 0},
	{ 26000000, 480000000, 960, 26, 1, 0},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_u = {
	.name      = "pll_u",
	.flags     = 0,
	.ops       = &tegra_pll_ops,
	.reg       = 0xc0,
	.input_min = 2000000,
	.input_max = 40000000,
	.parent    = &tegra_clk_m,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 480000000,
	.vco_max   = 960000000,
	.pll_table = tegra_pll_u_table,
};

static struct clk_pll_table tegra_pll_x_table[] = {
	{ 12000000, 1000000000, 1000, 12, 1, 12},
	{ 13000000, 1000000000, 1000, 13, 1, 12},
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 12},
	{ 12000000, 750000000,  750,  12, 1, 12},
	{ 13000000, 750000000,  750,  13, 1, 12},
	{ 19200000, 750000000,  625,  16, 1, 8},
	{ 26000000, 750000000,  750,  26, 1, 12},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_x = {
	.name      = "pll_x",
	.flags     = PLL_HAS_CPCON | PLL_ALT_MISC_REG,
	.ops       = &tegra_pll_ops,
	.reg       = 0xe0,
	.input_min = 2000000,
	.input_max = 31000000,
	.parent    = &tegra_clk_m,
	.cf_min    = 1000000,
	.cf_max    = 6000000,
	.vco_min   = 20000000,
	.vco_max   = 1200000000,
	.pll_table = tegra_pll_x_table,
};

static struct clk tegra_clk_d = {
	.name      = "clk_d",
	.flags     = PERIPH_NO_RESET,
	.ops       = &tegra_clk_double_ops,
	.clk_num   = 90,
	.reg       = 0x34,
	.reg_shift = 12,
	.parent    = &tegra_clk_m,
};

/* FIXME: need tegra_audio
static struct clk tegra_clk_audio_2x = {
	.name      = "clk_d",
	.flags     = PERIPH_NO_RESET,
	.ops       = &tegra_clk_double_ops,
	.clk_num   = 89,
	.reg       = 0x34,
	.reg_shift = 8,
	.parent    = &tegra_audio,
}
*/

static struct clk_mux_sel mux_cclk[] = {
	{ .input = &tegra_clk_m,	.value = 0},
	{ .input = &tegra_pll_c,	.value = 1},
	{ .input = &tegra_clk_32k,	.value = 2},
	{ .input = &tegra_pll_m,	.value = 3},
	{ .input = &tegra_pll_p,	.value = 4},
	{ .input = &tegra_pll_p_out4,	.value = 5},
	{ .input = &tegra_pll_p_out3,	.value = 6},
	{ .input = &tegra_clk_d,	.value = 7},
	{ .input = &tegra_pll_x,	.value = 8},
	{ 0, 0},
};

static struct clk_mux_sel mux_sclk[] = {
	{ .input = &tegra_clk_m,	.value = 0},
	{ .input = &tegra_pll_c_out1,	.value = 1},
	{ .input = &tegra_pll_p_out4,	.value = 2},
	{ .input = &tegra_pll_p_out3,	.value = 3},
	{ .input = &tegra_pll_p_out2,	.value = 4},
	{ .input = &tegra_clk_d,	.value = 5},
	{ .input = &tegra_clk_32k,	.value = 6},
	{ .input = &tegra_pll_m_out1,	.value = 7},
	{ 0, 0},
};

static struct clk tegra_clk_cpu = {
	.name	= "cpu",
	.inputs	= mux_cclk,
	.reg	= 0x20,
	.ops	= &tegra_super_ops,
};

static struct clk tegra_clk_sys = {
	.name	= "sys",
	.inputs	= mux_sclk,
	.reg	= 0x28,
	.ops	= &tegra_super_ops,
};

static struct clk tegra_clk_hclk = {
	.name		= "hclk",
	.flags		= DIV_BUS,
	.parent		= &tegra_clk_sys,
	.reg		= 0x30,
	.reg_shift	= 4,
	.ops		= &tegra_bus_ops,
};

static struct clk tegra_clk_pclk = {
	.name		= "pclk",
	.flags		= DIV_BUS,
	.parent		= &tegra_clk_hclk,
	.reg		= 0x30,
	.reg_shift	= 0,
	.ops		= &tegra_bus_ops,
};

static struct clk_mux_sel mux_pllm_pllc_pllp_plla[] = {
	{ .input = &tegra_pll_m, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_p, .value = 2},
	{ .input = &tegra_pll_a_out0, .value = 3},
	{ 0, 0},
};

static struct clk_mux_sel mux_pllm_pllc_pllp_clkm[] = {
	{ .input = &tegra_pll_m, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_p, .value = 2},
	{ .input = &tegra_clk_m, .value = 3},
	{ 0, 0},
};

static struct clk_mux_sel mux_pllp_pllc_pllm_clkm[] = {
	{ .input = &tegra_pll_p, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_m, .value = 2},
	{ .input = &tegra_clk_m, .value = 3},
	{ 0, 0},
};

static struct clk_mux_sel mux_plla_audio_pllp_clkm[] = {
	{.input = &tegra_pll_a, .value = 0},
	/* FIXME: no mux defined for tegra_audio
	{.input = &tegra_audio, .value = 1},*/
	{.input = &tegra_pll_p, .value = 2},
	{.input = &tegra_clk_m, .value = 3},
	{ 0, 0},
};

static struct clk_mux_sel mux_pllp_plld_pllc_clkm[] = {
	{.input = &tegra_pll_p, .value = 0},
	{.input = &tegra_pll_d_out0, .value = 1},
	{.input = &tegra_pll_c, .value = 2},
	{.input = &tegra_clk_m, .value = 3},
	{ 0, 0},
};

static struct clk_mux_sel mux_pllp_pllc_audio_clkm_clk32[] = {
	{.input = &tegra_pll_p,     .value = 0},
	{.input = &tegra_pll_c,     .value = 1},
	/* FIXME: no mux defined for tegra_audio
	{.input = &tegra_audio,     .value = 2},*/
	{.input = &tegra_clk_m,     .value = 3},
	{.input = &tegra_clk_32k,   .value = 4},
	{ 0, 0},
};

static struct clk_mux_sel mux_pllp_pllc_pllm[] = {
	{.input = &tegra_pll_p,     .value = 0},
	{.input = &tegra_pll_c,     .value = 1},
	{.input = &tegra_pll_m,     .value = 2},
	{ 0, 0},
};

static struct clk_mux_sel mux_clk_m[] = {
	{ .input = &tegra_clk_m, .value = 0},
	{ 0, 0},
};

static struct clk_mux_sel mux_pllp_out3[] = {
	{ .input = &tegra_pll_p_out3, .value = 0},
	{ 0, 0},
};

static struct clk_mux_sel mux_plld[] = {
	{ .input = &tegra_pll_d, .value = 0},
	{ 0, 0},
};

static struct clk_mux_sel mux_clk_32k[] = {
	{ .input = &tegra_clk_32k, .value = 0},
	{ 0, 0},
};

#define PERIPH_CLK(_name, _dev, _con, _clk_num, _reg, _inputs, _flags) \
	{						\
		.name      = _name,			\
		.lookup    = {				\
			.dev_id    = _dev,		\
			.con_id	   = _con,		\
		},					\
		.ops       = &tegra_periph_clk_ops,	\
		.clk_num   = _clk_num,			\
		.reg       = _reg,			\
		.inputs    = _inputs,			\
		.flags     = _flags,			\
	}

struct clk tegra_periph_clks[] = {
	PERIPH_CLK("rtc",	"rtc-tegra",		NULL,	4,	0,	mux_clk_32k,			PERIPH_NO_RESET),
	PERIPH_CLK("timer",	"timer",		NULL,	5,	0,	mux_clk_m,			0),
	PERIPH_CLK("i2s1",	"i2s.0",		NULL,	11,	0x100,	mux_plla_audio_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2s2",	"i2s.1",		NULL,	18,	0x104,	mux_plla_audio_pllp_clkm,	MUX | DIV_U71),
	/* FIXME: spdif has 2 clocks but 1 enable */
	PERIPH_CLK("spdif_out",	"spdif_out",		NULL,	10,	0x108,	mux_plla_audio_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("spdif_in",	"spdif_in",		NULL,	10,	0x10c,	mux_pllp_pllc_pllm,		MUX | DIV_U71),
	PERIPH_CLK("pwm",	"pwm",			NULL,	17,	0x110,	mux_pllp_pllc_audio_clkm_clk32,	MUX | DIV_U71),
	PERIPH_CLK("spi",	"spi",			NULL,	43,	0x114,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("xio",	"xio",			NULL,	45,	0x120,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("twc",	"twc",			NULL,	16,	0x12c,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc1",	"spi_tegra.0",		NULL,	41,	0x134,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc2",	"spi_tegra.1",		NULL,	44,	0x118,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc3",	"spi_tegra.2",		NULL,	46,	0x11c,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc4",	"spi_tegra.3",		NULL,	68,	0x1b4,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("ide",	"ide",			NULL,	25,	0x144,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("ndflash",	"tegra_nand",		NULL,	13,	0x160,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	/* FIXME: vfir shares an enable with uartb */
	PERIPH_CLK("vfir",	"vfir",			NULL,	7,	0x168,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sdmmc1",	"sdhci-tegra.0",	NULL,	14,	0x150,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sdmmc2",	"sdhci-tegra.1",	NULL,	9,	0x154,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sdmmc3",	"sdhci-tegra.2",	NULL,	69,	0x1bc,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sdmmc4",	"sdhci-tegra.3",	NULL,	15,	0x160,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("vde",	"vde",			NULL,	61,	0x1c8,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("csite",	"csite",		NULL,	73,	0x1d4,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	/* FIXME: what is la? */
	PERIPH_CLK("la",	"la",			NULL,	76,	0x1f8,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("owr",	"owr",			NULL,	71,	0x1cc,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("nor",	"nor",			NULL,	42,	0x1d0,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("mipi",	"mipi",			NULL,	50,	0x174,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2c1",	"tegra-i2c.0",		NULL,	12,	0x124,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2c2",	"tegra-i2c.1",		NULL,	54,	0x198,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2c3",	"tegra-i2c.2",		NULL,	67,	0x1b8,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("dvc",	"tegra-i2c.3",		NULL,	47,	0x128,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2c1_i2c",	"tegra-i2c.0",		"i2c",	0,	0,	mux_pllp_out3,			0),
	PERIPH_CLK("i2c2_i2c",	"tegra-i2c.1",		"i2c",	0,	0,	mux_pllp_out3,			0),
	PERIPH_CLK("i2c3_i2c",	"tegra-i2c.2",		"i2c",	0,	0,	mux_pllp_out3,			0),
	PERIPH_CLK("dvc_i2c",	"tegra-i2c.3",		"i2c",	0,	0,	mux_pllp_out3,			0),
	PERIPH_CLK("uarta",	"uart.0",		NULL,	6,	0x178,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("uartb",	"uart.1",		NULL,	7,	0x17c,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("uartc",	"uart.2",		NULL,	55,	0x1a0,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("uartd",	"uart.3",		NULL,	65,	0x1c0,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("uarte",	"uart.4",		NULL,	66,	0x1c4,	mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("3d",	"3d",			NULL,	24,	0x158,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | PERIPH_MANUAL_RESET),
	PERIPH_CLK("2d",	"2d",			NULL,	21,	0x15c,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71),
	/* FIXME: vi and vi_sensor share an enable */
	PERIPH_CLK("vi",	"vi",			NULL,	20,	0x148,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71),
	PERIPH_CLK("vi_sensor",	"vi_sensor",		NULL,	20,	0x1a8,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71),
	PERIPH_CLK("epp",	"epp",			NULL,	19,	0x16c,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71),
	PERIPH_CLK("mpe",	"mpe",			NULL,	60,	0x170,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71),
	PERIPH_CLK("host1x",	"host1x",		NULL,	28,	0x180,	mux_pllm_pllc_pllp_plla,	MUX | DIV_U71),
	/* FIXME: cve and tvo share an enable	*/
	PERIPH_CLK("cve",	"cve",			NULL,	49,	0x140,	mux_pllp_plld_pllc_clkm,	MUX | DIV_U71),
	PERIPH_CLK("tvo",	"tvo",			NULL,	49,	0x188,	mux_pllp_plld_pllc_clkm,	MUX | DIV_U71),
	PERIPH_CLK("hdmi",	"hdmi",			NULL,	51,	0x18c,	mux_pllp_plld_pllc_clkm,	MUX | DIV_U71),
	PERIPH_CLK("tvdac",	"tvdac",		NULL,	53,	0x194,	mux_pllp_plld_pllc_clkm,	MUX | DIV_U71),
	PERIPH_CLK("disp1",	"tegrafb.0",		NULL,	27,	0x138,	mux_pllp_plld_pllc_clkm,	MUX | DIV_U71),
	PERIPH_CLK("disp2",	"tegrafb.1",		NULL,	26,	0x13c,	mux_pllp_plld_pllc_clkm,	MUX | DIV_U71),
	PERIPH_CLK("usbd",	"fsl-tegra-udc",	NULL,	22,	0,	mux_clk_m,			0),
	PERIPH_CLK("usb2",	"usb.1",		NULL,	58,	0,	mux_clk_m,			0),
	PERIPH_CLK("usb3",	"usb.2",		NULL,	59,	0,	mux_clk_m,			0),
	PERIPH_CLK("emc",	"emc",			NULL,	57,	0x19c,	mux_pllm_pllc_pllp_clkm,	MUX | DIV_U71 | PERIPH_EMC_ENB),
	PERIPH_CLK("dsi",	"dsi",			NULL,	48,	0,	mux_plld,			0),
};

#define CLK_DUPLICATE(_name, _dev, _con)		\
	{						\
		.name	= _name,			\
		.lookup	= {				\
			.dev_id	= _dev,			\
			.con_id		= _con,		\
		},					\
	}

/* Some clocks may be used by different drivers depending on the board
 * configuration.  List those here to register them twice in the clock lookup
 * table under two names.
 */
struct clk_duplicate tegra_clk_duplicates[] = {
	CLK_DUPLICATE("uarta",	"tegra_uart.0",	NULL),
	CLK_DUPLICATE("uartb",	"tegra_uart.1",	NULL),
	CLK_DUPLICATE("uartc",	"tegra_uart.2",	NULL),
	CLK_DUPLICATE("uartd",	"tegra_uart.3",	NULL),
	CLK_DUPLICATE("uarte",	"tegra_uart.4",	NULL),
};

#define CLK(dev, con, ck)	\
	{			\
		.dev_id = dev,	\
		.con_id = con,	\
		.clk = ck,	\
	}

struct clk_lookup tegra_clk_lookups[] = {
	/* external root sources */
	CLK(NULL,	"32k_clk",	&tegra_clk_32k),
	CLK(NULL,	"pll_s",	&tegra_pll_s),
	CLK(NULL,	"clk_m",	&tegra_clk_m),
	CLK(NULL,	"pll_m",	&tegra_pll_m),
	CLK(NULL,	"pll_m_out1",	&tegra_pll_m_out1),
	CLK(NULL,	"pll_c",	&tegra_pll_c),
	CLK(NULL,	"pll_c_out1",	&tegra_pll_c_out1),
	CLK(NULL,	"pll_p",	&tegra_pll_p),
	CLK(NULL,	"pll_p_out1",	&tegra_pll_p_out1),
	CLK(NULL,	"pll_p_out2",	&tegra_pll_p_out2),
	CLK(NULL,	"pll_p_out3",	&tegra_pll_p_out3),
	CLK(NULL,	"pll_p_out4",	&tegra_pll_p_out4),
	CLK(NULL,	"pll_a",	&tegra_pll_a),
	CLK(NULL,	"pll_a_out0",	&tegra_pll_a_out0),
	CLK(NULL,	"pll_d",	&tegra_pll_d),
	CLK(NULL,	"pll_d_out0",	&tegra_pll_d_out0),
	CLK(NULL,	"pll_u",	&tegra_pll_u),
	CLK(NULL,	"pll_x",	&tegra_pll_x),
	CLK(NULL,	"cpu",		&tegra_clk_cpu),
	CLK(NULL,	"sys",		&tegra_clk_sys),
	CLK(NULL,	"hclk",		&tegra_clk_hclk),
	CLK(NULL,	"pclk",		&tegra_clk_pclk),
	CLK(NULL,	"clk_d",	&tegra_clk_d),
};

void __init tegra2_init_clocks(void)
{
	int i;
	struct clk_lookup *cl;
	struct clk *c;
	struct clk_duplicate *cd;

	for (i = 0; i < ARRAY_SIZE(tegra_clk_lookups); i++) {
		cl = &tegra_clk_lookups[i];
		clk_init(cl->clk);
		clkdev_add(cl);
	}

	for (i = 0; i < ARRAY_SIZE(tegra_periph_clks); i++) {
		c = &tegra_periph_clks[i];
		cl = &c->lookup;
		cl->clk = c;

		clk_init(cl->clk);
		clkdev_add(cl);
	}

	for (i = 0; i < ARRAY_SIZE(tegra_clk_duplicates); i++) {
		cd = &tegra_clk_duplicates[i];
		c = tegra_get_clock_by_name(cd->name);
		if (c) {
			cl = &cd->lookup;
			cl->clk = c;
			clkdev_add(cl);
		} else {
			pr_err("%s: Unknown duplicate clock %s\n", __func__,
				cd->name);
		}
	}
}
