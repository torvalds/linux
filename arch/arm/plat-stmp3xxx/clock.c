/*
 * Clock manipulation routines for Freescale STMP37XX/STMP378X
 *
 * Author: Vitaly Wool <vital@embeddedalley.com>
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */
#define DEBUG
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/clkdev.h>

#include <asm/mach-types.h>
#include <mach/platform.h>
#include <mach/regs-clkctrl.h>

#include "clock.h"

static DEFINE_SPINLOCK(clocks_lock);

static struct clk osc_24M;
static struct clk pll_clk;
static struct clk cpu_clk;
static struct clk hclk;

static int propagate_rate(struct clk *);

static inline int clk_is_busy(struct clk *clk)
{
	return __raw_readl(clk->busy_reg) & (1 << clk->busy_bit);
}

static inline int clk_good(struct clk *clk)
{
	return clk && !IS_ERR(clk) && clk->ops;
}

static int std_clk_enable(struct clk *clk)
{
	if (clk->enable_reg) {
		u32 clk_reg = __raw_readl(clk->enable_reg);
		if (clk->enable_negate)
			clk_reg &= ~(1 << clk->enable_shift);
		else
			clk_reg |= (1 << clk->enable_shift);
		__raw_writel(clk_reg, clk->enable_reg);
		if (clk->enable_wait)
			udelay(clk->enable_wait);
		return 0;
	} else
		return -EINVAL;
}

static int std_clk_disable(struct clk *clk)
{
	if (clk->enable_reg) {
		u32 clk_reg = __raw_readl(clk->enable_reg);
		if (clk->enable_negate)
			clk_reg |= (1 << clk->enable_shift);
		else
			clk_reg &= ~(1 << clk->enable_shift);
		__raw_writel(clk_reg, clk->enable_reg);
		return 0;
	} else
		return -EINVAL;
}

static int io_set_rate(struct clk *clk, u32 rate)
{
	u32 reg_frac, clkctrl_frac;
	int i, ret = 0, mask = 0x1f;

	clkctrl_frac = (clk->parent->rate * 18 + rate - 1) / rate;

	if (clkctrl_frac < 18 || clkctrl_frac > 35) {
		ret = -EINVAL;
		goto out;
	}

	reg_frac = __raw_readl(clk->scale_reg);
	reg_frac &= ~(mask << clk->scale_shift);
	__raw_writel(reg_frac | (clkctrl_frac << clk->scale_shift),
				clk->scale_reg);
	if (clk->busy_reg) {
		for (i = 10000; i; i--)
			if (!clk_is_busy(clk))
				break;
		if (!i)
			ret = -ETIMEDOUT;
		else
			ret = 0;
	}
out:
	return ret;
}

static long io_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate * 18;
	int mask = 0x1f;

	rate /= (__raw_readl(clk->scale_reg) >> clk->scale_shift) & mask;
	clk->rate = rate;

	return rate;
}

static long per_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate;
	long div;
	const int mask = 0xff;

	if (clk->enable_reg &&
			!(__raw_readl(clk->enable_reg) & clk->enable_shift))
		clk->rate = 0;
	else {
		div = (__raw_readl(clk->scale_reg) >> clk->scale_shift) & mask;
		if (div)
			rate /= div;
		clk->rate = rate;
	}

	return clk->rate;
}

static int per_set_rate(struct clk *clk, u32 rate)
{
	int ret = -EINVAL;
	int div = (clk->parent->rate + rate - 1) / rate;
	u32 reg_frac;
	const int mask = 0xff;
	int try = 10;
	int i = -1;

	if (div == 0 || div > mask)
		goto out;

	reg_frac = __raw_readl(clk->scale_reg);
	reg_frac &= ~(mask << clk->scale_shift);

	while (try--) {
		__raw_writel(reg_frac | (div << clk->scale_shift),
				clk->scale_reg);

		if (clk->busy_reg) {
			for (i = 10000; i; i--)
				if (!clk_is_busy(clk))
					break;
		}
		if (i)
			break;
	}

	if (!i)
		ret = -ETIMEDOUT;
	else
		ret = 0;

out:
	if (ret != 0)
		printk(KERN_ERR "%s: error %d\n", __func__, ret);
	return ret;
}

