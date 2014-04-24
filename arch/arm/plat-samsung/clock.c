/* linux/arch/arm/plat-s3c24xx/clock.c
 *
 * Copyright 2004-2005 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX Core clock control support
 *
 * Based on, and code from linux/arch/arm/mach-versatile/clock.c
 **
 **  Copyright (C) 2004 ARM Limited.
 **  Written by Deep Blue Solutions Limited.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/io.h>
#if defined(CONFIG_DEBUG_FS)
#include <linux/debugfs.h>
#endif

#include <asm/irq.h>

#include <plat/cpu-freq.h>

#include <plat/clock.h>
#include <plat/cpu.h>

#include <linux/serial_core.h>
#include <linux/serial_s3c.h> /* for s3c24xx_uart_devs */

/* clock information */

static LIST_HEAD(clocks);

/* We originally used an mutex here, but some contexts (see resume)
 * are calling functions such as clk_set_parent() with IRQs disabled
 * causing an BUG to be triggered.
 */
DEFINE_SPINLOCK(clocks_lock);

/* Global watchdog clock used by arch_wtd_reset() callback */
struct clk *s3c2410_wdtclk;
static int __init s3c_wdt_reset_init(void)
{
	s3c2410_wdtclk = clk_get(NULL, "watchdog");
	if (IS_ERR(s3c2410_wdtclk))
		printk(KERN_WARNING "%s: warning: cannot get watchdog clock\n", __func__);
	return 0;
}
arch_initcall(s3c_wdt_reset_init);

/* enable and disable calls for use with the clk struct */

static int clk_null_enable(struct clk *clk, int enable)
{
	return 0;
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	if (IS_ERR(clk) || clk == NULL)
		return -EINVAL;

	clk_enable(clk->parent);

	spin_lock_irqsave(&clocks_lock, flags);

	if ((clk->usage++) == 0)
		(clk->enable)(clk, 1);

	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (IS_ERR(clk) || clk == NULL)
		return;

	spin_lock_irqsave(&clocks_lock, flags);

	if ((--clk->usage) == 0)
		(clk->enable)(clk, 0);

	spin_unlock_irqrestore(&clocks_lock, flags);
	clk_disable(clk->parent);
}


unsigned long clk_get_rate(struct clk *clk)
{
	if (IS_ERR_OR_NULL(clk))
		return 0;

	if (clk->rate != 0)
		return clk->rate;

	if (clk->ops != NULL && clk->ops->get_rate != NULL)
		return (clk->ops->get_rate)(clk);

	if (clk->parent != NULL)
		return clk_get_rate(clk->parent);

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (!IS_ERR_OR_NULL(clk) && clk->ops && clk->ops->round_rate)
		return (clk->ops->round_rate)(clk, rate);

	return rate;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long flags;
	int ret;

	if (IS_ERR_OR_NULL(clk))
		return -EINVAL;

	/* We do not default just do a clk->rate = rate as
	 * the clock may have been made this way by choice.
	 */

	WARN_ON(clk->ops == NULL);
	WARN_ON(clk->ops && clk->ops->set_rate == NULL);

	if (clk->ops == NULL || clk->ops->set_rate == NULL)
		return -EINVAL;

	spin_lock_irqsave(&clocks_lock, flags);
	ret = (clk->ops->set_rate)(clk, rate);
	spin_unlock_irqrestore(&clocks_lock, flags);

	return ret;
}

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	unsigned long flags;
	int ret = 0;

	if (IS_ERR_OR_NULL(clk) || IS_ERR_OR_NULL(parent))
		return -EINVAL;

	spin_lock_irqsave(&clocks_lock, flags);

	if (clk->ops && clk->ops->set_parent)
		ret = (clk->ops->set_parent)(clk, parent);

	spin_unlock_irqrestore(&clocks_lock, flags);

	return ret;
}

EXPORT_SYMBOL(clk_enable);
EXPORT_SYMBOL(clk_disable);
EXPORT_SYMBOL(clk_get_rate);
EXPORT_SYMBOL(clk_round_rate);
EXPORT_SYMBOL(clk_set_rate);
EXPORT_SYMBOL(clk_get_parent);
EXPORT_SYMBOL(clk_set_parent);

/* base clocks */

