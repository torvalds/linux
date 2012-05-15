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

	if (divider_u16 - 1 > 255)
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

static struct clk_ops tegra_super_ops = {
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

static struct clk_ops tegra_cpu_ops = {
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

static struct clk_ops tegra_cop_ops = {
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

static struct clk_ops tegra_bus_ops = {
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

static struct clk_ops tegra_blink_clk_ops = {
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

static struct clk_ops tegra_pll_ops = {
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

static struct clk_ops tegra_pllx_ops = {
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

static struct clk_ops tegra_plle_ops = {
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

static struct clk_ops tegra_pll_div_ops = {
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
	pr_debug("%s: %s %s\n", __func__, c->name, p->name);
	for (sel = c->inputs; sel->input != NULL; sel++) {
		if (sel->input == p) {
			val = clk_readl(c->reg);
			val &= ~PERIPH_CLK_SOURCE_MASK;
			val |= (sel->value) << PERIPH_CLK_SOURCE_SHIFT;

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

static struct clk_ops tegra_periph_clk_ops = {
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

static struct clk_ops tegra_emc_clk_ops = {
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

static struct clk_ops tegra_clk_double_ops = {
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

static struct clk_ops tegra_audio_sync_clk_ops = {
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

static struct clk_ops tegra_cdev_clk_ops = {
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

static struct clk_ops tegra_clk_shared_bus_ops = {
	.init = tegra_clk_shared_bus_init,
	.enable = tegra_clk_shared_bus_enable,
	.disable = tegra_clk_shared_bus_disable,
	.set_rate = tegra_clk_shared_bus_set_rate,
	.round_rate = tegra_clk_shared_bus_round_rate,
};


/* Clock definitions */
static struct clk tegra_clk_32k = {
	.name = "clk_32k",
	.rate = 32768,
	.ops  = NULL,
	.max_rate = 32768,
};

static struct clk_pll_freq_table tegra_pll_s_freq_table[] = {
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
	.parent    = &tegra_clk_32k,
	.max_rate  = 26000000,
	.reg       = 0xf0,
	.u.pll = {
		.input_min = 32768,
		.input_max = 32768,
		.cf_min    = 0, /* FIXME */
		.cf_max    = 0, /* FIXME */
		.vco_min   = 12000000,
		.vco_max   = 26000000,
		.freq_table = tegra_pll_s_freq_table,
		.lock_delay = 300,
	},
};

static struct clk_mux_sel tegra_clk_m_sel[] = {
	{ .input = &tegra_clk_32k, .value = 0},
	{ .input = &tegra_pll_s,  .value = 1},
	{ NULL , 0},
};

static struct clk tegra_clk_m = {
	.name      = "clk_m",
	.flags     = ENABLE_ON_INIT,
	.ops       = &tegra_clk_m_ops,
	.inputs    = tegra_clk_m_sel,
	.reg       = 0x1fc,
	.reg_shift = 28,
	.max_rate  = 26000000,
};

static struct clk_pll_freq_table tegra_pll_c_freq_table[] = {
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_c = {
	.name      = "pll_c",
	.flags	   = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0x80,
	.parent    = &tegra_clk_m,
	.max_rate  = 600000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1400000000,
		.freq_table = tegra_pll_c_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_c_out1 = {
	.name      = "pll_c_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_c,
	.reg       = 0x84,
	.reg_shift = 0,
	.max_rate  = 600000000,
};

static struct clk_pll_freq_table tegra_pll_m_freq_table[] = {
	{ 12000000, 666000000, 666, 12, 1, 8},
	{ 13000000, 666000000, 666, 13, 1, 8},
	{ 19200000, 666000000, 555, 16, 1, 8},
	{ 26000000, 666000000, 666, 26, 1, 8},
	{ 12000000, 600000000, 600, 12, 1, 8},
	{ 13000000, 600000000, 600, 13, 1, 8},
	{ 19200000, 600000000, 375, 12, 1, 6},
	{ 26000000, 600000000, 600, 26, 1, 8},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_m = {
	.name      = "pll_m",
	.flags     = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0x90,
	.parent    = &tegra_clk_m,
	.max_rate  = 800000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1200000000,
		.freq_table = tegra_pll_m_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_m_out1 = {
	.name      = "pll_m_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_m,
	.reg       = 0x94,
	.reg_shift = 0,
	.max_rate  = 600000000,
};

static struct clk_pll_freq_table tegra_pll_p_freq_table[] = {
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
	.parent    = &tegra_clk_m,
	.max_rate  = 432000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1400000000,
		.freq_table = tegra_pll_p_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_p_out1 = {
	.name      = "pll_p_out1",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa4,
	.reg_shift = 0,
	.max_rate  = 432000000,
};

static struct clk tegra_pll_p_out2 = {
	.name      = "pll_p_out2",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa4,
	.reg_shift = 16,
	.max_rate  = 432000000,
};

static struct clk tegra_pll_p_out3 = {
	.name      = "pll_p_out3",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa8,
	.reg_shift = 0,
	.max_rate  = 432000000,
};

static struct clk tegra_pll_p_out4 = {
	.name      = "pll_p_out4",
	.ops       = &tegra_pll_div_ops,
	.flags     = ENABLE_ON_INIT | DIV_U71 | DIV_U71_FIXED,
	.parent    = &tegra_pll_p,
	.reg       = 0xa8,
	.reg_shift = 16,
	.max_rate  = 432000000,
};

static struct clk_pll_freq_table tegra_pll_a_freq_table[] = {
	{ 28800000, 56448000, 49, 25, 1, 1},
	{ 28800000, 73728000, 64, 25, 1, 1},
	{ 28800000, 24000000,  5,  6, 1, 1},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_a = {
	.name      = "pll_a",
	.flags     = PLL_HAS_CPCON,
	.ops       = &tegra_pll_ops,
	.reg       = 0xb0,
	.parent    = &tegra_pll_p_out1,
	.max_rate  = 73728000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1400000000,
		.freq_table = tegra_pll_a_freq_table,
		.lock_delay = 300,
	},
};

static struct clk tegra_pll_a_out0 = {
	.name      = "pll_a_out0",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_U71,
	.parent    = &tegra_pll_a,
	.reg       = 0xb4,
	.reg_shift = 0,
	.max_rate  = 73728000,
};

static struct clk_pll_freq_table tegra_pll_d_freq_table[] = {
	{ 12000000, 216000000, 216, 12, 1, 4},
	{ 13000000, 216000000, 216, 13, 1, 4},
	{ 19200000, 216000000, 135, 12, 1, 3},
	{ 26000000, 216000000, 216, 26, 1, 4},

	{ 12000000, 594000000, 594, 12, 1, 8},
	{ 13000000, 594000000, 594, 13, 1, 8},
	{ 19200000, 594000000, 495, 16, 1, 8},
	{ 26000000, 594000000, 594, 26, 1, 8},

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
	.parent    = &tegra_clk_m,
	.max_rate  = 1000000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 40000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 40000000,
		.vco_max   = 1000000000,
		.freq_table = tegra_pll_d_freq_table,
		.lock_delay = 1000,
	},
};

static struct clk tegra_pll_d_out0 = {
	.name      = "pll_d_out0",
	.ops       = &tegra_pll_div_ops,
	.flags     = DIV_2 | PLLD,
	.parent    = &tegra_pll_d,
	.max_rate  = 500000000,
};

static struct clk_pll_freq_table tegra_pll_u_freq_table[] = {
	{ 12000000, 480000000, 960, 12, 2, 0},
	{ 13000000, 480000000, 960, 13, 2, 0},
	{ 19200000, 480000000, 200, 4,  2, 0},
	{ 26000000, 480000000, 960, 26, 2, 0},
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_u = {
	.name      = "pll_u",
	.flags     = PLLU,
	.ops       = &tegra_pll_ops,
	.reg       = 0xc0,
	.parent    = &tegra_clk_m,
	.max_rate  = 480000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 40000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 480000000,
		.vco_max   = 960000000,
		.freq_table = tegra_pll_u_freq_table,
		.lock_delay = 1000,
	},
};

static struct clk_pll_freq_table tegra_pll_x_freq_table[] = {
	/* 1 GHz */
	{ 12000000, 1000000000, 1000, 12, 1, 12},
	{ 13000000, 1000000000, 1000, 13, 1, 12},
	{ 19200000, 1000000000, 625,  12, 1, 8},
	{ 26000000, 1000000000, 1000, 26, 1, 12},

	/* 912 MHz */
	{ 12000000, 912000000,  912,  12, 1, 12},
	{ 13000000, 912000000,  912,  13, 1, 12},
	{ 19200000, 912000000,  760,  16, 1, 8},
	{ 26000000, 912000000,  912,  26, 1, 12},

	/* 816 MHz */
	{ 12000000, 816000000,  816,  12, 1, 12},
	{ 13000000, 816000000,  816,  13, 1, 12},
	{ 19200000, 816000000,  680,  16, 1, 8},
	{ 26000000, 816000000,  816,  26, 1, 12},

	/* 760 MHz */
	{ 12000000, 760000000,  760,  12, 1, 12},
	{ 13000000, 760000000,  760,  13, 1, 12},
	{ 19200000, 760000000,  950,  24, 1, 8},
	{ 26000000, 760000000,  760,  26, 1, 12},

	/* 608 MHz */
	{ 12000000, 608000000,  608,  12, 1, 12},
	{ 13000000, 608000000,  608,  13, 1, 12},
	{ 19200000, 608000000,  380,  12, 1, 8},
	{ 26000000, 608000000,  608,  26, 1, 12},

	/* 456 MHz */
	{ 12000000, 456000000,  456,  12, 1, 12},
	{ 13000000, 456000000,  456,  13, 1, 12},
	{ 19200000, 456000000,  380,  16, 1, 8},
	{ 26000000, 456000000,  456,  26, 1, 12},

	/* 312 MHz */
	{ 12000000, 312000000,  312,  12, 1, 12},
	{ 13000000, 312000000,  312,  13, 1, 12},
	{ 19200000, 312000000,  260,  16, 1, 8},
	{ 26000000, 312000000,  312,  26, 1, 12},

	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_x = {
	.name      = "pll_x",
	.flags     = PLL_HAS_CPCON | PLL_ALT_MISC_REG,
	.ops       = &tegra_pllx_ops,
	.reg       = 0xe0,
	.parent    = &tegra_clk_m,
	.max_rate  = 1000000000,
	.u.pll = {
		.input_min = 2000000,
		.input_max = 31000000,
		.cf_min    = 1000000,
		.cf_max    = 6000000,
		.vco_min   = 20000000,
		.vco_max   = 1200000000,
		.freq_table = tegra_pll_x_freq_table,
		.lock_delay = 300,
	},
};

static struct clk_pll_freq_table tegra_pll_e_freq_table[] = {
	{ 12000000, 100000000,  200,  24, 1, 0 },
	{ 0, 0, 0, 0, 0, 0 },
};

static struct clk tegra_pll_e = {
	.name      = "pll_e",
	.flags	   = PLL_ALT_MISC_REG,
	.ops       = &tegra_plle_ops,
	.parent    = &tegra_clk_m,
	.reg       = 0xe8,
	.max_rate  = 100000000,
	.u.pll = {
		.input_min = 12000000,
		.input_max = 12000000,
		.freq_table = tegra_pll_e_freq_table,
	},
};

static struct clk tegra_clk_d = {
	.name      = "clk_d",
	.flags     = PERIPH_NO_RESET,
	.ops       = &tegra_clk_double_ops,
	.reg       = 0x34,
	.reg_shift = 12,
	.parent    = &tegra_clk_m,
	.max_rate  = 52000000,
	.u.periph  = {
		.clk_num = 90,
	},
};

/* dap_mclk1, belongs to the cdev1 pingroup. */
static struct clk tegra_clk_cdev1 = {
	.name      = "cdev1",
	.ops       = &tegra_cdev_clk_ops,
	.rate      = 26000000,
	.max_rate  = 26000000,
	.u.periph  = {
		.clk_num = 94,
	},
};

/* dap_mclk2, belongs to the cdev2 pingroup. */
static struct clk tegra_clk_cdev2 = {
	.name      = "cdev2",
	.ops       = &tegra_cdev_clk_ops,
	.rate      = 26000000,
	.max_rate  = 26000000,
	.u.periph  = {
		.clk_num   = 93,
	},
};

/* initialized before peripheral clocks */
static struct clk_mux_sel mux_audio_sync_clk[8+1];
static const struct audio_sources {
	const char *name;
	int value;
} mux_audio_sync_clk_sources[] = {
	{ .name = "spdif_in", .value = 0 },
	{ .name = "i2s1", .value = 1 },
	{ .name = "i2s2", .value = 2 },
	{ .name = "pll_a_out0", .value = 4 },
#if 0 /* FIXME: not implemented */
	{ .name = "ac97", .value = 3 },
	{ .name = "ext_audio_clk2", .value = 5 },
	{ .name = "ext_audio_clk1", .value = 6 },
	{ .name = "ext_vimclk", .value = 7 },
#endif
	{ NULL, 0 }
};

static struct clk tegra_clk_audio = {
	.name      = "audio",
	.inputs    = mux_audio_sync_clk,
	.reg       = 0x38,
	.max_rate  = 73728000,
	.ops       = &tegra_audio_sync_clk_ops
};

static struct clk tegra_clk_audio_2x = {
	.name      = "audio_2x",
	.flags     = PERIPH_NO_RESET,
	.max_rate  = 48000000,
	.ops       = &tegra_clk_double_ops,
	.reg       = 0x34,
	.reg_shift = 8,
	.parent    = &tegra_clk_audio,
	.u.periph = {
		.clk_num = 89,
	},
};

static struct clk_lookup tegra_audio_clk_lookups[] = {
	{ .con_id = "audio", .clk = &tegra_clk_audio },
	{ .con_id = "audio_2x", .clk = &tegra_clk_audio_2x }
};

/* This is called after peripheral clocks are initialized, as the
 * audio_sync clock depends on some of the peripheral clocks.
 */

static void init_audio_sync_clock_mux(void)
{
	int i;
	struct clk_mux_sel *sel = mux_audio_sync_clk;
	const struct audio_sources *src = mux_audio_sync_clk_sources;
	struct clk_lookup *lookup;

	for (i = 0; src->name; i++, sel++, src++) {
		sel->input = tegra_get_clock_by_name(src->name);
		if (!sel->input)
			pr_err("%s: could not find clk %s\n", __func__,
				src->name);
		sel->value = src->value;
	}

	lookup = tegra_audio_clk_lookups;
	for (i = 0; i < ARRAY_SIZE(tegra_audio_clk_lookups); i++, lookup++) {
		clk_init(lookup->clk);
		clkdev_add(lookup);
	}
}

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
	{ NULL, 0},
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
	{ NULL, 0},
};

static struct clk tegra_clk_cclk = {
	.name	= "cclk",
	.inputs	= mux_cclk,
	.reg	= 0x20,
	.ops	= &tegra_super_ops,
	.max_rate = 1000000000,
};

static struct clk tegra_clk_sclk = {
	.name	= "sclk",
	.inputs	= mux_sclk,
	.reg	= 0x28,
	.ops	= &tegra_super_ops,
	.max_rate = 240000000,
	.min_rate = 120000000,
};

static struct clk tegra_clk_virtual_cpu = {
	.name      = "cpu",
	.parent    = &tegra_clk_cclk,
	.ops       = &tegra_cpu_ops,
	.max_rate  = 1000000000,
	.u.cpu = {
		.main      = &tegra_pll_x,
		.backup    = &tegra_pll_p,
	},
};

static struct clk tegra_clk_cop = {
	.name      = "cop",
	.parent    = &tegra_clk_sclk,
	.ops       = &tegra_cop_ops,
	.max_rate  = 240000000,
};

static struct clk tegra_clk_hclk = {
	.name		= "hclk",
	.flags		= DIV_BUS,
	.parent		= &tegra_clk_sclk,
	.reg		= 0x30,
	.reg_shift	= 4,
	.ops		= &tegra_bus_ops,
	.max_rate       = 240000000,
};

static struct clk tegra_clk_pclk = {
	.name		= "pclk",
	.flags		= DIV_BUS,
	.parent		= &tegra_clk_hclk,
	.reg		= 0x30,
	.reg_shift	= 0,
	.ops		= &tegra_bus_ops,
	.max_rate       = 120000000,
};

static struct clk tegra_clk_blink = {
	.name		= "blink",
	.parent		= &tegra_clk_32k,
	.reg		= 0x40,
	.ops		= &tegra_blink_clk_ops,
	.max_rate	= 32768,
};

static struct clk_mux_sel mux_pllm_pllc_pllp_plla[] = {
	{ .input = &tegra_pll_m, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_p, .value = 2},
	{ .input = &tegra_pll_a_out0, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllm_pllc_pllp_clkm[] = {
	{ .input = &tegra_pll_m, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_p, .value = 2},
	{ .input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_pllc_pllm_clkm[] = {
	{ .input = &tegra_pll_p, .value = 0},
	{ .input = &tegra_pll_c, .value = 1},
	{ .input = &tegra_pll_m, .value = 2},
	{ .input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllaout0_audio2x_pllp_clkm[] = {
	{.input = &tegra_pll_a_out0, .value = 0},
	{.input = &tegra_clk_audio_2x, .value = 1},
	{.input = &tegra_pll_p, .value = 2},
	{.input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_plld_pllc_clkm[] = {
	{.input = &tegra_pll_p, .value = 0},
	{.input = &tegra_pll_d_out0, .value = 1},
	{.input = &tegra_pll_c, .value = 2},
	{.input = &tegra_clk_m, .value = 3},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_pllc_audio_clkm_clk32[] = {
	{.input = &tegra_pll_p,     .value = 0},
	{.input = &tegra_pll_c,     .value = 1},
	{.input = &tegra_clk_audio,     .value = 2},
	{.input = &tegra_clk_m,     .value = 3},
	{.input = &tegra_clk_32k,   .value = 4},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_pllc_pllm[] = {
	{.input = &tegra_pll_p,     .value = 0},
	{.input = &tegra_pll_c,     .value = 1},
	{.input = &tegra_pll_m,     .value = 2},
	{ NULL, 0},
};

static struct clk_mux_sel mux_clk_m[] = {
	{ .input = &tegra_clk_m, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pllp_out3[] = {
	{ .input = &tegra_pll_p_out3, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_plld[] = {
	{ .input = &tegra_pll_d, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_clk_32k[] = {
	{ .input = &tegra_clk_32k, .value = 0},
	{ NULL, 0},
};

static struct clk_mux_sel mux_pclk[] = {
	{ .input = &tegra_clk_pclk, .value = 0},
	{ NULL, 0},
};

static struct clk tegra_clk_emc = {
	.name = "emc",
	.ops = &tegra_emc_clk_ops,
	.reg = 0x19c,
	.max_rate = 800000000,
	.inputs = mux_pllm_pllc_pllp_clkm,
	.flags = MUX | DIV_U71 | PERIPH_EMC_ENB,
	.u.periph = {
		.clk_num = 57,
	},
};

#define PERIPH_CLK(_name, _dev, _con, _clk_num, _reg, _max, _inputs, _flags) \
	{						\
		.name      = _name,			\
		.lookup    = {				\
			.dev_id    = _dev,		\
			.con_id	   = _con,		\
		},					\
		.ops       = &tegra_periph_clk_ops,	\
		.reg       = _reg,			\
		.inputs    = _inputs,			\
		.flags     = _flags,			\
		.max_rate  = _max,			\
		.u.periph = {				\
			.clk_num   = _clk_num,		\
		},					\
	}

#define SHARED_CLK(_name, _dev, _con, _parent)		\
	{						\
		.name      = _name,			\
		.lookup    = {				\
			.dev_id    = _dev,		\
			.con_id    = _con,		\
		},					\
		.ops       = &tegra_clk_shared_bus_ops,	\
		.parent = _parent,			\
	}

static struct clk tegra_list_clks[] = {
	PERIPH_CLK("apbdma",	"tegra-dma",		NULL,	34,	0,	108000000, mux_pclk,			0),
	PERIPH_CLK("rtc",	"rtc-tegra",		NULL,	4,	0,	32768,     mux_clk_32k,			PERIPH_NO_RESET),
	PERIPH_CLK("timer",	"timer",		NULL,	5,	0,	26000000,  mux_clk_m,			0),
	PERIPH_CLK("i2s1",	"tegra-i2s.0",		NULL,	11,	0x100,	26000000,  mux_pllaout0_audio2x_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("i2s2",	"tegra-i2s.1",		NULL,	18,	0x104,	26000000,  mux_pllaout0_audio2x_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("spdif_out",	"spdif_out",		NULL,	10,	0x108,	100000000, mux_pllaout0_audio2x_pllp_clkm,	MUX | DIV_U71),
	PERIPH_CLK("spdif_in",	"spdif_in",		NULL,	10,	0x10c,	100000000, mux_pllp_pllc_pllm,		MUX | DIV_U71),
	PERIPH_CLK("pwm",	"pwm",			NULL,	17,	0x110,	432000000, mux_pllp_pllc_audio_clkm_clk32,	MUX | DIV_U71),
	PERIPH_CLK("spi",	"spi",			NULL,	43,	0x114,	40000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("xio",	"xio",			NULL,	45,	0x120,	150000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("twc",	"twc",			NULL,	16,	0x12c,	150000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc1",	"spi_tegra.0",		NULL,	41,	0x134,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc2",	"spi_tegra.1",		NULL,	44,	0x118,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc3",	"spi_tegra.2",		NULL,	46,	0x11c,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sbc4",	"spi_tegra.3",		NULL,	68,	0x1b4,	160000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("ide",	"ide",			NULL,	25,	0x144,	100000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("ndflash",	"tegra_nand",		NULL,	13,	0x160,	164000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("vfir",	"vfir",			NULL,	7,	0x168,	72000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("sdmmc1",	"sdhci-tegra.0",	NULL,	14,	0x150,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("sdmmc2",	"sdhci-tegra.1",	NULL,	9,	0x154,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("sdmmc3",	"sdhci-tegra.2",	NULL,	69,	0x1bc,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("sdmmc4",	"sdhci-tegra.3",	NULL,	15,	0x164,	52000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("vcp",	"tegra-avp",		"vcp",	29,	0,	250000000, mux_clk_m,			0),
	PERIPH_CLK("bsea",	"tegra-avp",		"bsea",	62,	0,	250000000, mux_clk_m,			0),
	PERIPH_CLK("bsev",	"tegra-aes",		"bsev",	63,	0,	250000000, mux_clk_m,			0),
	PERIPH_CLK("vde",	"tegra-avp",		"vde",	61,	0x1c8,	250000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("csite",	"csite",		NULL,	73,	0x1d4,	144000000, mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* max rate ??? */
	/* FIXME: what is la? */
	PERIPH_CLK("la",	"la",			NULL,	76,	0x1f8,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("owr",	"tegra_w1",		NULL,	71,	0x1cc,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71),
	PERIPH_CLK("nor",	"nor",			NULL,	42,	0x1d0,	92000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("mipi",	"mipi",			NULL,	50,	0x174,	60000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U71), /* scales with voltage */
	PERIPH_CLK("i2c1",	"tegra-i2c.0",		NULL,	12,	0x124,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("i2c2",	"tegra-i2c.1",		NULL,	54,	0x198,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("i2c3",	"tegra-i2c.2",		NULL,	67,	0x1b8,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("dvc",	"tegra-i2c.3",		NULL,	47,	0x128,	26000000,  mux_pllp_pllc_pllm_clkm,	MUX | DIV_U16),
	PERIPH_CLK("i2c1_i2c",	"tegra-i2c.0",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("i2c2_i2c",	"tegra-i2c.1",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("i2c3_i2c",	"tegra-i2c.2",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("dvc_i2c",	"tegra-i2c.3",		"i2c",	0,	0,	72000000,  mux_pllp_out3,			0),
	PERIPH_CLK("uarta",	"uart.0",		NULL,	6,	0x178,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uartb",	"uart.1",		NULL,	7,	0x17c,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uartc",	"uart.2",		NULL,	55,	0x1a0,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uartd",	"uart.3",		NULL,	65,	0x1c0,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("uarte",	"uart.4",		NULL,	66,	0x1c4,	600000000, mux_pllp_pllc_pllm_clkm,	MUX),
	PERIPH_CLK("3d",	"3d",			NULL,	24,	0x158,	300000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | PERIPH_MANUAL_RESET), /* scales with voltage and process_id */
	PERIPH_CLK("2d",	"2d",			NULL,	21,	0x15c,	300000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("vi",	"tegra_camera",		"vi",	20,	0x148,	150000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("vi_sensor",	"tegra_camera",		"vi_sensor",	20,	0x1a8,	150000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71 | PERIPH_NO_RESET), /* scales with voltage and process_id */
	PERIPH_CLK("epp",	"epp",			NULL,	19,	0x16c,	300000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("mpe",	"mpe",			NULL,	60,	0x170,	250000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("host1x",	"host1x",		NULL,	28,	0x180,	166000000, mux_pllm_pllc_pllp_plla,	MUX | DIV_U71), /* scales with voltage and process_id */
	PERIPH_CLK("cve",	"cve",			NULL,	49,	0x140,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("tvo",	"tvo",			NULL,	49,	0x188,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("hdmi",	"hdmi",			NULL,	51,	0x18c,	600000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("tvdac",	"tvdac",		NULL,	53,	0x194,	250000000, mux_pllp_plld_pllc_clkm,	MUX | DIV_U71), /* requires min voltage */
	PERIPH_CLK("disp1",	"tegradc.0",		NULL,	27,	0x138,	600000000, mux_pllp_plld_pllc_clkm,	MUX), /* scales with voltage and process_id */
	PERIPH_CLK("disp2",	"tegradc.1",		NULL,	26,	0x13c,	600000000, mux_pllp_plld_pllc_clkm,	MUX), /* scales with voltage and process_id */
	PERIPH_CLK("usbd",	"fsl-tegra-udc",	NULL,	22,	0,	480000000, mux_clk_m,			0), /* requires min voltage */
	PERIPH_CLK("usb2",	"tegra-ehci.1",		NULL,	58,	0,	480000000, mux_clk_m,			0), /* requires min voltage */
	PERIPH_CLK("usb3",	"tegra-ehci.2",		NULL,	59,	0,	480000000, mux_clk_m,			0), /* requires min voltage */
	PERIPH_CLK("dsi",	"dsi",			NULL,	48,	0,	500000000, mux_plld,			0), /* scales with voltage */
	PERIPH_CLK("csi",	"tegra_camera",		"csi",	52,	0,	72000000,  mux_pllp_out3,		0),
	PERIPH_CLK("isp",	"tegra_camera",		"isp",	23,	0,	150000000, mux_clk_m,			0), /* same frequency as VI */
	PERIPH_CLK("csus",	"tegra_camera",		"csus",	92,	0,	150000000, mux_clk_m,			PERIPH_NO_RESET),
	PERIPH_CLK("pex",       NULL,			"pex",  70,     0,	26000000,  mux_clk_m,			PERIPH_MANUAL_RESET),
	PERIPH_CLK("afi",       NULL,			"afi",  72,     0,	26000000,  mux_clk_m,			PERIPH_MANUAL_RESET),
	PERIPH_CLK("pcie_xclk", NULL,		  "pcie_xclk",  74,     0,	26000000,  mux_clk_m,			PERIPH_MANUAL_RESET),

	SHARED_CLK("avp.sclk",	"tegra-avp",		"sclk",	&tegra_clk_sclk),
	SHARED_CLK("avp.emc",	"tegra-avp",		"emc",	&tegra_clk_emc),
	SHARED_CLK("cpu.emc",	"cpu",			"emc",	&tegra_clk_emc),
	SHARED_CLK("disp1.emc",	"tegradc.0",		"emc",	&tegra_clk_emc),
	SHARED_CLK("disp2.emc",	"tegradc.1",		"emc",	&tegra_clk_emc),
	SHARED_CLK("hdmi.emc",	"hdmi",			"emc",	&tegra_clk_emc),
	SHARED_CLK("host.emc",	"tegra_grhost",		"emc",	&tegra_clk_emc),
	SHARED_CLK("usbd.emc",	"fsl-tegra-udc",	"emc",	&tegra_clk_emc),
	SHARED_CLK("usb1.emc",	"tegra-ehci.0",		"emc",	&tegra_clk_emc),
	SHARED_CLK("usb2.emc",	"tegra-ehci.1",		"emc",	&tegra_clk_emc),
	SHARED_CLK("usb3.emc",	"tegra-ehci.2",		"emc",	&tegra_clk_emc),
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
static struct clk_duplicate tegra_clk_duplicates[] = {
	CLK_DUPLICATE("uarta",	"tegra_uart.0",	NULL),
	CLK_DUPLICATE("uartb",	"tegra_uart.1",	NULL),
	CLK_DUPLICATE("uartc",	"tegra_uart.2",	NULL),
	CLK_DUPLICATE("uartd",	"tegra_uart.3",	NULL),
	CLK_DUPLICATE("uarte",	"tegra_uart.4",	NULL),
	CLK_DUPLICATE("usbd", "utmip-pad", NULL),
	CLK_DUPLICATE("usbd", "tegra-ehci.0", NULL),
	CLK_DUPLICATE("usbd", "tegra-otg", NULL),
	CLK_DUPLICATE("hdmi", "tegradc.0", "hdmi"),
	CLK_DUPLICATE("hdmi", "tegradc.1", "hdmi"),
	CLK_DUPLICATE("pwm", "tegra_pwm.0", NULL),
	CLK_DUPLICATE("pwm", "tegra_pwm.1", NULL),
	CLK_DUPLICATE("pwm", "tegra_pwm.2", NULL),
	CLK_DUPLICATE("pwm", "tegra_pwm.3", NULL),
	CLK_DUPLICATE("host1x", "tegra_grhost", "host1x"),
	CLK_DUPLICATE("2d", "tegra_grhost", "gr2d"),
	CLK_DUPLICATE("3d", "tegra_grhost", "gr3d"),
	CLK_DUPLICATE("epp", "tegra_grhost", "epp"),
	CLK_DUPLICATE("mpe", "tegra_grhost", "mpe"),
	CLK_DUPLICATE("cop", "tegra-avp", "cop"),
	CLK_DUPLICATE("vde", "tegra-aes", "vde"),
};

#define CLK(dev, con, ck)	\
	{			\
		.dev_id = dev,	\
		.con_id = con,	\
		.clk = ck,	\
	}

static struct clk *tegra_ptr_clks[] = {
	&tegra_clk_32k,
	&tegra_pll_s,
	&tegra_clk_m,
	&tegra_pll_m,
	&tegra_pll_m_out1,
	&tegra_pll_c,
	&tegra_pll_c_out1,
	&tegra_pll_p,
	&tegra_pll_p_out1,
	&tegra_pll_p_out2,
	&tegra_pll_p_out3,
	&tegra_pll_p_out4,
	&tegra_pll_a,
	&tegra_pll_a_out0,
	&tegra_pll_d,
	&tegra_pll_d_out0,
	&tegra_pll_u,
	&tegra_pll_x,
	&tegra_pll_e,
	&tegra_clk_cclk,
	&tegra_clk_sclk,
	&tegra_clk_hclk,
	&tegra_clk_pclk,
	&tegra_clk_d,
	&tegra_clk_cdev1,
	&tegra_clk_cdev2,
	&tegra_clk_virtual_cpu,
	&tegra_clk_blink,
	&tegra_clk_cop,
	&tegra_clk_emc,
};

static void tegra2_init_one_clock(struct clk *c)
{
	clk_init(c);
	INIT_LIST_HEAD(&c->shared_bus_list);
	if (!c->lookup.dev_id && !c->lookup.con_id)
		c->lookup.con_id = c->name;
	c->lookup.clk = c;
	clkdev_add(&c->lookup);
}

void __init tegra2_init_clocks(void)
{
	int i;
	struct clk *c;

	for (i = 0; i < ARRAY_SIZE(tegra_ptr_clks); i++)
		tegra2_init_one_clock(tegra_ptr_clks[i]);

	for (i = 0; i < ARRAY_SIZE(tegra_list_clks); i++)
		tegra2_init_one_clock(&tegra_list_clks[i]);

	for (i = 0; i < ARRAY_SIZE(tegra_clk_duplicates); i++) {
		c = tegra_get_clock_by_name(tegra_clk_duplicates[i].name);
		if (!c) {
			pr_err("%s: Unknown duplicate clock %s\n", __func__,
				tegra_clk_duplicates[i].name);
			continue;
		}

		tegra_clk_duplicates[i].lookup.clk = c;
		clkdev_add(&tegra_clk_duplicates[i].lookup);
	}

	init_audio_sync_clock_mux();
}

#ifdef CONFIG_PM
static u32 clk_rst_suspend[RST_DEVICES_NUM + CLK_OUT_ENB_NUM +
			   PERIPH_CLK_SOURCE_NUM + 22];

void tegra_clk_suspend(void)
{
	unsigned long off, i;
	u32 *ctx = clk_rst_suspend;

	*ctx++ = clk_readl(OSC_CTRL) & OSC_CTRL_MASK;
	*ctx++ = clk_readl(tegra_pll_c.reg + PLL_BASE);
	*ctx++ = clk_readl(tegra_pll_c.reg + PLL_MISC(&tegra_pll_c));
	*ctx++ = clk_readl(tegra_pll_a.reg + PLL_BASE);
	*ctx++ = clk_readl(tegra_pll_a.reg + PLL_MISC(&tegra_pll_a));
	*ctx++ = clk_readl(tegra_pll_s.reg + PLL_BASE);
	*ctx++ = clk_readl(tegra_pll_s.reg + PLL_MISC(&tegra_pll_s));
	*ctx++ = clk_readl(tegra_pll_d.reg + PLL_BASE);
	*ctx++ = clk_readl(tegra_pll_d.reg + PLL_MISC(&tegra_pll_d));
	*ctx++ = clk_readl(tegra_pll_u.reg + PLL_BASE);
	*ctx++ = clk_readl(tegra_pll_u.reg + PLL_MISC(&tegra_pll_u));

	*ctx++ = clk_readl(tegra_pll_m_out1.reg);
	*ctx++ = clk_readl(tegra_pll_a_out0.reg);
	*ctx++ = clk_readl(tegra_pll_c_out1.reg);

	*ctx++ = clk_readl(tegra_clk_cclk.reg);
	*ctx++ = clk_readl(tegra_clk_cclk.reg + SUPER_CLK_DIVIDER);

	*ctx++ = clk_readl(tegra_clk_sclk.reg);
	*ctx++ = clk_readl(tegra_clk_sclk.reg + SUPER_CLK_DIVIDER);
	*ctx++ = clk_readl(tegra_clk_pclk.reg);

	*ctx++ = clk_readl(tegra_clk_audio.reg);

	for (off = PERIPH_CLK_SOURCE_I2S1; off <= PERIPH_CLK_SOURCE_OSC;
			off += 4) {
		if (off == PERIPH_CLK_SOURCE_EMC)
			continue;
		*ctx++ = clk_readl(off);
	}

	off = RST_DEVICES;
	for (i = 0; i < RST_DEVICES_NUM; i++, off += 4)
		*ctx++ = clk_readl(off);

	off = CLK_OUT_ENB;
	for (i = 0; i < CLK_OUT_ENB_NUM; i++, off += 4)
		*ctx++ = clk_readl(off);

	*ctx++ = clk_readl(MISC_CLK_ENB);
	*ctx++ = clk_readl(CLK_MASK_ARM);

	BUG_ON(ctx - clk_rst_suspend != ARRAY_SIZE(clk_rst_suspend));
}

void tegra_clk_resume(void)
{
	unsigned long off, i;
	const u32 *ctx = clk_rst_suspend;
	u32 val;

	val = clk_readl(OSC_CTRL) & ~OSC_CTRL_MASK;
	val |= *ctx++;
	clk_writel(val, OSC_CTRL);

	clk_writel(*ctx++, tegra_pll_c.reg + PLL_BASE);
	clk_writel(*ctx++, tegra_pll_c.reg + PLL_MISC(&tegra_pll_c));
	clk_writel(*ctx++, tegra_pll_a.reg + PLL_BASE);
	clk_writel(*ctx++, tegra_pll_a.reg + PLL_MISC(&tegra_pll_a));
	clk_writel(*ctx++, tegra_pll_s.reg + PLL_BASE);
	clk_writel(*ctx++, tegra_pll_s.reg + PLL_MISC(&tegra_pll_s));
	clk_writel(*ctx++, tegra_pll_d.reg + PLL_BASE);
	clk_writel(*ctx++, tegra_pll_d.reg + PLL_MISC(&tegra_pll_d));
	clk_writel(*ctx++, tegra_pll_u.reg + PLL_BASE);
	clk_writel(*ctx++, tegra_pll_u.reg + PLL_MISC(&tegra_pll_u));
	udelay(1000);

	clk_writel(*ctx++, tegra_pll_m_out1.reg);
	clk_writel(*ctx++, tegra_pll_a_out0.reg);
	clk_writel(*ctx++, tegra_pll_c_out1.reg);

	clk_writel(*ctx++, tegra_clk_cclk.reg);
	clk_writel(*ctx++, tegra_clk_cclk.reg + SUPER_CLK_DIVIDER);

	clk_writel(*ctx++, tegra_clk_sclk.reg);
	clk_writel(*ctx++, tegra_clk_sclk.reg + SUPER_CLK_DIVIDER);
	clk_writel(*ctx++, tegra_clk_pclk.reg);

	clk_writel(*ctx++, tegra_clk_audio.reg);

	/* enable all clocks before configuring clock sources */
	clk_writel(0xbffffff9ul, CLK_OUT_ENB);
	clk_writel(0xfefffff7ul, CLK_OUT_ENB + 4);
	clk_writel(0x77f01bfful, CLK_OUT_ENB + 8);
	wmb();

	for (off = PERIPH_CLK_SOURCE_I2S1; off <= PERIPH_CLK_SOURCE_OSC;
			off += 4) {
		if (off == PERIPH_CLK_SOURCE_EMC)
			continue;
		clk_writel(*ctx++, off);
	}
	wmb();

	off = RST_DEVICES;
	for (i = 0; i < RST_DEVICES_NUM; i++, off += 4)
		clk_writel(*ctx++, off);
	wmb();

	off = CLK_OUT_ENB;
	for (i = 0; i < CLK_OUT_ENB_NUM; i++, off += 4)
		clk_writel(*ctx++, off);
	wmb();

	clk_writel(*ctx++, MISC_CLK_ENB);
	clk_writel(*ctx++, CLK_MASK_ARM);
}
#endif