static long lcdif_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate;
	long div;
	const int mask = 0xff;

	div = (__raw_readl(clk->scale_reg) >> clk->scale_shift) & mask;
	if (div) {
		rate /= div;
		div = (__raw_readl(REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC) &
			BM_CLKCTRL_FRAC_PIXFRAC) >> BP_CLKCTRL_FRAC_PIXFRAC;
		rate /= div;
	}
	clk->rate = rate;

	return rate;
}

static int lcdif_set_rate(struct clk *clk, u32 rate)
{
	int ret = 0;
	/*
	 * On 3700, we can get most timings exact by modifying ref_pix
	 * and the divider, but keeping the phase timings at 1 (2
	 * phases per cycle).
	 *
	 * ref_pix can be between 480e6*18/35=246.9MHz and 480e6*18/18=480MHz,
	 * which is between 18/(18*480e6)=2.084ns and 35/(18*480e6)=4.050ns.
	 *
	 * ns_cycle >= 2*18e3/(18*480) = 25/6
	 * ns_cycle <= 2*35e3/(18*480) = 875/108
	 *
	 * Multiply the ns_cycle by 'div' to lengthen it until it fits the
	 * bounds. This is the divider we'll use after ref_pix.
	 *
	 * 6 * ns_cycle >= 25 * div
	 * 108 * ns_cycle <= 875 * div
	 */
	u32 ns_cycle = 1000000 / rate;
	u32 div, reg_val;
	u32 lowest_result = (u32) -1;
	u32 lowest_div = 0, lowest_fracdiv = 0;

	for (div = 1; div < 256; ++div) {
		u32 fracdiv;
		u32 ps_result;
		int lower_bound = 6 * ns_cycle >= 25 * div;
		int upper_bound = 108 * ns_cycle <= 875 * div;
		if (!lower_bound)
			break;
		if (!upper_bound)
			continue;
		/*
		 * Found a matching div. Calculate fractional divider needed,
		 * rounded up.
		 */
		fracdiv = ((clk->parent->rate / 1000 * 18 / 2) *
				ns_cycle + 1000 * div - 1) /
				(1000 * div);
		if (fracdiv < 18 || fracdiv > 35) {
			ret = -EINVAL;
			goto out;
		}
		/* Calculate the actual cycle time this results in */
		ps_result = 6250 * div * fracdiv / 27;

		/* Use the fastest result that doesn't break ns_cycle */
		if (ps_result <= lowest_result) {
			lowest_result = ps_result;
			lowest_div = div;
			lowest_fracdiv = fracdiv;
		}
	}

	if (div >= 256 || lowest_result == (u32) -1) {
		ret = -EINVAL;
		goto out;
	}
	pr_debug("Programming PFD=%u,DIV=%u ref_pix=%uMHz "
			"PIXCLK=%uMHz cycle=%u.%03uns\n",
			lowest_fracdiv, lowest_div,
			480*18/lowest_fracdiv, 480*18/lowest_fracdiv/lowest_div,
			lowest_result / 1000, lowest_result % 1000);

	/* Program ref_pix phase fractional divider */
	reg_val = __raw_readl(REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC);
	reg_val &= ~BM_CLKCTRL_FRAC_PIXFRAC;
	reg_val |= BF(lowest_fracdiv, CLKCTRL_FRAC_PIXFRAC);
	__raw_writel(reg_val, REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC);

	/* Ungate PFD */
	stmp3xxx_clearl(BM_CLKCTRL_FRAC_CLKGATEPIX,
			REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC);

	/* Program pix divider */
	reg_val = __raw_readl(clk->scale_reg);
	reg_val &= ~(BM_CLKCTRL_PIX_DIV | BM_CLKCTRL_PIX_CLKGATE);
	reg_val |= BF(lowest_div, CLKCTRL_PIX_DIV);
	__raw_writel(reg_val, clk->scale_reg);

	/* Wait for divider update */
	if (clk->busy_reg) {
		int i;
		for (i = 10000; i; i--)
			if (!clk_is_busy(clk))
				break;
		if (!i) {
			ret = -ETIMEDOUT;
			goto out;
		}
	}

	/* Switch to ref_pix source */
	reg_val = __raw_readl(REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ);
	reg_val &= ~BM_CLKCTRL_CLKSEQ_BYPASS_PIX;
	__raw_writel(reg_val, REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ);

out:
	return ret;
}


