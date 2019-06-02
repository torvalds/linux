// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Clock and PLL control for C64x+ devices
 *
 * Copyright (C) 2010, 2011 Texas Instruments.
 * Contributed by: Mark Salter <msalter@redhat.com>
 *
 * Copied heavily from arm/mach-davinci/clock.c, so:
 *
 * Copyright (C) 2006-2007 Texas Instruments.
 * Copyright (C) 2008-2009 Deep Root Systems, LLC
 */

#include <linux/module.h>
#include <linux/clkdev.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/err.h>

#include <asm/clock.h>
#include <asm/soc.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static void __clk_enable(struct clk *clk)
{
	if (clk->parent)
		__clk_enable(clk->parent);
	clk->usecount++;
}

static void __clk_disable(struct clk *clk)
{
	if (WARN_ON(clk->usecount == 0))
		return;
	--clk->usecount;

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

	if (clk->round_rate)
		return clk->round_rate(clk, rate);

	return clk->rate;
}
EXPORT_SYMBOL(clk_round_rate);

/* Propagate rate to children */
static void propagate_rate(struct clk *root)
{
	struct clk *clk;

	list_for_each_entry(clk, &root->children, childnode) {
		if (clk->recalc)
			clk->rate = clk->recalc(clk);
		propagate_rate(clk);
	}
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret = -EINVAL;

	if (clk == NULL || IS_ERR(clk))
		return ret;

	if (clk->set_rate)
		ret = clk->set_rate(clk, rate);

	spin_lock_irqsave(&clockfw_lock, flags);
	if (ret == 0) {
		if (clk->recalc)
			clk->rate = clk->recalc(clk);
		propagate_rate(clk);
	}
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	/* Cannot change parent on enabled clock */
	if (WARN_ON(clk->usecount))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	clk->parent = parent;
	list_del_init(&clk->childnode);
	list_add(&clk->childnode, &clk->parent->children);
	mutex_unlock(&clocks_mutex);

	spin_lock_irqsave(&clockfw_lock, flags);
	if (clk->recalc)
		clk->rate = clk->recalc(clk);
	propagate_rate(clk);
	spin_unlock_irqrestore(&clockfw_lock, flags);

	return 0;
}
EXPORT_SYMBOL(clk_set_parent);

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
	if (clk->parent)
		list_add_tail(&clk->childnode, &clk->parent->children);
	mutex_unlock(&clocks_mutex);

	/* If rate is already set, use it */
	if (clk->rate)
		return 0;

	/* Else, see if there is a way to calculate it */
	if (clk->recalc)
		clk->rate = clk->recalc(clk);

	/* Otherwise, default to parent rate */
	else if (clk->parent)
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
	list_del(&clk->childnode);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);


static u32 pll_read(struct pll_data *pll, int reg)
{
	return soc_readl(pll->base + reg);
}

static unsigned long clk_sysclk_recalc(struct clk *clk)
{
	u32 v, plldiv = 0;
	struct pll_data *pll;
	unsigned long rate = clk->rate;

	if (WARN_ON(!clk->parent))
		return rate;

	rate = clk->parent->rate;

	/* the parent must be a PLL */
	if (WARN_ON(!clk->parent->pll_data))
		return rate;

	pll = clk->parent->pll_data;

	/* If pre-PLL, source clock is before the multiplier and divider(s) */
	if (clk->flags & PRE_PLL)
		rate = pll->input_rate;

	if (!clk->div) {
		pr_debug("%s: (no divider) rate = %lu KHz\n",
			 clk->name, rate / 1000);
		return rate;
	}

	if (clk->flags & FIXED_DIV_PLL) {
		rate /= clk->div;
		pr_debug("%s: (fixed divide by %d) rate = %lu KHz\n",
			 clk->name, clk->div, rate / 1000);
		return rate;
	}

	v = pll_read(pll, clk->div);
	if (v & PLLDIV_EN)
		plldiv = (v & PLLDIV_RATIO_MASK) + 1;

	if (plldiv == 0)
		plldiv = 1;

	rate /= plldiv;

	pr_debug("%s: (divide by %d) rate = %lu KHz\n",
		 clk->name, plldiv, rate / 1000);

	return rate;
}

