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
#include <linux/clkdev.h>
#include <linux/clk.h>

#include <mach/iomap.h>
#include <mach/suspend.h>

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

#define PLL_MISC(c)			(((c)->flags & PLL_ALT_MISC_REG) ? 0x4 : 0xc)

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
		pr_err("%s: Unexpected clock autodetect value %d", __func__, clock_autodetect);
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

struct clk_ops tegra_clk_m_ops = {
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

	val = clk_readl(c->reg + SUPER_CLK_MUX);
	BUG_ON(((val & SUPER_STATE_MASK) != SUPER_STATE_RUN) &&
		((val & SUPER_STATE_MASK) != SUPER_STATE_IDLE));
	shift = ((val & SUPER_STATE_MASK) == SUPER_STATE_IDLE) ?
		SUPER_IDLE_SOURCE_SHIFT : SUPER_RUN_SOURCE_SHIFT;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			val &= ~(SUPER_SOURCE_MASK << shift);
			val |= sel->value << shift;

			if (c->refcnt)
				clk_enable(p);

			clk_writel(val, c->reg);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}
	return -EINVAL;
}

/*
 * Super clocks have "clock skippers" instead of dividers.  Dividing using
 * a clock skipper does not allow the voltage to be scaled down, so instead
 * adjust the rate of the parent clock.  This requires that the parent of a
 * super clock have no other children, otherwise the rate will change
 * underneath the other children.
 */
static int tegra2_super_clk_set_rate(struct clk *c, unsigned long rate)
{
	return clk_set_rate(c->parent, rate);
}

struct clk_ops tegra_super_ops = {
	.init			= tegra2_super_clk_init,
	.enable			= tegra2_super_clk_enable,
	.disable		= tegra2_super_clk_disable,
	.set_parent		= tegra2_super_clk_set_parent,
	.set_rate		= tegra2_super_clk_set_rate,
};

/* virtual cpu clock functions */
/* some clocks can not be stopped (cpu, memory bus) while the SoC is running.
   To change the frequency of these clocks, the parent pll may need to be
   reprogrammed, so the clock must be moved off the pll, the pll reprogrammed,
   and then the clock moved back to the pll.  To hide this sequence, a virtual
   clock handles it.
 */
static void tegra2_cpu_clk_init(struct clk *c)
{
}

static int tegra2_cpu_clk_enable(struct clk *c)
{
	return 0;
}

static void tegra2_cpu_clk_disable(struct clk *c)
{
	pr_debug("%s on clock %s\n", __func__, c->name);

	/* oops - don't disable the CPU clock! */
	BUG();
}

static int tegra2_cpu_clk_set_rate(struct clk *c, unsigned long rate)
{
	int ret;
	/*
	 * Take an extra reference to the main pll so it doesn't turn
	 * off when we move the cpu off of it
	 */
	clk_enable(c->u.cpu.main);

	ret = clk_set_parent(c->parent, c->u.cpu.backup);
	if (ret) {
		pr_err("Failed to switch cpu to clock %s\n", c->u.cpu.backup->name);
		goto out;
	}

	if (rate == clk_get_rate(c->u.cpu.backup))
		goto out;

	ret = clk_set_rate(c->u.cpu.main, rate);
	if (ret) {
		pr_err("Failed to change cpu pll to %lu\n", rate);
		goto out;
	}

	ret = clk_set_parent(c->parent, c->u.cpu.main);
	if (ret) {
		pr_err("Failed to switch cpu to clock %s\n", c->u.cpu.main->name);
		goto out;
	}

out:
	clk_disable(c->u.cpu.main);
	return ret;
}

struct clk_ops tegra_cpu_ops = {
	.init     = tegra2_cpu_clk_init,
	.enable   = tegra2_cpu_clk_enable,
	.disable  = tegra2_cpu_clk_disable,
	.set_rate = tegra2_cpu_clk_set_rate,
};

/* virtual cop clock functions. Used to acquire the fake 'cop' clock to
 * reset the COP block (i.e. AVP) */