static int cpu_set_rate(struct clk *clk, u32 rate)
{
	u32 reg_val;

	if (rate < 24000)
		return -EINVAL;
	else if (rate == 24000) {
		/* switch to the 24M source */
		clk_set_parent(clk, &osc_24M);
	} else {
		int i;
		u32 clkctrl_cpu = 1;
		u32 c = clkctrl_cpu;
		u32 clkctrl_frac = 1;
		u32 val;
		for ( ; c < 0x40; c++) {
			u32 f = (pll_clk.rate*18/c + rate/2) / rate;
			int s1, s2;

			if (f < 18 || f > 35)
				continue;
			s1 = pll_clk.rate*18/clkctrl_frac/clkctrl_cpu - rate;
			s2 = pll_clk.rate*18/c/f - rate;
			pr_debug("%s: s1 %d, s2 %d\n", __func__, s1, s2);
			if (abs(s1) > abs(s2)) {
				clkctrl_cpu = c;
				clkctrl_frac = f;
			}
			if (s2 == 0)
				break;
		};
		pr_debug("%s: clkctrl_cpu %d, clkctrl_frac %d\n", __func__,
				clkctrl_cpu, clkctrl_frac);
		if (c == 0x40) {
			int  d = pll_clk.rate*18/clkctrl_frac/clkctrl_cpu -
				rate;
			if (abs(d) > 100 ||
			    clkctrl_frac < 18 || clkctrl_frac > 35)
				return -EINVAL;
		}

		/* 4.6.2 */
		val = __raw_readl(clk->scale_reg);
		val &= ~(0x3f << clk->scale_shift);
		val |= clkctrl_frac;
		clk_set_parent(clk, &osc_24M);
		udelay(10);
		__raw_writel(val, clk->scale_reg);
		/* ungate */
		__raw_writel(1<<7, clk->scale_reg + 8);
		/* write clkctrl_cpu */
		clk->saved_div = clkctrl_cpu;

		reg_val = __raw_readl(REGS_CLKCTRL_BASE + HW_CLKCTRL_CPU);
		reg_val &= ~0x3F;
		reg_val |= clkctrl_cpu;
		__raw_writel(reg_val, REGS_CLKCTRL_BASE + HW_CLKCTRL_CPU);

		for (i = 10000; i; i--)
			if (!clk_is_busy(clk))
				break;
		if (!i) {
			printk(KERN_ERR "couldn't set up CPU divisor\n");
			return -ETIMEDOUT;
		}
		clk_set_parent(clk, &pll_clk);
		clk->saved_div = 0;
		udelay(10);
	}
	return 0;
}

static long cpu_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate * 18;

	rate /= (__raw_readl(clk->scale_reg) >> clk->scale_shift) & 0x3f;
	rate /= __raw_readl(REGS_CLKCTRL_BASE + HW_CLKCTRL_CPU) & 0x3f;
	rate = ((rate + 9) / 10) * 10;
	clk->rate = rate;

	return rate;
}

