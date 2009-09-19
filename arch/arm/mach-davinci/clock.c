/*
 * Clock and PLL control for DaVinci devices
 *
 * Copyright (C) 2006-2007 Texas Instruments.
 * Copyright (C) 2008-2009 Deep Root Systems, LLC
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/errno.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include <mach/psc.h>
#include <mach/cputype.h>
#include "clock.h"

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static unsigned psc_domain(struct clk *clk)
{
	return (clk->flags & PSC_DSP)
		? DAVINCI_GPSC_DSPDOMAIN
		: DAVINCI_GPSC_ARMDOMAIN;
}

static void __clk_enable(struct clk *clk)
{
	if (clk->parent)
		__clk_enable(clk->parent);
	if (clk->usecount++ == 0 && (clk->flags & CLK_PSC))
		davinci_psc_config(psc_domain(clk), clk->psc_ctlr,
				clk->lpsc, 1);
}

static void __clk_disable(struct clk *clk)
{
	if (WARN_ON(clk->usecount == 0))
		return;
	if (--clk->usecount == 0 && !(clk->flags & CLK_PLL))
		davinci_psc_config(psc_domain(clk), clk->psc_ctlr,
				clk->lpsc, 0);
	if (clk->parent)
		__clk_disable(clk->parent);
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	spin_lock_irqsave(&clockfw_lock, flags);
	__clk_enable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	spin_lock_irqsave(&clockfw_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return clk->rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/* changing the clk rate is not supported */
	return -EINVAL;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_register(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (WARN(clk->parent && !clk->parent->rate,
			"CLK: %s parent %s has no rate!\n",
			clk->name, clk->parent->name))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	list_add_tail(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);

	/* If rate is already set, use it */
	if (clk->rate)
		return 0;

	/* Otherwise, default to parent rate */
	if (clk->parent)
		clk->rate = clk->parent->rate;

	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return;

	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

#ifdef CONFIG_DAVINCI_RESET_CLOCKS
/*
 * Disable any unused clocks left on by the bootloader
 */
static int __init clk_disable_unused(void)
{
	struct clk *ck;

	spin_lock_irq(&clockfw_lock);
	list_for_each_entry(ck, &clocks, node) {
		if (ck->usecount > 0)
			continue;
		if (!(ck->flags & CLK_PSC))
			continue;

		/* ignore if in Disabled or SwRstDisable states */
		if (!davinci_psc_is_clk_active(ck->psc_ctlr, ck->lpsc))
			continue;

		pr_info("Clocks: disable unused %s\n", ck->name);
		davinci_psc_config(psc_domain(ck), ck->psc_ctlr, ck->lpsc, 0);
	}
	spin_unlock_irq(&clockfw_lock);

	return 0;
}
late_initcall(clk_disable_unused);
#endif

static void clk_sysclk_recalc(struct clk *clk)
{
	u32 v, plldiv;
	struct pll_data *pll;

	/* If this is the PLL base clock, no more calculations needed */
	if (clk->pll_data)
		return;

	if (WARN_ON(!clk->parent))
		return;

	clk->rate = clk->parent->rate;

	/* Otherwise, the parent must be a PLL */
	if (WARN_ON(!clk->parent->pll_data))
		return;

	pll = clk->parent->pll_data;

	/* If pre-PLL, source clock is before the multiplier and divider(s) */
	if (clk->flags & PRE_PLL)
		clk->rate = pll->input_rate;

	if (!clk->div_reg)
		return;

	v = __raw_readl(pll->base + clk->div_reg);
	if (v & PLLDIV_EN) {
		plldiv = (v & PLLDIV_RATIO_MASK) + 1;
		if (plldiv)
			clk->rate /= plldiv;
	}
}

static void __init clk_pll_init(struct clk *clk)
{
	u32 ctrl, mult = 1, prediv = 1, postdiv = 1;
	u8 bypass;
	struct pll_data *pll = clk->pll_data;

	pll->base = IO_ADDRESS(pll->phys_base);
	ctrl = __raw_readl(pll->base + PLLCTL);
	clk->rate = pll->input_rate = clk->parent->rate;

	if (ctrl & PLLCTL_PLLEN) {
		bypass = 0;
		mult = __raw_readl(pll->base + PLLM);
		if (cpu_is_davinci_dm365())
			mult = 2 * (mult & PLLM_PLLM_MASK);
		else
			mult = (mult & PLLM_PLLM_MASK) + 1;
	} else
		bypass = 1;

	if (pll->flags & PLL_HAS_PREDIV) {
		prediv = __raw_readl(pll->base + PREDIV);
		if (prediv & PLLDIV_EN)
			prediv = (prediv & PLLDIV_RATIO_MASK) + 1;
		else
			prediv = 1;
	}

	/* pre-divider is fixed, but (some?) chips won't report that */
	if (cpu_is_davinci_dm355() && pll->num == 1)
		prediv = 8;

	if (pll->flags & PLL_HAS_POSTDIV) {
		postdiv = __raw_readl(pll->base + POSTDIV);
		if (postdiv & PLLDIV_EN)
			postdiv = (postdiv & PLLDIV_RATIO_MASK) + 1;
		else
			postdiv = 1;
	}

	if (!bypass) {
		clk->rate /= prediv;
		clk->rate *= mult;
		clk->rate /= postdiv;
	}

	pr_debug("PLL%d: input = %lu MHz [ ",
		 pll->num, clk->parent->rate / 1000000);
	if (bypass)
		pr_debug("bypass ");
	if (prediv > 1)
		pr_debug("/ %d ", prediv);
	if (mult > 1)
		pr_debug("* %d ", mult);
	if (postdiv > 1)
		pr_debug("/ %d ", postdiv);
	pr_debug("] --> %lu MHz output.\n", clk->rate / 1000000);
}

int __init davinci_clk_init(struct davinci_clk *clocks)
  {
	struct davinci_clk *c;
	struct clk *clk;

	for (c = clocks; c->lk.clk; c++) {
		clk = c->lk.clk;

		if (clk->pll_data)
			clk_pll_init(clk);

		/* Calculate rates for PLL-derived clocks */
		else if (clk->flags & CLK_PLL)
			clk_sysclk_recalc(clk);

		if (clk->lpsc)
			clk->flags |= CLK_PSC;

		clkdev_add(&c->lk);
		clk_register(clk);

		/* Turn on clocks that Linux doesn't otherwise manage */
		if (clk->flags & ALWAYS_ENABLED)
			clk_enable(clk);
	}

	return 0;
}

#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#include <linux/seq_file.h>

static void *davinci_ck_start(struct seq_file *m, loff_t *pos)
{
	return *pos < 1 ? (void *)1 : NULL;
}

static void *davinci_ck_next(struct seq_file *m, void *v, loff_t *pos)
{
	++*pos;
	return NULL;
}

static void davinci_ck_stop(struct seq_file *m, void *v)
{
}

#define CLKNAME_MAX	10		/* longest clock name */
#define NEST_DELTA	2
#define NEST_MAX	4

static void
dump_clock(struct seq_file *s, unsigned nest, struct clk *parent)
{
	char		*state;
	char		buf[CLKNAME_MAX + NEST_DELTA * NEST_MAX];
	struct clk	*clk;
	unsigned	i;

	if (parent->flags & CLK_PLL)
		state = "pll";
	else if (parent->flags & CLK_PSC)
		state = "psc";
	else
		state = "";

	/* <nest spaces> name <pad to end> */
	memset(buf, ' ', sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	i = strlen(parent->name);
	memcpy(buf + nest, parent->name,
			min(i, (unsigned)(sizeof(buf) - 1 - nest)));

	seq_printf(s, "%s users=%2d %-3s %9ld Hz\n",
		   buf, parent->usecount, state, clk_get_rate(parent));
	/* REVISIT show device associations too */

	/* cost is now small, but not linear... */
	list_for_each_entry(clk, &clocks, node) {
		if (clk->parent == parent)
			dump_clock(s, nest + NEST_DELTA, clk);
	}
}

static int davinci_ck_show(struct seq_file *m, void *v)
{
	/* Show clock tree; we know the main oscillator is first.
	 * We trust nonzero usecounts equate to PSC enables...
	 */
	mutex_lock(&clocks_mutex);
	if (!list_empty(&clocks))
		dump_clock(m, 0, list_first_entry(&clocks, struct clk, node));
	mutex_unlock(&clocks_mutex);

	return 0;
}

static const struct seq_operations davinci_ck_op = {
	.start	= davinci_ck_start,
	.next	= davinci_ck_next,
	.stop	= davinci_ck_stop,
	.show	= davinci_ck_show
};

static int davinci_ck_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &davinci_ck_op);
}

static const struct file_operations proc_davinci_ck_operations = {
	.open		= davinci_ck_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static int __init davinci_ck_proc_init(void)
{
	proc_create("davinci_clocks", 0, NULL, &proc_davinci_ck_operations);
	return 0;

}
__initcall(davinci_ck_proc_init);
#endif /* CONFIG_DEBUG_PROC_FS */