static void tegra2_cop_clk_reset(struct clk *c, bool assert)
{
	unsigned long reg = assert ? RST_DEVICES_SET : RST_DEVICES_CLR;

	pr_debug("%s %s\n", __func__, assert ? "assert" : "deassert");
	clk_writel(1 << 1, reg);
}

struct clk_ops tegra_cop_ops = {
	.reset    = tegra2_cop_clk_reset,
};

/* bus clock functions */
static void tegra2_bus_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	c->state = ((val >> c->reg_shift) & BUS_CLK_DISABLE) ? OFF : ON;
	c->div = ((val >> c->reg_shift) & BUS_CLK_DIV_MASK) + 1;
	c->mul = 1;
}

static int tegra2_bus_clk_enable(struct clk *c)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&clock_register_lock, flags);

	val = clk_readl(c->reg);
	val &= ~(BUS_CLK_DISABLE << c->reg_shift);
	clk_writel(val, c->reg);

	spin_unlock_irqrestore(&clock_register_lock, flags);

	return 0;
}

static void tegra2_bus_clk_disable(struct clk *c)
{
	u32 val;
	unsigned long flags;

	spin_lock_irqsave(&clock_register_lock, flags);

	val = clk_readl(c->reg);
	val |= BUS_CLK_DISABLE << c->reg_shift;
	clk_writel(val, c->reg);

	spin_unlock_irqrestore(&clock_register_lock, flags);
}

static int tegra2_bus_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	unsigned long parent_rate = clk_get_rate(c->parent);
	unsigned long flags;
	int ret = -EINVAL;
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

struct clk_ops tegra_bus_ops = {
	.init			= tegra2_bus_clk_init,
	.enable			= tegra2_bus_clk_enable,
	.disable		= tegra2_bus_clk_disable,
	.set_rate		= tegra2_bus_clk_set_rate,
};

/* Blink output functions */

static void tegra2_blink_clk_init(struct clk *c)
{
	u32 val;

	val = pmc_readl(PMC_CTRL);
	c->state = (val & PMC_CTRL_BLINK_ENB) ? ON : OFF;
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
}

static int tegra2_blink_clk_enable(struct clk *c)
{
	u32 val;

	val = pmc_readl(PMC_DPD_PADS_ORIDE);
	pmc_writel(val | PMC_DPD_PADS_ORIDE_BLINK_ENB, PMC_DPD_PADS_ORIDE);

	val = pmc_readl(PMC_CTRL);
	pmc_writel(val | PMC_CTRL_BLINK_ENB, PMC_CTRL);

	return 0;
}

static void tegra2_blink_clk_disable(struct clk *c)
{
	u32 val;

	val = pmc_readl(PMC_CTRL);
	pmc_writel(val & ~PMC_CTRL_BLINK_ENB, PMC_CTRL);

	val = pmc_readl(PMC_DPD_PADS_ORIDE);
	pmc_writel(val & ~PMC_DPD_PADS_ORIDE_BLINK_ENB, PMC_DPD_PADS_ORIDE);
}

static int tegra2_blink_clk_set_rate(struct clk *c, unsigned long rate)
{
	unsigned long parent_rate = clk_get_rate(c->parent);
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

struct clk_ops tegra_blink_clk_ops = {
	.init			= &tegra2_blink_clk_init,
	.enable			= &tegra2_blink_clk_enable,
	.disable		= &tegra2_blink_clk_disable,
	.set_rate		= &tegra2_blink_clk_set_rate,
};

/* PLL Functions */
static int tegra2_pll_clk_wait_for_lock(struct clk *c)
{
	udelay(c->u.pll.lock_delay);

	return 0;
}

static void tegra2_pll_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg + PLL_BASE);

	c->state = (val & PLL_BASE_ENABLE) ? ON : OFF;

	if (c->flags & PLL_FIXED && !(val & PLL_BASE_OVERRIDE)) {
		pr_warning("Clock %s has unknown fixed frequency\n", c->name);
		c->mul = 1;
		c->div = 1;
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
}