static long cpu_round_rate(struct clk *clk, u32 rate)
{
	unsigned long r = 0;

	if (rate <= 24000)
		r = 24000;
	else {
		u32 clkctrl_cpu = 1;
		u32 clkctrl_frac;
		do {
			clkctrl_frac =
				(pll_clk.rate*18 / clkctrl_cpu + rate/2) / rate;
			if (clkctrl_frac > 35)
				continue;
			if (pll_clk.rate*18 / clkctrl_frac / clkctrl_cpu/10 ==
			    rate / 10)
				break;
		} while (pll_clk.rate / 2  >= clkctrl_cpu++ * rate);
		if (pll_clk.rate / 2 < (clkctrl_cpu - 1) * rate)
			clkctrl_cpu--;
		pr_debug("%s: clkctrl_cpu %d, clkctrl_frac %d\n", __func__,
				clkctrl_cpu, clkctrl_frac);
		if (clkctrl_frac < 18)
			clkctrl_frac = 18;
		if (clkctrl_frac > 35)
			clkctrl_frac = 35;

		r = pll_clk.rate * 18;
		r /= clkctrl_frac;
		r /= clkctrl_cpu;
		r = 10 * ((r + 9) / 10);
	}
	return r;
}

static long emi_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate * 18;

	rate /= (__raw_readl(clk->scale_reg) >> clk->scale_shift) & 0x3f;
	rate /= __raw_readl(REGS_CLKCTRL_BASE + HW_CLKCTRL_EMI) & 0x3f;
	clk->rate = rate;

	return rate;
}

static int clkseq_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = -EINVAL;
	int shift = 8;

	/* bypass? */
	if (parent == &osc_24M)
		shift = 4;

	if (clk->bypass_reg) {
#ifdef CONFIG_ARCH_STMP378X
		u32 hbus_val, cpu_val;

		if (clk == &cpu_clk && shift == 4) {
			hbus_val = __raw_readl(REGS_CLKCTRL_BASE +
					HW_CLKCTRL_HBUS);
			cpu_val = __raw_readl(REGS_CLKCTRL_BASE +
					HW_CLKCTRL_CPU);

			hbus_val &= ~(BM_CLKCTRL_HBUS_DIV_FRAC_EN |
				      BM_CLKCTRL_HBUS_DIV);
			clk->saved_div = cpu_val & BM_CLKCTRL_CPU_DIV_CPU;
			cpu_val &= ~BM_CLKCTRL_CPU_DIV_CPU;
			cpu_val |= 1;

			if (machine_is_stmp378x()) {
				__raw_writel(hbus_val,
					REGS_CLKCTRL_BASE + HW_CLKCTRL_HBUS);
				__raw_writel(cpu_val,
					REGS_CLKCTRL_BASE + HW_CLKCTRL_CPU);
				hclk.rate = 0;
			}
		} else if (clk == &cpu_clk && shift == 8) {
			hbus_val = __raw_readl(REGS_CLKCTRL_BASE +
							HW_CLKCTRL_HBUS);
			cpu_val = __raw_readl(REGS_CLKCTRL_BASE +
							HW_CLKCTRL_CPU);
			hbus_val &= ~(BM_CLKCTRL_HBUS_DIV_FRAC_EN |
				      BM_CLKCTRL_HBUS_DIV);
			hbus_val |= 2;
			cpu_val &= ~BM_CLKCTRL_CPU_DIV_CPU;
			if (clk->saved_div)
				cpu_val |= clk->saved_div;
			else
				cpu_val |= 2;

			if (machine_is_stmp378x()) {
				__raw_writel(hbus_val,
					REGS_CLKCTRL_BASE + HW_CLKCTRL_HBUS);
				__raw_writel(cpu_val,
					REGS_CLKCTRL_BASE + HW_CLKCTRL_CPU);
				hclk.rate = 0;
			}
		}
#endif
		__raw_writel(1 << clk->bypass_shift, clk->bypass_reg + shift);

		ret = 0;
	}

	return ret;
}

