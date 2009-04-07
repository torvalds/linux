/*
 * TI DaVinci clock config file
 *
 * Copyright (C) 2006 Texas Instruments.
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
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/platform_device.h>
#include <linux/io.h>

#include <mach/hardware.h>

#include <mach/psc.h>
#include "clock.h"

/* PLL/Reset register offsets */
#define PLLM		0x110

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clockfw_lock);

static unsigned int commonrate;
static unsigned int armrate;
static unsigned int fixedrate = 27000000;	/* 27 MHZ */

extern void davinci_psc_config(unsigned int domain, unsigned int id, char enable);

/*
 * Returns a clock. Note that we first try to use device id on the bus
 * and clock name. If this fails, we try to use clock name only.
 */
struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);
	int idno;

	if (dev == NULL || dev->bus != &platform_bus_type)
		idno = -1;
	else
		idno = to_platform_device(dev)->id;

	mutex_lock(&clocks_mutex);

	list_for_each_entry(p, &clocks, node) {
		if (p->id == idno &&
		    strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			goto found;
		}
	}

	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}

found:
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	if (clk && !IS_ERR(clk))
		module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);

static int __clk_enable(struct clk *clk)
{
	if (clk->flags & ALWAYS_ENABLED)
		return 0;

	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, clk->lpsc, 1);
	return 0;
}

static void __clk_disable(struct clk *clk)
{
	if (clk->usecount)
		return;

	davinci_psc_config(DAVINCI_GPSC_ARMDOMAIN, clk->lpsc, 0);
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;
	int ret = 0;

	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	if (clk->usecount++ == 0) {
		spin_lock_irqsave(&clockfw_lock, flags);
		ret = __clk_enable(clk);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}

	return ret;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk == NULL || IS_ERR(clk))
		return;

	if (clk->usecount > 0 && !(--clk->usecount)) {
		spin_lock_irqsave(&clockfw_lock, flags);
		__clk_disable(clk);
		spin_unlock_irqrestore(&clockfw_lock, flags);
	}
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return *(clk->rate);
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk == NULL || IS_ERR(clk))
		return -EINVAL;

	return *(clk->rate);
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

	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);

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

static struct clk davinci_clks[] = {
	{
		.name = "ARMCLK",
		.rate = &armrate,
		.lpsc = -1,
		.flags = ALWAYS_ENABLED,
	},
	{
		.name = "UART",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_UART0,
	},
	{
		.name = "EMACCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_EMAC_WRAPPER,
	},
	{
		.name = "I2CCLK",
		.rate = &fixedrate,
		.lpsc = DAVINCI_LPSC_I2C,
	},
	{
		.name = "IDECLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_ATA,
	},
	{
		.name = "McBSPCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_McBSP,
	},
	{
		.name = "MMCSDCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_MMC_SD,
	},
	{
		.name = "SPICLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_SPI,
	},
	{
		.name = "gpio",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_GPIO,
	},
	{
		.name = "usb",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_USB,
	},
	{
		.name = "AEMIFCLK",
		.rate = &commonrate,
		.lpsc = DAVINCI_LPSC_AEMIF,
		.usecount = 1,
	}
};

int __init davinci_clk_init(void)
{
	struct clk *clkp;
	int count = 0;
	u32 pll_mult;

	pll_mult = davinci_readl(DAVINCI_PLL_CNTRL0_BASE + PLLM);
	commonrate = ((pll_mult + 1) * 27000000) / 6;
	armrate = ((pll_mult + 1) * 27000000) / 2;

	for (clkp = davinci_clks; count < ARRAY_SIZE(davinci_clks);
	     count++, clkp++) {
		clk_register(clkp);

		/* Turn on clocks that have been enabled in the
		 * table above */
		if (clkp->usecount)
			clk_enable(clkp);
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

static int davinci_ck_show(struct seq_file *m, void *v)
{
	struct clk *cp;

	list_for_each_entry(cp, &clocks, node)
		seq_printf(m,"%s %d %d\n", cp->name, *(cp->rate), cp->usecount);

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
#endif	/* CONFIG_DEBUG_PROC_FS */
