// SPDX-License-Identifier: GPL-2.0
/*
 * arch/sh/kernel/cpu/sh4/clock-sh4-202.c
 *
 * Additional SH4-202 support for the clock framework
 *
 *  Copyright (C) 2005  Paul Mundt
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/clkdev.h>
#include <asm/clock.h>
#include <asm/freq.h>

#define CPG2_FRQCR3	0xfe0a0018

static int frqcr3_divisors[] = { 1, 2, 3, 4, 6, 8, 16 };
static int frqcr3_values[]   = { 0, 1, 2, 3, 4, 5, 6  };

static unsigned long emi_clk_recalc(struct clk *clk)
{
	int idx = __raw_readl(CPG2_FRQCR3) & 0x0007;
	return clk->parent->rate / frqcr3_divisors[idx];
}

static inline int frqcr3_lookup(struct clk *clk, unsigned long rate)
{
	int divisor = clk->parent->rate / rate;
	int i;

	for (i = 0; i < ARRAY_SIZE(frqcr3_divisors); i++)
		if (frqcr3_divisors[i] == divisor)
			return frqcr3_values[i];

	/* Safe fallback */
	return 5;
}

static struct sh_clk_ops sh4202_emi_clk_ops = {
	.recalc		= emi_clk_recalc,
};

static struct clk sh4202_emi_clk = {
	.flags		= CLK_ENABLE_ON_INIT,
	.ops		= &sh4202_emi_clk_ops,
};

static unsigned long femi_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readl(CPG2_FRQCR3) >> 3) & 0x0007;
	return clk->parent->rate / frqcr3_divisors[idx];
}

static struct sh_clk_ops sh4202_femi_clk_ops = {
	.recalc		= femi_clk_recalc,
};

static struct clk sh4202_femi_clk = {
	.flags		= CLK_ENABLE_ON_INIT,
	.ops		= &sh4202_femi_clk_ops,
};

static void shoc_clk_init(struct clk *clk)
{
	int i;

	/*
	 * For some reason, the shoc_clk seems to be set to some really
	 * insane value at boot (values outside of the allowable frequency
	 * range for instance). We deal with this by scaling it back down
	 * to something sensible just in case.
	 *
	 * Start scaling from the high end down until we find something
	 * that passes rate verification..
	 */
	for (i = 0; i < ARRAY_SIZE(frqcr3_divisors); i++) {
		int divisor = frqcr3_divisors[i];

		if (clk->ops->set_rate(clk, clk->parent->rate / divisor) == 0)
			break;
	}

	WARN_ON(i == ARRAY_SIZE(frqcr3_divisors));	/* Undefined clock */
}

static unsigned long shoc_clk_recalc(struct clk *clk)
{
	int idx = (__raw_readl(CPG2_FRQCR3) >> 6) & 0x0007;
	return clk->parent->rate / frqcr3_divisors[idx];
}

static int shoc_clk_verify_rate(struct clk *clk, unsigned long rate)
{
	struct clk *bclk = clk_get(NULL, "bus_clk");
	unsigned long bclk_rate = clk_get_rate(bclk);

	clk_put(bclk);

	if (rate > bclk_rate)
		return 1;
	if (rate > 66000000)
		return 1;

	return 0;
}

static int shoc_clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long frqcr3;
	unsigned int tmp;

	/* Make sure we have something sensible to switch to */
	if (shoc_clk_verify_rate(clk, rate) != 0)
		return -EINVAL;

	tmp = frqcr3_lookup(clk, rate);

	frqcr3 = __raw_readl(CPG2_FRQCR3);
	frqcr3 &= ~(0x0007 << 6);
	frqcr3 |= tmp << 6;
	__raw_writel(frqcr3, CPG2_FRQCR3);

	clk->rate = clk->parent->rate / frqcr3_divisors[tmp];

	return 0;
}

static struct sh_clk_ops sh4202_shoc_clk_ops = {
	.init		= shoc_clk_init,
	.recalc		= shoc_clk_recalc,
	.set_rate	= shoc_clk_set_rate,
};

static struct clk sh4202_shoc_clk = {
	.flags		= CLK_ENABLE_ON_INIT,
	.ops		= &sh4202_shoc_clk_ops,
};

static struct clk *sh4202_onchip_clocks[] = {
	&sh4202_emi_clk,
	&sh4202_femi_clk,
	&sh4202_shoc_clk,
};

static struct clk_lookup lookups[] = {
	/* main clocks */
	CLKDEV_CON_ID("emi_clk", &sh4202_emi_clk),
	CLKDEV_CON_ID("femi_clk", &sh4202_femi_clk),
	CLKDEV_CON_ID("shoc_clk", &sh4202_shoc_clk),
};

int __init arch_clk_init(void)
{
	struct clk *clk;
	int i, ret = 0;

	cpg_clk_init();

	clk = clk_get(NULL, "master_clk");
	for (i = 0; i < ARRAY_SIZE(sh4202_onchip_clocks); i++) {
		struct clk *clkp = sh4202_onchip_clocks[i];

		clkp->parent = clk;
		ret |= clk_register(clkp);
	}

	clk_put(clk);

	clkdev_add_table(lookups, ARRAY_SIZE(lookups));

	return ret;
}
