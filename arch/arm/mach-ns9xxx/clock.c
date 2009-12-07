/*
 * arch/arm/mach-ns9xxx/clock.c
 *
 * Copyright (C) 2007 by Digi International Inc.
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
#include <linux/err.h>
#include <linux/module.h>
#include <linux/list.h>
#include <linux/clk.h>
#include <linux/string.h>
#include <linux/platform_device.h>
#include <linux/semaphore.h>

#include "clock.h"

static LIST_HEAD(clocks);
static DEFINE_SPINLOCK(clk_lock);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *ret = NULL, *retgen = NULL;
	unsigned long flags;
	int idno;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	spin_lock_irqsave(&clk_lock, flags);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0) {
			if (p->id == idno) {
				if (!try_module_get(p->owner))
					continue;
				ret = p;
				break;
			} else if (p->id == -1)
				/* remember match with id == -1 in case there is
				 * no clock for idno */
				retgen = p;
		}
	}

	if (!ret && retgen && try_module_get(retgen->owner))
		ret = retgen;

	if (ret)
		++ret->refcount;

	spin_unlock_irqrestore(&clk_lock, flags);

	return ret ? ret : ERR_PTR(-ENOENT);
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	module_put(clk->owner);
	--clk->refcount;
}
EXPORT_SYMBOL(clk_put);

static int clk_enable_unlocked(struct clk *clk)
{
	int ret = 0;
	if (clk->parent) {
		ret = clk_enable_unlocked(clk->parent);
		if (ret)
			return ret;
	}

	if (clk->usage++ == 0 && clk->endisable)
		ret = clk->endisable(clk, 1);

	return ret;
}

int clk_enable(struct clk *clk)
{
	int ret;
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);

	ret = clk_enable_unlocked(clk);

	spin_unlock_irqrestore(&clk_lock, flags);

	return ret;
}
EXPORT_SYMBOL(clk_enable);

static void clk_disable_unlocked(struct clk *clk)
{
	if (--clk->usage == 0 && clk->endisable)
		clk->endisable(clk, 0);

	if (clk->parent)
		clk_disable_unlocked(clk->parent);
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);

	clk_disable_unlocked(clk);

	spin_unlock_irqrestore(&clk_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk->get_rate)
		return clk->get_rate(clk);

	if (clk->rate)
		return clk->rate;

	if (clk->parent)
		return clk_get_rate(clk->parent);

	return 0;
}
EXPORT_SYMBOL(clk_get_rate);

int clk_register(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);

	list_add(&clk->node, &clocks);

	if (clk->parent)
		++clk->parent->refcount;

	spin_unlock_irqrestore(&clk_lock, flags);

	return 0;
}

int clk_unregister(struct clk *clk)
{
	int ret = 0;
	unsigned long flags;

	spin_lock_irqsave(&clk_lock, flags);

	if (clk->usage || clk->refcount)
		ret = -EBUSY;
	else
		list_del(&clk->node);

	if (clk->parent)
		--clk->parent->refcount;

	spin_unlock_irqrestore(&clk_lock, flags);

	return ret;
}

#if defined CONFIG_DEBUG_FS

#include <linux/debugfs.h>
#include <linux/seq_file.h>

static int clk_debugfs_show(struct seq_file *s, void *null)
{
	unsigned long flags;
	struct clk *p;

	spin_lock_irqsave(&clk_lock, flags);

	list_for_each_entry(p, &clocks, node)
		seq_printf(s, "%s.%d: usage=%lu refcount=%lu rate=%lu\n",
				p->name, p->id, p->usage, p->refcount,
				p->usage ? clk_get_rate(p) : 0);

	spin_unlock_irqrestore(&clk_lock, flags);

	return 0;
}

static int clk_debugfs_open(struct inode *inode, struct file *file)
{
	return single_open(file, clk_debugfs_show, NULL);
}

static const struct file_operations clk_debugfs_operations = {
	.open = clk_debugfs_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int __init clk_debugfs_init(void)
{
	struct dentry *dentry;

	dentry = debugfs_create_file("clk", S_IFREG | S_IRUGO, NULL, NULL,
			&clk_debugfs_operations);
	return IS_ERR(dentry) ? PTR_ERR(dentry) : 0;
}
subsys_initcall(clk_debugfs_init);

#endif /* if defined CONFIG_DEBUG_FS */
