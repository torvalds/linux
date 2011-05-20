/*
 * Clock management for AT32AP CPUs
 *
 * Copyright (C) 2006 Atmel Corporation
 *
 * Based on arch/arm/mach-at91/clock.c
 *   Copyright (C) 2005 David Brownell
 *   Copyright (C) 2005 Ivan Kokshaysky
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk.h>
#include <linux/err.h>
#include <linux/device.h>
#include <linux/string.h>
#include <linux/list.h>

#include <mach/chip.h>

#include "clock.h"

/* at32 clock list */
static LIST_HEAD(at32_clock_list);

static DEFINE_SPINLOCK(clk_lock);
static DEFINE_SPINLOCK(clk_list_lock);

void at32_clk_register(struct clk *clk)
{
	spin_lock(&clk_list_lock);
	/* add the new item to the end of the list */
	list_add_tail(&clk->list, &at32_clock_list);
	spin_unlock(&clk_list_lock);
}

static struct clk *__clk_get(struct device *dev, const char *id)
{
	struct clk *clk;

	list_for_each_entry(clk, &at32_clock_list, list) {
		if (clk->dev == dev && strcmp(id, clk->name) == 0) {
			return clk;
		}
	}

	return ERR_PTR(-ENOENT);
}

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *clk;

	spin_lock(&clk_list_lock);
	clk = __clk_get(dev, id);
	spin_unlock(&clk_list_lock);

	return clk;
}

EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	/* clocks are static for now, we can't free them */
}
EXPORT_SYMBOL(clk_put);

static void __clk_enable(struct clk *clk)
{
	if (clk->parent)
		__clk_enable(clk->parent);
	if (clk->users++ == 0 && clk->mode)
		clk->mode(clk, 1);
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_enable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

static void __clk_disable(struct clk *clk)
{
	if (clk->users == 0) {
		printk(KERN_ERR "%s: mismatched disable\n", clk->name);
		WARN_ON(1);
		return;
	}

	if (--clk->users == 0 && clk->mode)
		clk->mode(clk, 0);
	if (clk->parent)
		__clk_disable(clk->parent);
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);
	__clk_disable(clk);
	spin_unlock_irqrestore(&clk_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long flags;
	unsigned long rate;

	spin_lock_irqsave(&clk_lock, flags);
	rate = clk->get_rate(clk);
	spin_unlock_irqrestore(&clk_lock, flags);

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags, actual_rate;

	if (!clk->set_rate)
		return -ENOSYS;

	spin_lock_irqsave(&clk_lock, flags);
	actual_rate = clk->set_rate(clk, rate, 0);
	spin_unlock_irqrestore(&clk_lock, flags);

	return actual_rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	long ret;

	if (!clk->set_rate)
		return -ENOSYS;

	spin_lock_irqsave(&clk_lock, flags);
	ret = clk->set_rate(clk, rate, 1);
	spin_unlock_irqrestore(&clk_lock, flags);

	return (ret < 0) ? ret : 0;
}
EXPORT_SYMBOL(clk_set_rate);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret;

	if (!clk->set_parent)
		return -ENOSYS;

	spin_lock_irqsave(&clk_lock, flags);
	ret = clk->set_parent(clk, parent);
	spin_unlock_irqrestore(&clk_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_set_parent);

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}
EXPORT_SYMBOL(clk_get_parent);



#ifdef CONFIG_DEBUG_FS

/* /sys/kernel/debug/at32ap_clk */

#include <linux/io.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include "pm.h"


#define	NEST_DELTA	2
#define	NEST_MAX	6

struct clkinf {
	struct seq_file	*s;
	unsigned	nest;
};

static void
dump_clock(struct clk *parent, struct clkinf *r)
{
	unsigned	nest = r->nest;
	char		buf[16 + NEST_MAX];
	struct clk	*clk;
	unsigned	i;

	/* skip clocks coupled to devices that aren't registered */
	if (parent->dev && !dev_name(parent->dev) && !parent->users)
		return;

	/* <nest spaces> name <pad to end> */
	memset(buf, ' ', sizeof(buf) - 1);
	buf[sizeof(buf) - 1] = 0;
	i = strlen(parent->name);
	memcpy(buf + nest, parent->name,
			min(i, (unsigned)(sizeof(buf) - 1 - nest)));

	seq_printf(r->s, "%s%c users=%2d %-3s %9ld Hz",
		buf, parent->set_parent ? '*' : ' ',
		parent->users,
		parent->users ? "on" : "off",	/* NOTE: not-paranoid!! */
		clk_get_rate(parent));
	if (parent->dev)
		seq_printf(r->s, ", for %s", dev_name(parent->dev));
	seq_printf(r->s, "\n");

	/* cost of this scan is small, but not linear... */
	r->nest = nest + NEST_DELTA;

	list_for_each_entry(clk, &at32_clock_list, list) {
		if (clk->parent == parent)
			dump_clock(clk, r);
	}
	r->nest = nest;
}

static int clk_show(struct seq_file *s, void *unused)
{
	struct clkinf	r;
	int		i;
	struct clk 	*clk;

	/* show all the power manager registers */
	seq_printf(s, "MCCTRL  = %8x\n", pm_readl(MCCTRL));
	seq_printf(s, "CKSEL   = %8x\n", pm_readl(CKSEL));
	seq_printf(s, "CPUMASK = %8x\n", pm_readl(CPU_MASK));
	seq_printf(s, "HSBMASK = %8x\n", pm_readl(HSB_MASK));
	seq_printf(s, "PBAMASK = %8x\n", pm_readl(PBA_MASK));
	seq_printf(s, "PBBMASK = %8x\n", pm_readl(PBB_MASK));
	seq_printf(s, "PLL0    = %8x\n", pm_readl(PLL0));
	seq_printf(s, "PLL1    = %8x\n", pm_readl(PLL1));
	seq_printf(s, "IMR     = %8x\n", pm_readl(IMR));
	for (i = 0; i < 8; i++) {
		if (i == 5)
			continue;
		seq_printf(s, "GCCTRL%d = %8x\n", i, pm_readl(GCCTRL(i)));
	}

	seq_printf(s, "\n");

	r.s = s;
	r.nest = 0;
	/* protected from changes on the list while dumping */
	spin_lock(&clk_list_lock);

	/* show clock tree as derived from the three oscillators */
	clk = __clk_get(NULL, "osc32k");
	dump_clock(clk, &r);
	clk_put(clk);

	clk = __clk_get(NULL, "osc0");
	dump_clock(clk, &r);
	clk_put(clk);

	clk = __clk_get(NULL, "osc1");
	dump_clock(clk, &r);
	clk_put(clk);

	spin_unlock(&clk_list_lock);

	return 0;
}

static int clk_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_show, NULL);
}

static const struct file_operations clk_operations = {
	.open		= clk_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int __init clk_debugfs_init(void)
{
	(void) debugfs_create_file("at32ap_clk", S_IFREG | S_IRUGO,
			NULL, NULL, &clk_operations);

	return 0;
}
postcore_initcall(clk_debugfs_init);

#endif