static int tegra2_pll_clk_enable(struct clk *c)
{
	u32 val;
	pr_debug("%s on clock %s\n", __func__, c->name);

	val = clk_readl(c->reg + PLL_BASE);
	val &= ~PLL_BASE_BYPASS;
	val |= PLL_BASE_ENABLE;
	clk_writel(val, c->reg + PLL_BASE);

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
	const struct clk_pll_freq_table *sel;

	pr_debug("%s: %s %lu\n", __func__, c->name, rate);

	input_rate = clk_get_rate(c->parent);
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
				tegra2_pll_clk_enable(c);

			return 0;
		}
	}
	return -EINVAL;
}

struct clk_ops tegra_pll_ops = {
	.init			= tegra2_pll_clk_init,
	.enable			= tegra2_pll_clk_enable,
	.disable		= tegra2_pll_clk_disable,
	.set_rate		= tegra2_pll_clk_set_rate,
};

static void tegra2_pllx_clk_init(struct clk *c)
{
	tegra2_pll_clk_init(c);

	if (tegra_sku_id == 7)
		c->max_rate = 750000000;
}

struct clk_ops tegra_pllx_ops = {
	.init     = tegra2_pllx_clk_init,
	.enable   = tegra2_pll_clk_enable,
	.disable  = tegra2_pll_clk_disable,
	.set_rate = tegra2_pll_clk_set_rate,
};

static int tegra2_plle_clk_enable(struct clk *c)
{
	u32 val;

	pr_debug("%s on clock %s\n", __func__, c->name);

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
	.init       = tegra2_pll_clk_init,
	.enable     = tegra2_plle_clk_enable,
	.set_rate   = tegra2_pll_clk_set_rate,
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
}

static int tegra2_pll_div_clk_enable(struct clk *c)
{
	u32 val;
	u32 new_val;
	unsigned long flags;

	pr_debug("%s: %s\n", __func__, c->name);
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

static void tegra2_pll_div_clk_disable(struct clk *c)
{
	u32 val;
	u32 new_val;
	unsigned long flags;

	pr_debug("%s: %s\n", __func__, c->name);
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

static int tegra2_pll_div_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	u32 new_val;
	int divider_u71;
	unsigned long parent_rate = clk_get_rate(c->parent);
	unsigned long flags;

	pr_debug("%s: %s %lu\n", __func__, c->name, rate);
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

static long tegra2_pll_div_clk_round_rate(struct clk *c, unsigned long rate)
{
	int divider;
	unsigned long parent_rate = clk_get_rate(c->parent);
	pr_debug("%s: %s %lu\n", __func__, c->name, rate);

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
	.init			= tegra2_pll_div_clk_init,
	.enable			= tegra2_pll_div_clk_enable,
	.disable		= tegra2_pll_div_clk_disable,
	.set_rate		= tegra2_pll_div_clk_set_rate,
	.round_rate		= tegra2_pll_div_clk_round_rate,
};

/* Periph clk ops */

static void tegra2_periph_clk_init(struct clk *c)
{
	u32 val = clk_readl(c->reg);
	const struct clk_mux_sel *mux = NULL;
	const struct clk_mux_sel *sel;
	u32 shift;
	u32 mask;

	if (c->flags & MUX_PWM) {
		shift = PERIPH_CLK_SOURCE_PWM_SHIFT;
		mask = PERIPH_CLK_SOURCE_PWM_MASK;
	} else {
		shift = PERIPH_CLK_SOURCE_SHIFT;
		mask = PERIPH_CLK_SOURCE_MASK;
	}

	if (c->flags & MUX) {
		for (sel = c->inputs; sel->input != NULL; sel++) {
			if ((val & mask) >> shift == sel->value)
				mux = sel;
		}
		BUG_ON(!mux);

		c->parent = mux->input;
	} else {
		c->parent = c->inputs[0].input;
	}

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
	}

	c->state = ON;

	if (!c->u.periph.clk_num)
		return;

	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;

	if (!(c->flags & PERIPH_NO_RESET))
		if (clk_readl(RST_DEVICES + PERIPH_CLK_TO_ENB_REG(c)) &
				PERIPH_CLK_TO_ENB_BIT(c))
			c->state = OFF;
}

static int tegra2_periph_clk_enable(struct clk *c)
{
	u32 val;
	unsigned long flags;
	int refcount;
	pr_debug("%s on clock %s\n", __func__, c->name);

	if (!c->u.periph.clk_num)
		return 0;

	spin_lock_irqsave(&clock_register_lock, flags);

	refcount = tegra_periph_clk_enable_refcount[c->u.periph.clk_num]++;

	if (refcount > 1)
		goto out;

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

out:
	spin_unlock_irqrestore(&clock_register_lock, flags);

	return 0;
}

static void tegra2_periph_clk_disable(struct clk *c)
{
	unsigned long flags;

	pr_debug("%s on clock %s\n", __func__, c->name);

	if (!c->u.periph.clk_num)
		return;

	spin_lock_irqsave(&clock_register_lock, flags);

	if (c->refcnt)
		tegra_periph_clk_enable_refcount[c->u.periph.clk_num]--;

	if (tegra_periph_clk_enable_refcount[c->u.periph.clk_num] == 0)
		clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
			CLK_OUT_ENB_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));

	spin_unlock_irqrestore(&clock_register_lock, flags);
}