int clk_default_setrate(struct clk *clk, unsigned long rate)
{
	clk->rate = rate;
	return 0;
}

struct clk_ops clk_ops_def_setrate = {
	.set_rate	= clk_default_setrate,
};

struct clk clk_xtal = {
	.name		= "xtal",
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

struct clk clk_ext = {
	.name		= "ext",
};

struct clk clk_epll = {
	.name		= "epll",
};

struct clk clk_mpll = {
	.name		= "mpll",
	.ops		= &clk_ops_def_setrate,
};

struct clk clk_upll = {
	.name		= "upll",
	.parent		= NULL,
	.ctrlbit	= 0,
};

struct clk clk_f = {
	.name		= "fclk",
	.rate		= 0,
	.parent		= &clk_mpll,
	.ctrlbit	= 0,
};

struct clk clk_h = {
	.name		= "hclk",
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
	.ops		= &clk_ops_def_setrate,
};

struct clk clk_p = {
	.name		= "pclk",
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
	.ops		= &clk_ops_def_setrate,
};

struct clk clk_usb_bus = {
	.name		= "usb-bus",
	.rate		= 0,
	.parent		= &clk_upll,
};


struct clk s3c24xx_uclk = {
	.name		= "uclk",
};

/* initialise the clock system */

/**
 * s3c24xx_register_clock() - register a clock
 * @clk: The clock to register
 *
 * Add the specified clock to the list of clocks known by the system.
 */
int s3c24xx_register_clock(struct clk *clk)
{
	if (clk->enable == NULL)
		clk->enable = clk_null_enable;

	/* fill up the clk_lookup structure and register it*/
	clk->lookup.dev_id = clk->devname;
	clk->lookup.con_id = clk->name;
	clk->lookup.clk = clk;
	clkdev_add(&clk->lookup);

	return 0;
}

/**
 * s3c24xx_register_clocks() - register an array of clock pointers
 * @clks: Pointer to an array of struct clk pointers
 * @nr_clks: The number of clocks in the @clks array.
 *
 * Call s3c24xx_register_clock() for all the clock pointers contained
 * in the @clks list. Returns the number of failures.
 */
int s3c24xx_register_clocks(struct clk **clks, int nr_clks)
{
	int fails = 0;

	for (; nr_clks > 0; nr_clks--, clks++) {
		if (s3c24xx_register_clock(*clks) < 0) {
			struct clk *clk = *clks;
			printk(KERN_ERR "%s: failed to register %p: %s\n",
			       __func__, clk, clk->name);
			fails++;
		}
	}

	return fails;
}

/**
 * s3c_register_clocks() - register an array of clocks
 * @clkp: Pointer to the first clock in the array.
 * @nr_clks: Number of clocks to register.
 *
 * Call s3c24xx_register_clock() on the @clkp array given, printing an
 * error if it fails to register the clock (unlikely).
 */
void __init s3c_register_clocks(struct clk *clkp, int nr_clks)
{
	int ret;

	for (; nr_clks > 0; nr_clks--, clkp++) {
		ret = s3c24xx_register_clock(clkp);

		if (ret < 0) {
			printk(KERN_ERR "Failed to register clock %s (%d)\n",
			       clkp->name, ret);
		}
	}
}

/**
 * s3c_disable_clocks() - disable an array of clocks
 * @clkp: Pointer to the first clock in the array.
 * @nr_clks: Number of clocks to register.
 *
 * for internal use only at initialisation time. disable the clocks in the
 * @clkp array.
 */

void __init s3c_disable_clocks(struct clk *clkp, int nr_clks)
{
	for (; nr_clks > 0; nr_clks--, clkp++)
		(clkp->enable)(clkp, 0);
}

/* initialise all the clocks */

int __init s3c24xx_register_baseclocks(unsigned long xtal)
{
	printk(KERN_INFO "S3C24XX Clocks, Copyright 2004 Simtec Electronics\n");

	clk_xtal.rate = xtal;

	/* register our clocks */

	if (s3c24xx_register_clock(&clk_xtal) < 0)
		printk(KERN_ERR "failed to register master xtal\n");

	if (s3c24xx_register_clock(&clk_mpll) < 0)
		printk(KERN_ERR "failed to register mpll clock\n");

	if (s3c24xx_register_clock(&clk_upll) < 0)
		printk(KERN_ERR "failed to register upll clock\n");

	if (s3c24xx_register_clock(&clk_f) < 0)
		printk(KERN_ERR "failed to register cpu fclk\n");

	if (s3c24xx_register_clock(&clk_h) < 0)
		printk(KERN_ERR "failed to register cpu hclk\n");

	if (s3c24xx_register_clock(&clk_p) < 0)
		printk(KERN_ERR "failed to register cpu pclk\n");

	return 0;
}

#if defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS)
/* debugfs support to trace clock tree hierarchy and attributes */

static struct dentry *clk_debugfs_root;

static void clock_tree_show_one(struct seq_file *s, struct clk *c, int level)
{
	struct clk *child;
	const char *state;
	char buf[255] = { 0 };
	int n = 0;

	if (c->name)
		n = snprintf(buf, sizeof(buf) - 1, "%s", c->name);

	if (c->devname)
		n += snprintf(buf + n, sizeof(buf) - 1 - n, ":%s", c->devname);

	state = (c->usage > 0) ? "on" : "off";

	seq_printf(s, "%*s%-*s %-6s %-3d %-10lu\n",
		   level * 3 + 1, "",
		   50 - level * 3, buf,
		   state, c->usage, clk_get_rate(c));

	list_for_each_entry(child, &clocks, list) {
		if (child->parent != c)
			continue;

		clock_tree_show_one(s, child, level + 1);
	}
}

static int clock_tree_show(struct seq_file *s, void *data)
{
	struct clk *c;
	unsigned long flags;

	seq_printf(s, " clock state ref rate\n");
	seq_printf(s, "----------------------------------------------------\n");

	spin_lock_irqsave(&clocks_lock, flags);

	list_for_each_entry(c, &clocks, list)
		if (c->parent == NULL)
			clock_tree_show_one(s, c, 0);

	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}

static int clock_tree_open(struct inode *inode, struct file *file)
{
	return single_open(file, clock_tree_show, inode->i_private);
}

static const struct file_operations clock_tree_fops = {
	.open		= clock_tree_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int clock_rate_show(void *data, u64 *val)
{
	struct clk *c = data;
	*val = clk_get_rate(c);
	return 0;
}
DEFINE_SIMPLE_ATTRIBUTE(clock_rate_fops, clock_rate_show, NULL, "%llu\n");

static int clk_debugfs_register_one(struct clk *c)
{
	int err;
	struct dentry *d;
	struct clk *pa = c->parent;
	char s[255];
	char *p = s;

	p += sprintf(p, "%s", c->devname);

	d = debugfs_create_dir(s, pa ? pa->dent : clk_debugfs_root);
	if (!d)
		return -ENOMEM;

	c->dent = d;

	d = debugfs_create_u8("usecount", S_IRUGO, c->dent, (u8 *)&c->usage);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}

	d = debugfs_create_file("rate", S_IRUGO, c->dent, c, &clock_rate_fops);
	if (!d) {
		err = -ENOMEM;
		goto err_out;
	}
	return 0;

err_out:
	debugfs_remove_recursive(c->dent);
	return err;
}

static int clk_debugfs_register(struct clk *c)
{
	int err;
	struct clk *pa = c->parent;

	if (pa && !pa->dent) {
		err = clk_debugfs_register(pa);
		if (err)
			return err;
	}

	if (!c->dent) {
		err = clk_debugfs_register_one(c);
		if (err)
			return err;
	}
	return 0;
}

static int __init clk_debugfs_init(void)
{
	struct clk *c;
	struct dentry *d;
	int err = -ENOMEM;

	d = debugfs_create_dir("clock", NULL);
	if (!d)
		return -ENOMEM;
	clk_debugfs_root = d;

	d = debugfs_create_file("clock_tree", S_IRUGO, clk_debugfs_root, NULL,
				 &clock_tree_fops);
	if (!d)
		goto err_out;

	list_for_each_entry(c, &clocks, list) {
		err = clk_debugfs_register(c);
		if (err)
			goto err_out;
	}
	return 0;

err_out:
	debugfs_remove_recursive(clk_debugfs_root);
	return err;
}
late_initcall(clk_debugfs_init);

#endif /* defined(CONFIG_PM_DEBUG) && defined(CONFIG_DEBUG_FS) */
