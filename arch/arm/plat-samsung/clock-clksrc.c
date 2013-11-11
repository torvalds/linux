/* linux/arch/arm/plat-samsung/clock-clksrc.c
 *
 * Copyright 2008 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *	http://armlinux.simtec.co.uk/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/device.h>
#include <linux/io.h>

#include <plat/clock.h>
#include <plat/clock-clksrc.h>
#include <plat/cpu-freq.h>

static inline struct clksrc_clk *to_clksrc(struct clk *clk)
{
	return container_of(clk, struct clksrc_clk, clk);
}

static inline u32 bit_mask(u32 shift, u32 nr_bits)
{
	u32 mask = 0xffffffff >> (32 - nr_bits);

	return mask << shift;
}

static unsigned long s3c_getrate_clksrc(struct clk *clk)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	unsigned long rate = clk_get_rate(clk->parent);
	u32 clkdiv = __raw_readl(sclk->reg_div.reg);
	u32 mask = bit_mask(sclk->reg_div.shift, sclk->reg_div.size);

	clkdiv &= mask;
	clkdiv >>= sclk->reg_div.shift;
	clkdiv++;

	rate /= clkdiv;
	return rate;
}

static int s3c_setrate_clksrc(struct clk *clk, unsigned long rate)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	void __iomem *reg = sclk->reg_div.reg;
	unsigned int div;
	u32 mask = bit_mask(sclk->reg_div.shift, sclk->reg_div.size);
	u32 val;

	rate = clk_round_rate(clk, rate);
	div = clk_get_rate(clk->parent) / rate;
	if (div > (1 << sclk->reg_div.size))
		return -EINVAL;

	val = __raw_readl(reg);
	val &= ~mask;
	val |= (div - 1) << sclk->reg_div.shift;
	__raw_writel(val, reg);

	return 0;
}

static int s3c_setparent_clksrc(struct clk *clk, struct clk *parent)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	struct clksrc_sources *srcs = sclk->sources;
	u32 clksrc = __raw_readl(sclk->reg_src.reg);
	u32 mask = bit_mask(sclk->reg_src.shift, sclk->reg_src.size);
	int src_nr = -1;
	int ptr;

	for (ptr = 0; ptr < srcs->nr_sources; ptr++)
		if (srcs->sources[ptr] == parent) {
			src_nr = ptr;
			break;
		}

	if (src_nr >= 0) {
		clk->parent = parent;

		clksrc &= ~mask;
		clksrc |= src_nr << sclk->reg_src.shift;

		__raw_writel(clksrc, sclk->reg_src.reg);
		return 0;
	}

	return -EINVAL;
}

static unsigned long s3c_roundrate_clksrc(struct clk *clk,
					      unsigned long rate)
{
	struct clksrc_clk *sclk = to_clksrc(clk);
	unsigned long parent_rate = clk_get_rate(clk->parent);
	int max_div = 1 << sclk->reg_div.size;
	int div;

	if (rate >= parent_rate)
		rate = parent_rate;
	else {
		div = parent_rate / rate;
		if (parent_rate % rate)
			div++;

		if (div == 0)
			div = 1;
		if (div > max_div)
			div = max_div;

		rate = parent_rate / div;
	}

	return rate;
}

/* Clock initialisation code */

void __init_or_cpufreq s3c_set_clksrc(struct clksrc_clk *clk, bool announce)
{
	struct clksrc_sources *srcs = clk->sources;
	u32 mask = bit_mask(clk->reg_src.shift, clk->reg_src.size);
	u32 clksrc;

	if (!clk->reg_src.reg) {
		if (!clk->clk.parent)
			printk(KERN_ERR "%s: no parent clock specified\n",
				clk->clk.name);
		return;
	}

	clksrc = __raw_readl(clk->reg_src.reg);
	clksrc &= mask;
	clksrc >>= clk->reg_src.shift;

	if (clksrc > srcs->nr_sources || !srcs->sources[clksrc]) {
		printk(KERN_ERR "%s: bad source %d\n",
		       clk->clk.name, clksrc);
		return;
	}

	clk->clk.parent = srcs->sources[clksrc];

	if (announce)
		printk(KERN_INFO "%s: source is %s (%d), rate is %ld\n",
		       clk->clk.name, clk->clk.parent->name, clksrc,
		       clk_get_rate(&clk->clk));
}

static struct clk_ops clksrc_ops = {
	.set_parent	= s3c_setparent_clksrc,
	.get_rate	= s3c_getrate_clksrc,
	.set_rate	= s3c_setrate_clksrc,
	.round_rate	= s3c_roundrate_clksrc,
};

static struct clk_ops clksrc_ops_nodiv = {
	.set_parent	= s3c_setparent_clksrc,
};

static struct clk_ops clksrc_ops_nosrc = {
	.get_rate	= s3c_getrate_clksrc,
	.set_rate	= s3c_setrate_clksrc,
	.round_rate	= s3c_roundrate_clksrc,
};

void __init s3c_register_clksrc(struct clksrc_clk *clksrc, int size)
{
	int ret;

	for (; size > 0; size--, clksrc++) {
		if (!clksrc->reg_div.reg && !clksrc->reg_src.reg)
			printk(KERN_ERR "%s: clock %s has no registers set\n",
			       __func__, clksrc->clk.name);

		/* fill in the default functions */

		if (!clksrc->clk.ops) {
			if (!clksrc->reg_div.reg)
				clksrc->clk.ops = &clksrc_ops_nodiv;
			else if (!clksrc->reg_src.reg)
				clksrc->clk.ops = &clksrc_ops_nosrc;
			else
				clksrc->clk.ops = &clksrc_ops;
		}

		/* setup the clocksource, but do not announce it
		 * as it may be re-set by the setup routines
		 * called after the rest of the clocks have been
		 * registered
		 */
		s3c_set_clksrc(clksrc, false);

		ret = s3c24xx_register_clock(&clksrc->clk);

		if (ret < 0) {
			printk(KERN_ERR "%s: failed to register %s (%d)\n",
			       __func__, clksrc->clk.name, ret);
		}
	}
}