static int hbus_set_rate(struct clk *clk, u32 rate)
{
	u8 div = 0;
	int is_frac = 0;
	u32 clkctrl_hbus;
	struct clk *parent = clk->parent;

	pr_debug("%s: rate %d, parent rate %d\n", __func__, rate,
			parent->rate);

	if (rate > parent->rate)
		return -EINVAL;

	if (((parent->rate + rate/2) / rate) * rate != parent->rate &&
	    parent->rate / rate < 32) {
		pr_debug("%s: switching to fractional mode\n", __func__);
		is_frac = 1;
	}

	if (is_frac)
		div = (32 * rate + parent->rate / 2) / parent->rate;
	else
		div = (parent->rate + rate - 1) / rate;
	pr_debug("%s: div calculated is %d\n", __func__, div);
	if (!div || div > 0x1f)
		return -EINVAL;

	clk_set_parent(&cpu_clk, &osc_24M);
	udelay(10);
	clkctrl_hbus = __raw_readl(clk->scale_reg);
	clkctrl_hbus &= ~0x3f;
	clkctrl_hbus |= div;
	clkctrl_hbus |= (is_frac << 5);

	__raw_writel(clkctrl_hbus, clk->scale_reg);
	if (clk->busy_reg) {
		int i;
		for (i = 10000; i; i--)
			if (!clk_is_busy(clk))
				break;
		if (!i) {
			printk(KERN_ERR "couldn't set up CPU divisor\n");
			return -ETIMEDOUT;
		}
	}
	clk_set_parent(&cpu_clk, &pll_clk);
	__raw_writel(clkctrl_hbus, clk->scale_reg);
	udelay(10);
	return 0;
}

static long hbus_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate;

	if (__raw_readl(clk->scale_reg) & 0x20) {
		rate *= __raw_readl(clk->scale_reg) & 0x1f;
		rate /= 32;
	} else
		rate /= __raw_readl(clk->scale_reg) & 0x1f;
	clk->rate = rate;

	return rate;
}

static int xbus_set_rate(struct clk *clk, u32 rate)
{
	u16 div = 0;
	u32 clkctrl_xbus;

	pr_debug("%s: rate %d, parent rate %d\n", __func__, rate,
			clk->parent->rate);

	div = (clk->parent->rate + rate - 1) / rate;
	pr_debug("%s: div calculated is %d\n", __func__, div);
	if (!div || div > 0x3ff)
		return -EINVAL;

	clkctrl_xbus = __raw_readl(clk->scale_reg);
	clkctrl_xbus &= ~0x3ff;
	clkctrl_xbus |= div;
	__raw_writel(clkctrl_xbus, clk->scale_reg);
	if (clk->busy_reg) {
		int i;
		for (i = 10000; i; i--)
			if (!clk_is_busy(clk))
				break;
		if (!i) {
			printk(KERN_ERR "couldn't set up xbus divisor\n");
			return -ETIMEDOUT;
		}
	}
	return 0;
}

static long xbus_get_rate(struct clk *clk)
{
	long rate = clk->parent->rate;

	rate /= __raw_readl(clk->scale_reg) & 0x3ff;
	clk->rate = rate;

	return rate;
}


/* Clock ops */

static struct clk_ops std_ops = {
	.enable		= std_clk_enable,
	.disable	= std_clk_disable,
	.get_rate	= per_get_rate,
	.set_rate	= per_set_rate,
	.set_parent	= clkseq_set_parent,
};

static struct clk_ops min_ops = {
	.enable		= std_clk_enable,
	.disable	= std_clk_disable,
};

static struct clk_ops cpu_ops = {
	.enable		= std_clk_enable,
	.disable	= std_clk_disable,
	.get_rate	= cpu_get_rate,
	.set_rate	= cpu_set_rate,
	.round_rate	= cpu_round_rate,
	.set_parent	= clkseq_set_parent,
};