static void tegra2_periph_clk_reset(struct clk *c, bool assert)
{
	unsigned long base = assert ? RST_DEVICES_SET : RST_DEVICES_CLR;

	pr_debug("%s %s on clock %s\n", __func__,
		 assert ? "assert" : "deassert", c->name);

	BUG_ON(!c->u.periph.clk_num);

	if (!(c->flags & PERIPH_NO_RESET))
		clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
			   base + PERIPH_CLK_TO_ENB_SET_REG(c));
}

static int tegra2_periph_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	u32 mask, shift;

	pr_debug("%s: %s %s\n", __func__, c->name, p->name);

	if (c->flags & MUX_PWM) {
		shift = PERIPH_CLK_SOURCE_PWM_SHIFT;
		mask = PERIPH_CLK_SOURCE_PWM_MASK;
	} else {
		shift = PERIPH_CLK_SOURCE_SHIFT;
		mask = PERIPH_CLK_SOURCE_MASK;
	}

	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			val = clk_readl(c->reg);
			val &= ~mask;
			val |= (sel->value) << shift;

			if (c->refcnt)
				clk_enable(p);

			clk_writel(val, c->reg);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}

	return -EINVAL;
}

static int tegra2_periph_clk_set_rate(struct clk *c, unsigned long rate)
{
	u32 val;
	int divider;
	unsigned long parent_rate = clk_get_rate(c->parent);

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

static long tegra2_periph_clk_round_rate(struct clk *c,
	unsigned long rate)
{
	int divider;
	unsigned long parent_rate = clk_get_rate(c->parent);
	pr_debug("%s: %s %lu\n", __func__, c->name, rate);

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
	.init			= &tegra2_periph_clk_init,
	.enable			= &tegra2_periph_clk_enable,
	.disable		= &tegra2_periph_clk_disable,
	.set_parent		= &tegra2_periph_clk_set_parent,
	.set_rate		= &tegra2_periph_clk_set_rate,
	.round_rate		= &tegra2_periph_clk_round_rate,
	.reset			= &tegra2_periph_clk_reset,
};

/* The SDMMC controllers have extra bits in the clock source register that
 * adjust the delay between the clock and data to compenstate for delays
 * on the PCB. */
void tegra2_sdmmc_tap_delay(struct clk *c, int delay)
{
	u32 reg;
	unsigned long flags;

	spin_lock_irqsave(&c->spinlock, flags);

	delay = clamp(delay, 0, 15);
	reg = clk_readl(c->reg);
	reg &= ~SDMMC_CLK_INT_FB_DLY_MASK;
	reg |= SDMMC_CLK_INT_FB_SEL;
	reg |= delay << SDMMC_CLK_INT_FB_DLY_SHIFT;
	clk_writel(reg, c->reg);

	spin_unlock_irqrestore(&c->spinlock, flags);
}

/* External memory controller clock ops */
static void tegra2_emc_clk_init(struct clk *c)
{
	tegra2_periph_clk_init(c);
	c->max_rate = clk_get_rate_locked(c);
}

static long tegra2_emc_clk_round_rate(struct clk *c, unsigned long rate)
{
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
	clk_rate = tegra2_periph_clk_round_rate(c, emc_rate);

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

static int tegra2_emc_clk_set_rate(struct clk *c, unsigned long rate)
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

	ret = tegra2_periph_clk_set_rate(c, rate);
	udelay(1);

	return ret;
}

struct clk_ops tegra_emc_clk_ops = {
	.init			= &tegra2_emc_clk_init,
	.enable			= &tegra2_periph_clk_enable,
	.disable		= &tegra2_periph_clk_disable,
	.set_parent		= &tegra2_periph_clk_set_parent,
	.set_rate		= &tegra2_emc_clk_set_rate,
	.round_rate		= &tegra2_emc_clk_round_rate,
	.reset			= &tegra2_periph_clk_reset,
};

/* Clock doubler ops */
static void tegra2_clk_double_init(struct clk *c)
{
	c->mul = 2;
	c->div = 1;
	c->state = ON;

	if (!c->u.periph.clk_num)
		return;

	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;
};

static int tegra2_clk_double_set_rate(struct clk *c, unsigned long rate)
{
	if (rate != 2 * clk_get_rate(c->parent))
		return -EINVAL;
	c->mul = 2;
	c->div = 1;
	return 0;
}

struct clk_ops tegra_clk_double_ops = {
	.init			= &tegra2_clk_double_init,
	.enable			= &tegra2_periph_clk_enable,
	.disable		= &tegra2_periph_clk_disable,
	.set_rate		= &tegra2_clk_double_set_rate,
};

/* Audio sync clock ops */
static void tegra2_audio_sync_clk_init(struct clk *c)
{
	int source;
	const struct clk_mux_sel *sel;
	u32 val = clk_readl(c->reg);
	c->state = (val & (1<<4)) ? OFF : ON;
	source = val & 0xf;
	for (sel = c->inputs; sel->input != NULL; sel++)
		if (sel->value == source)
			break;
	BUG_ON(sel->input == NULL);
	c->parent = sel->input;
}

static int tegra2_audio_sync_clk_enable(struct clk *c)
{
	clk_writel(0, c->reg);
	return 0;
}

static void tegra2_audio_sync_clk_disable(struct clk *c)
{
	clk_writel(1, c->reg);
}

static int tegra2_audio_sync_clk_set_parent(struct clk *c, struct clk *p)
{
	u32 val;
	const struct clk_mux_sel *sel;
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			val = clk_readl(c->reg);
			val &= ~0xf;
			val |= sel->value;

			if (c->refcnt)
				clk_enable(p);

			clk_writel(val, c->reg);

			if (c->refcnt && c->parent)
				clk_disable(c->parent);

			clk_reparent(c, p);
			return 0;
		}
	}

	return -EINVAL;
}