static unsigned long clk_leafclk_recalc(struct clk *clk)
{
	if (WARN_ON(!clk->parent))
		return clk->rate;

	pr_debug("%s: (parent %s) rate = %lu KHz\n",
		 clk->name, clk->parent->name,	clk->parent->rate / 1000);

	return clk->parent->rate;
}

static unsigned long clk_pllclk_recalc(struct clk *clk)
{
	u32 ctrl, mult = 0, prediv = 0, postdiv = 0;
	u8 bypass;
	struct pll_data *pll = clk->pll_data;
	unsigned long rate = clk->rate;

	if (clk->flags & FIXED_RATE_PLL)
		return rate;

	ctrl = pll_read(pll, PLLCTL);
	rate = pll->input_rate = clk->parent->rate;

	if (ctrl & PLLCTL_PLLEN)
		bypass = 0;
	else
		bypass = 1;

	if (pll->flags & PLL_HAS_MUL) {
		mult = pll_read(pll, PLLM);
		mult = (mult & PLLM_PLLM_MASK) + 1;
	}
	if (pll->flags & PLL_HAS_PRE) {
		prediv = pll_read(pll, PLLPRE);
		if (prediv & PLLDIV_EN)
			prediv = (prediv & PLLDIV_RATIO_MASK) + 1;
		else
			prediv = 0;
	}
	if (pll->flags & PLL_HAS_POST) {
		postdiv = pll_read(pll, PLLPOST);
		if (postdiv & PLLDIV_EN)
			postdiv = (postdiv & PLLDIV_RATIO_MASK) + 1;
		else
			postdiv = 1;
	}

	if (!bypass) {
		if (prediv)
			rate /= prediv;
		if (mult)
			rate *= mult;
		if (postdiv)
			rate /= postdiv;

		pr_debug("PLL%d: input = %luMHz, pre[%d] mul[%d] post[%d] "
			 "--> %luMHz output.\n",
			 pll->num, clk->parent->rate / 1000000,
			 prediv, mult, postdiv, rate / 1000000);
	} else
		pr_debug("PLL%d: input = %luMHz, bypass mode.\n",
			 pll->num, clk->parent->rate / 1000000);

	return rate;
}


static void __init __init_clk(struct clk *clk)
{
	INIT_LIST_HEAD(&clk->node);
	INIT_LIST_HEAD(&clk->children);
	INIT_LIST_HEAD(&clk->childnode);

	if (!clk->recalc) {

		/* Check if clock is a PLL */
		if (clk->pll_data)
			clk->recalc = clk_pllclk_recalc;

		/* Else, if it is a PLL-derived clock */
		else if (clk->flags & CLK_PLL)
			clk->recalc = clk_sysclk_recalc;

		/* Otherwise, it is a leaf clock (PSC clock) */
		else if (clk->parent)
			clk->recalc = clk_leafclk_recalc;
	}
}

void __init c6x_clks_init(struct clk_lookup *clocks)
{
	struct clk_lookup *c;
	struct clk *clk;
	size_t num_clocks = 0;

	for (c = clocks; c->clk; c++) {
		clk = c->clk;

		__init_clk(clk);
		clk_register(clk);
		num_clocks++;

		/* Turn on clocks that Linux doesn't otherwise manage */
		if (clk->flags & ALWAYS_ENABLED)
			clk_enable(clk);
	}

	clkdev_add_table(clocks, num_clocks);
}

#ifdef CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>

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
	list_for_each_entry(clk, &parent->children, childnode) {
		dump_clock(s, nest + NEST_DELTA, clk);
	}
}

static int c6x_ck_show(struct seq_file *m, void *v)
{
	struct clk *clk;

	/*
	 * Show clock tree; We trust nonzero usecounts equate to PSC enables...
	 */
	mutex_lock(&clocks_mutex);
	list_for_each_entry(clk, &clocks, node)
		if (!clk->parent)
			dump_clock(m, 0, clk);
	mutex_unlock(&clocks_mutex);

	return 0;
}

static int c6x_ck_open(struct inode *inode, struct file *file)
{
	return single_open(file, c6x_ck_show, NULL);
}

static const struct file_operations c6x_ck_operations = {
	.open		= c6x_ck_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init c6x_clk_debugfs_init(void)
{
	debugfs_create_file("c6x_clocks", S_IFREG | S_IRUGO, NULL, NULL,
			    &c6x_ck_operations);

	return 0;
}
device_initcall(c6x_clk_debugfs_init);
#endif /* CONFIG_DEBUG_FS */