static struct clk_ops io_ops = {
	.enable		= std_clk_enable,
	.disable	= std_clk_disable,
	.get_rate	= io_get_rate,
	.set_rate	= io_set_rate,
};

static struct clk_ops hbus_ops = {
	.get_rate	= hbus_get_rate,
	.set_rate	= hbus_set_rate,
};

static struct clk_ops xbus_ops = {
	.get_rate	= xbus_get_rate,
	.set_rate	= xbus_set_rate,
};

static struct clk_ops lcdif_ops = {
	.enable		= std_clk_enable,
	.disable	= std_clk_disable,
	.get_rate	= lcdif_get_rate,
	.set_rate	= lcdif_set_rate,
	.set_parent	= clkseq_set_parent,
};

static struct clk_ops emi_ops = {
	.get_rate	= emi_get_rate,
};

/* List of on-chip clocks */

static struct clk osc_24M = {
	.flags		= FIXED_RATE | ENABLED,
	.rate		= 24000,
};

static struct clk pll_clk = {
	.parent		= &osc_24M,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_PLLCTRL0,
	.enable_shift	= 16,
	.enable_wait	= 10,
	.flags		= FIXED_RATE | ENABLED,
	.rate		= 480000,
	.ops		= &min_ops,
};

static struct clk cpu_clk = {
	.parent		= &pll_clk,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC,
	.scale_shift	= 0,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 7,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CPU,
	.busy_bit	= 28,
	.flags		= RATE_PROPAGATES | ENABLED,
	.ops		= &cpu_ops,
};

static struct clk io_clk = {
	.parent		= &pll_clk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC,
	.scale_shift	= 24,
	.flags		= RATE_PROPAGATES | ENABLED,
	.ops		= &io_ops,
};

static struct clk hclk = {
	.parent		= &cpu_clk,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_HBUS,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 7,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_HBUS,
	.busy_bit	= 29,
	.flags		= RATE_PROPAGATES | ENABLED,
	.ops		= &hbus_ops,
};

static struct clk xclk = {
	.parent		= &osc_24M,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XBUS,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XBUS,
	.busy_bit	= 31,
	.flags		= RATE_PROPAGATES | ENABLED,
	.ops		= &xbus_ops,
};

static struct clk uart_clk = {
	.parent		= &xclk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XTAL,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.flags		= ENABLED,
	.ops		= &min_ops,
};

static struct clk audio_clk = {
	.parent		= &xclk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XTAL,
	.enable_shift	= 30,
	.enable_negate	= 1,
	.ops		= &min_ops,
};

static struct clk pwm_clk = {
	.parent		= &xclk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XTAL,
	.enable_shift	= 29,
	.enable_negate	= 1,
	.ops		= &min_ops,
};

static struct clk dri_clk = {
	.parent		= &xclk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XTAL,
	.enable_shift	= 28,
	.enable_negate	= 1,
	.ops		= &min_ops,
};

static struct clk digctl_clk = {
	.parent		= &xclk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XTAL,
	.enable_shift	= 27,
	.enable_negate	= 1,
	.ops		= &min_ops,
};

static struct clk timer_clk = {
	.parent		= &xclk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_XTAL,
	.enable_shift	= 26,
	.enable_negate	= 1,
	.flags		= ENABLED,
	.ops		= &min_ops,
};

static struct clk lcdif_clk = {
	.parent		= &pll_clk,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_PIX,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_PIX,
	.busy_bit	= 29,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_PIX,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 1,
	.flags		= NEEDS_SET_PARENT,
	.ops		= &lcdif_ops,
};

static struct clk ssp_clk = {
	.parent		= &io_clk,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SSP,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SSP,
	.busy_bit	= 29,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SSP,
	.enable_shift	= 31,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 5,
	.enable_negate	= 1,
	.flags		= NEEDS_SET_PARENT,
	.ops		= &std_ops,
};