struct clk_ops tegra_audio_sync_clk_ops = {
	.init       = tegra2_audio_sync_clk_init,
	.enable     = tegra2_audio_sync_clk_enable,
	.disable    = tegra2_audio_sync_clk_disable,
	.set_parent = tegra2_audio_sync_clk_set_parent,
};

/* cdev1 and cdev2 (dap_mclk1 and dap_mclk2) ops */

static void tegra2_cdev_clk_init(struct clk *c)
{
	/* We could un-tristate the cdev1 or cdev2 pingroup here; this is
	 * currently done in the pinmux code. */
	c->state = ON;

	BUG_ON(!c->u.periph.clk_num);

	if (!(clk_readl(CLK_OUT_ENB + PERIPH_CLK_TO_ENB_REG(c)) &
			PERIPH_CLK_TO_ENB_BIT(c)))
		c->state = OFF;
}

static int tegra2_cdev_clk_enable(struct clk *c)
{
	BUG_ON(!c->u.periph.clk_num);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_SET + PERIPH_CLK_TO_ENB_SET_REG(c));
	return 0;
}

static void tegra2_cdev_clk_disable(struct clk *c)
{
	BUG_ON(!c->u.periph.clk_num);

	clk_writel(PERIPH_CLK_TO_ENB_BIT(c),
		CLK_OUT_ENB_CLR + PERIPH_CLK_TO_ENB_SET_REG(c));
}

