/* linux/arch/arm/plat-s3c24xx/clock.c
 *
 * Copyright (c) 2004-2005 Simtec Electronics
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
#include <linux/sysdev.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/clk.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#include <asm/hardware.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <asm/arch/regs-clock.h>
#include <asm/arch/regs-gpio.h>

#include <asm/plat-s3c24xx/clock.h>
#include <asm/plat-s3c24xx/cpu.h>

/* clock information */

static LIST_HEAD(clocks);

DEFINE_MUTEX(clocks_mutex);

/* enable and disable calls for use with the clk struct */

static int clk_null_enable(struct clk *clk, int enable)
{
	return 0;
}

/* Clock API calls */

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p;
	struct clk *clk = ERR_PTR(-ENOENT);
	int idno;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(p, &clocks, list) {
		if (p->id == idno &&
		    strcmp(id, p->name) == 0 &&
		    try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}

	/* check for the case where a device was supplied, but the
	 * clock that was being searched for is not device specific */

	if (IS_ERR(clk)) {
		list_for_each_entry(p, &clocks, list) {
			if (p->id == -1 && strcmp(id, p->name) == 0 &&
			    try_module_get(p->owner)) {
				clk = p;
				break;
			}
		}
	}

	mutex_unlock(&clocks_mutex);
	return clk;
}

void clk_put(struct clk *clk)
{
	module_put(clk->owner);
}

int clk_enable(struct clk *clk)
{
	if (IS_ERR(clk) || clk == NULL)
		return -EINVAL;

	clk_enable(clk->parent);

	mutex_lock(&clocks_mutex);

	if ((clk->usage++) == 0)
		(clk->enable)(clk, 1);

	mutex_unlock(&clocks_mutex);
	return 0;
}

void clk_disable(struct clk *clk)
{
	if (IS_ERR(clk) || clk == NULL)
		return;

	mutex_lock(&clocks_mutex);

	if ((--clk->usage) == 0)
		(clk->enable)(clk, 0);

	mutex_unlock(&clocks_mutex);
	clk_disable(clk->parent);
}


unsigned long clk_get_rate(struct clk *clk)
{
	if (IS_ERR(clk))
		return 0;

	if (clk->rate != 0)
		return clk->rate;

	if (clk->get_rate != NULL)
		return (clk->get_rate)(clk);

	if (clk->parent != NULL)
		return clk_get_rate(clk->parent);

	return clk->rate;
}

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (!IS_ERR(clk) && clk->round_rate)
		return (clk->round_rate)(clk, rate);

	return rate;
}

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	int ret;

	if (IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);
	ret = (clk->set_rate)(clk, rate);
	mutex_unlock(&clocks_mutex);

	return ret;
}

struct clk *clk_get_parent(struct clk *clk)
{
	return clk->parent;
}

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	int ret = 0;

	if (IS_ERR(clk))
		return -EINVAL;

	mutex_lock(&clocks_mutex);

	if (clk->set_parent)
		ret = (clk->set_parent)(clk, parent);

	mutex_unlock(&clocks_mutex);

	return ret;
}

EXPORT_SYMBOL(clk_get);
EXPORT_SYMBOL(clk_put);
EXPORT_SYMBOL(clk_enable);
EXPORT_SYMBOL(clk_disable);
EXPORT_SYMBOL(clk_get_rate);
EXPORT_SYMBOL(clk_round_rate);
EXPORT_SYMBOL(clk_set_rate);
EXPORT_SYMBOL(clk_get_parent);
EXPORT_SYMBOL(clk_set_parent);

/* base clocks */