static struct clk gpmi_clk = {
	.parent		= &io_clk,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_GPMI,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_GPMI,
	.busy_bit	= 29,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_GPMI,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 4,
	.flags		= NEEDS_SET_PARENT,
	.ops		= &std_ops,
};

static struct clk spdif_clk = {
	.parent		= &pll_clk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SPDIF,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.ops		= &min_ops,
};

static struct clk emi_clk = {
	.parent		= &pll_clk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_EMI,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_FRAC,
	.scale_shift	= 8,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_EMI,
	.busy_bit	= 28,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 6,
	.flags		= ENABLED,
	.ops		= &emi_ops,
};

static struct clk ir_clk = {
	.parent		= &io_clk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_IR,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 3,
	.ops		= &min_ops,
};

static struct clk saif_clk = {
	.parent		= &pll_clk,
	.scale_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SAIF,
	.busy_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SAIF,
	.busy_bit	= 29,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_SAIF,
	.enable_shift	= 31,
	.enable_negate	= 1,
	.bypass_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_CLKSEQ,
	.bypass_shift	= 0,
	.ops		= &std_ops,
};

static struct clk usb_clk = {
	.parent		= &pll_clk,
	.enable_reg	= REGS_CLKCTRL_BASE + HW_CLKCTRL_PLLCTRL0,
	.enable_shift	= 18,
	.enable_negate	= 1,
	.ops		= &min_ops,
};

/* list of all the clocks */
static struct clk_lookup onchip_clks[] = {
	{
		.con_id = "osc_24M",
		.clk = &osc_24M,
	}, {
		.con_id = "pll",
		.clk = &pll_clk,
	}, {
		.con_id = "cpu",
		.clk = &cpu_clk,
	}, {
		.con_id = "hclk",
		.clk = &hclk,
	}, {
		.con_id = "xclk",
		.clk = &xclk,
	}, {
		.con_id = "io",
		.clk = &io_clk,
	}, {
		.con_id = "uart",
		.clk = &uart_clk,
	}, {
		.con_id = "audio",
		.clk = &audio_clk,
	}, {
		.con_id = "pwm",
		.clk = &pwm_clk,
	}, {
		.con_id = "dri",
		.clk = &dri_clk,
	}, {
		.con_id = "digctl",
		.clk = &digctl_clk,
	}, {
		.con_id = "timer",
		.clk = &timer_clk,
	}, {
		.con_id = "lcdif",
		.clk = &lcdif_clk,
	}, {
		.con_id = "ssp",
		.clk = &ssp_clk,
	}, {
		.con_id = "gpmi",
		.clk = &gpmi_clk,
	}, {
		.con_id = "spdif",
		.clk = &spdif_clk,
	}, {
		.con_id = "emi",
		.clk = &emi_clk,
	}, {
		.con_id = "ir",
		.clk = &ir_clk,
	}, {
		.con_id = "saif",
		.clk = &saif_clk,
	}, {
		.con_id = "usb",
		.clk = &usb_clk,
	},
};

static int __init propagate_rate(struct clk *clk)
{
	struct clk_lookup *cl;

	for (cl = onchip_clks; cl < onchip_clks + ARRAY_SIZE(onchip_clks);
	     cl++) {
		if (unlikely(!clk_good(cl->clk)))
			continue;
		if (cl->clk->parent == clk && cl->clk->ops->get_rate) {
			cl->clk->ops->get_rate(cl->clk);
			if (cl->clk->flags & RATE_PROPAGATES)
				propagate_rate(cl->clk);
		}
	}

	return 0;
}

/* Exported API */
unsigned long clk_get_rate(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return 0;

	if (clk->rate != 0)
		return clk->rate;

	if (clk->ops->get_rate != NULL)
		return clk->ops->get_rate(clk);

	return clk_get_rate(clk->parent);
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (unlikely(!clk_good(clk)))
		return 0;

	if (clk->ops->round_rate)
		return clk->ops->round_rate(clk, rate);

	return 0;
}
EXPORT_SYMBOL(clk_round_rate);