struct clk_ops tegra_cdev_clk_ops = {
	.init			= &tegra2_cdev_clk_init,
	.enable			= &tegra2_cdev_clk_enable,
	.disable		= &tegra2_cdev_clk_disable,
};

/* shared bus ops */
/*
 * Some clocks may have multiple downstream users that need to request a
 * higher clock rate.  Shared bus clocks provide a unique shared_bus_user
 * clock to each user.  The frequency of the bus is set to the highest
 * enabled shared_bus_user clock, with a minimum value set by the
 * shared bus.
 */
static int tegra_clk_shared_bus_update(struct clk *bus)
{
	struct clk *c;
	unsigned long rate = bus->min_rate;

	list_for_each_entry(c, &bus->shared_bus_list, u.shared_bus_user.node)
		if (c->u.shared_bus_user.enabled)
			rate = max(c->u.shared_bus_user.rate, rate);

	if (rate == clk_get_rate_locked(bus))
		return 0;

	return clk_set_rate_locked(bus, rate);
};

static void tegra_clk_shared_bus_init(struct clk *c)
{
	unsigned long flags;

	c->max_rate = c->parent->max_rate;
	c->u.shared_bus_user.rate = c->parent->max_rate;
	c->state = OFF;
	c->set = true;

	spin_lock_irqsave(&c->parent->spinlock, flags);

	list_add_tail(&c->u.shared_bus_user.node,
		&c->parent->shared_bus_list);

	spin_unlock_irqrestore(&c->parent->spinlock, flags);
}

static int tegra_clk_shared_bus_set_rate(struct clk *c, unsigned long rate)
{
	unsigned long flags;
	int ret;
	long new_rate = rate;

	new_rate = clk_round_rate(c->parent, new_rate);
	if (new_rate < 0)
		return new_rate;

	spin_lock_irqsave(&c->parent->spinlock, flags);

	c->u.shared_bus_user.rate = new_rate;
	ret = tegra_clk_shared_bus_update(c->parent);

	spin_unlock_irqrestore(&c->parent->spinlock, flags);

	return ret;
}

static long tegra_clk_shared_bus_round_rate(struct clk *c, unsigned long rate)
{
	return clk_round_rate(c->parent, rate);
}

static int tegra_clk_shared_bus_enable(struct clk *c)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&c->parent->spinlock, flags);

	c->u.shared_bus_user.enabled = true;
	ret = tegra_clk_shared_bus_update(c->parent);

	spin_unlock_irqrestore(&c->parent->spinlock, flags);

	return ret;
}

static void tegra_clk_shared_bus_disable(struct clk *c)
{
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&c->parent->spinlock, flags);

	c->u.shared_bus_user.enabled = false;
	ret = tegra_clk_shared_bus_update(c->parent);
	WARN_ON_ONCE(ret);

	spin_unlock_irqrestore(&c->parent->spinlock, flags);
}

struct clk_ops tegra_clk_shared_bus_ops = {
	.init = tegra_clk_shared_bus_init,
	.enable = tegra_clk_shared_bus_enable,
	.disable = tegra_clk_shared_bus_disable,
	.set_rate = tegra_clk_shared_bus_set_rate,
	.round_rate = tegra_clk_shared_bus_round_rate,
};