struct clk clk_xtal = {
	.name		= "xtal",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

struct clk clk_mpll = {
	.name		= "mpll",
	.id		= -1,
};

struct clk clk_upll = {
	.name		= "upll",
	.id		= -1,
	.parent		= NULL,
	.ctrlbit	= 0,
};

struct clk clk_f = {
	.name		= "fclk",
	.id		= -1,
	.rate		= 0,
	.parent		= &clk_mpll,
	.ctrlbit	= 0,
};

struct clk clk_h = {
	.name		= "hclk",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

struct clk clk_p = {
	.name		= "pclk",
	.id		= -1,
	.rate		= 0,
	.parent		= NULL,
	.ctrlbit	= 0,
};

struct clk clk_usb_bus = {
	.name		= "usb-bus",
	.id		= -1,
	.rate		= 0,
	.parent		= &clk_upll,
};

/* clocks that could be registered by external code */

static int s3c24xx_dclk_enable(struct clk *clk, int enable)
{
	unsigned long dclkcon = __raw_readl(S3C24XX_DCLKCON);

	if (enable)
		dclkcon |= clk->ctrlbit;
	else
		dclkcon &= ~clk->ctrlbit;

	__raw_writel(dclkcon, S3C24XX_DCLKCON);

	return 0;
}

static int s3c24xx_dclk_setparent(struct clk *clk, struct clk *parent)
{
	unsigned long dclkcon;
	unsigned int uclk;

	if (parent == &clk_upll)
		uclk = 1;
	else if (parent == &clk_p)
		uclk = 0;
	else
		return -EINVAL;

	clk->parent = parent;

	dclkcon = __raw_readl(S3C24XX_DCLKCON);

	if (clk->ctrlbit == S3C2410_DCLKCON_DCLK0EN) {
		if (uclk)
			dclkcon |= S3C2410_DCLKCON_DCLK0_UCLK;
		else
			dclkcon &= ~S3C2410_DCLKCON_DCLK0_UCLK;
	} else {
		if (uclk)
			dclkcon |= S3C2410_DCLKCON_DCLK1_UCLK;
		else
			dclkcon &= ~S3C2410_DCLKCON_DCLK1_UCLK;
	}

	__raw_writel(dclkcon, S3C24XX_DCLKCON);

	return 0;
}


static int s3c24xx_clkout_setparent(struct clk *clk, struct clk *parent)
{
	unsigned long mask;
	unsigned long source;

	/* calculate the MISCCR setting for the clock */

	if (parent == &clk_xtal)
		source = S3C2410_MISCCR_CLK0_MPLL;
	else if (parent == &clk_upll)
		source = S3C2410_MISCCR_CLK0_UPLL;
	else if (parent == &clk_f)
		source = S3C2410_MISCCR_CLK0_FCLK;
	else if (parent == &clk_h)
		source = S3C2410_MISCCR_CLK0_HCLK;
	else if (parent == &clk_p)
		source = S3C2410_MISCCR_CLK0_PCLK;
	else if (clk == &s3c24xx_clkout0 && parent == &s3c24xx_dclk0)
		source = S3C2410_MISCCR_CLK0_DCLK0;
	else if (clk == &s3c24xx_clkout1 && parent == &s3c24xx_dclk1)
		source = S3C2410_MISCCR_CLK0_DCLK0;
	else
		return -EINVAL;

	clk->parent = parent;

	if (clk == &s3c24xx_dclk0)
		mask = S3C2410_MISCCR_CLK0_MASK;
	else {
		source <<= 4;
		mask = S3C2410_MISCCR_CLK1_MASK;
	}

	s3c2410_modify_misccr(mask, source);
	return 0;
}

/* external clock definitions */

struct clk s3c24xx_dclk0 = {
	.name		= "dclk0",
	.id		= -1,
	.ctrlbit	= S3C2410_DCLKCON_DCLK0EN,
	.enable	        = s3c24xx_dclk_enable,
	.set_parent	= s3c24xx_dclk_setparent,
};

struct clk s3c24xx_dclk1 = {
	.name		= "dclk1",
	.id		= -1,
	.ctrlbit	= S3C2410_DCLKCON_DCLK0EN,
	.enable		= s3c24xx_dclk_enable,
	.set_parent	= s3c24xx_dclk_setparent,
};

struct clk s3c24xx_clkout0 = {
	.name		= "clkout0",
	.id		= -1,
	.set_parent	= s3c24xx_clkout_setparent,
};

struct clk s3c24xx_clkout1 = {
	.name		= "clkout1",
	.id		= -1,
	.set_parent	= s3c24xx_clkout_setparent,
};

struct clk s3c24xx_uclk = {
	.name		= "uclk",
	.id		= -1,
};

/* initialise the clock system */

int s3c24xx_register_clock(struct clk *clk)
{
	clk->owner = THIS_MODULE;

	if (clk->enable == NULL)
		clk->enable = clk_null_enable;

	/* add to the list of available clocks */

	mutex_lock(&clocks_mutex);
	list_add(&clk->list, &clocks);
	mutex_unlock(&clocks_mutex);

	return 0;
}

int s3c24xx_register_clocks(struct clk **clks, int nr_clks)
{
	int fails = 0;

	for (; nr_clks > 0; nr_clks--, clks++) {
		if (s3c24xx_register_clock(*clks) < 0)
			fails++;
	}

	return fails;
}

/* initalise all the clocks */

int __init s3c24xx_setup_clocks(unsigned long xtal,
				unsigned long fclk,
				unsigned long hclk,
				unsigned long pclk)
{
	printk(KERN_INFO "S3C24XX Clocks, (c) 2004 Simtec Electronics\n");

	/* initialise the main system clocks */

	clk_xtal.rate = xtal;
	clk_upll.rate = s3c2410_get_pll(__raw_readl(S3C2410_UPLLCON), xtal);

	clk_mpll.rate = fclk;
	clk_h.rate = hclk;
	clk_p.rate = pclk;
	clk_f.rate = fclk;

	/* assume uart clocks are correctly setup */

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