static inline int close_enough(long rate1, long rate2)
{
	return rate1 && !((rate2 - rate1) * 1000 / rate1);
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret = -EINVAL;

	if (unlikely(!clk_good(clk)))
		goto out;

	if (clk->flags & FIXED_RATE || !clk->ops->set_rate)
		goto out;

	else if (!close_enough(clk->rate, rate)) {
		ret = clk->ops->set_rate(clk, rate);
		if (ret < 0)
			goto out;
		clk->rate = rate;
		if (clk->flags & RATE_PROPAGATES)
			propagate_rate(clk);
	} else
		ret = 0;

out:
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_enable(struct clk *clk)
{
	unsigned long clocks_flags;

	if (unlikely(!clk_good(clk)))
		return -EINVAL;

	if (clk->parent)
		clk_enable(clk->parent);

	spin_lock_irqsave(&clocks_lock, clocks_flags);

	clk->usage++;
	if (clk->ops && clk->ops->enable)
		clk->ops->enable(clk);

	spin_unlock_irqrestore(&clocks_lock, clocks_flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

static void local_clk_disable(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return;

	if (clk->usage == 0 && clk->ops->disable)
		clk->ops->disable(clk);

	if (clk->parent)
		local_clk_disable(clk->parent);
}

void clk_disable(struct clk *clk)
{
	unsigned long clocks_flags;

	if (unlikely(!clk_good(clk)))
		return;

	spin_lock_irqsave(&clocks_lock, clocks_flags);

	if ((--clk->usage) == 0 && clk->ops->disable)
		clk->ops->disable(clk);

	spin_unlock_irqrestore(&clocks_lock, clocks_flags);
	if (clk->parent)
		clk_disable(clk->parent);
}
EXPORT_SYMBOL(clk_disable);

/* Some additional API */
int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = -ENODEV;
	unsigned long clocks_flags;

	if (unlikely(!clk_good(clk)))
		goto out;

	if (!clk->ops->set_parent)
		goto out;

	spin_lock_irqsave(&clocks_lock, clocks_flags);

	ret = clk->ops->set_parent(clk, parent);
	if (!ret) {
		/* disable if usage count is 0 */
		local_clk_disable(parent);

		parent->usage += clk->usage;
		clk->parent->usage -= clk->usage;

		/* disable if new usage count is 0 */
		local_clk_disable(clk->parent);

		clk->parent = parent;
	}
	spin_unlock_irqrestore(&clocks_lock, clocks_flags);

out:
	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	if (unlikely(!clk_good(clk)))
		return NULL;
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);

static int __init clk_init(void)
{
	struct clk_lookup *cl;
	struct clk_ops *ops;

	spin_lock_init(&clocks_lock);

	for (cl = onchip_clks; cl < onchip_clks + ARRAY_SIZE(onchip_clks);
	     cl++) {
		if (cl->clk->flags & ENABLED)
			clk_enable(cl->clk);
		else
			local_clk_disable(cl->clk);

		ops = cl->clk->ops;

		if ((cl->clk->flags & NEEDS_INITIALIZATION) &&
				ops && ops->set_rate)
			ops->set_rate(cl->clk, cl->clk->rate);

		if (cl->clk->flags & FIXED_RATE) {
			if (cl->clk->flags & RATE_PROPAGATES)
				propagate_rate(cl->clk);
		} else {
			if (ops && ops->get_rate)
				ops->get_rate(cl->clk);
		}

		if (cl->clk->flags & NEEDS_SET_PARENT) {
			if (ops && ops->set_parent)
				ops->set_parent(cl->clk, cl->clk->parent);
		}
	}
	clkdev_add_table(onchip_clks, ARRAY_SIZE(onchip_clks));
	return 0;
}

arch_initcall(clk_init);
