/* arch/arm/mach-lh7a40x/clocks.c
 *
 *  Copyright (C) 2004 Marc Singer
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  version 2 as published by the Free Software Foundation.
 *
 */

#include <linux/cpufreq.h>
#include <mach/hardware.h>
#include <mach/clocks.h>
#include <linux/err.h>

struct module;
struct icst525_params;

struct clk {
	struct list_head node;
	unsigned long rate;
	struct module *owner;
	const char *name;
//	void *data;
//	const struct icst525_params *params;
//	void (*setvco)(struct clk *, struct icst525_vco vco);
};

int clk_register(struct clk *clk);
void clk_unregister(struct clk *clk);

/* ----- */

#define MAINDIV1(c)	(((c) >>  7) & 0x0f)
#define MAINDIV2(c)	(((c) >> 11) & 0x1f)
#define PS(c)		(((c) >> 18) & 0x03)
#define PREDIV(c)	(((c) >>  2) & 0x1f)
#define HCLKDIV(c)	(((c) >>  0) & 0x02)
#define PCLKDIV(c)	(((c) >> 16) & 0x03)

unsigned int cpufreq_get (unsigned int cpu) /* in kHz */
{
	return fclkfreq_get ()/1000;
}
EXPORT_SYMBOL(cpufreq_get);

unsigned int fclkfreq_get (void)
{
	unsigned int clkset = CSC_CLKSET;
	unsigned int gclk
		= XTAL_IN
		/ (1 << PS(clkset))
		* (MAINDIV1(clkset) + 2)
		/ (PREDIV(clkset)   + 2)
		* (MAINDIV2(clkset) + 2)
		;
	return gclk;
}

unsigned int hclkfreq_get (void)
{
	unsigned int clkset = CSC_CLKSET;
	unsigned int hclk = fclkfreq_get () / (HCLKDIV(clkset) + 1);

	return hclk;
}

unsigned int pclkfreq_get (void)
{
	unsigned int clkset = CSC_CLKSET;
	int pclkdiv = PCLKDIV(clkset);
	unsigned int pclk;
	if (pclkdiv == 0x3)
		pclkdiv = 0x2;
	pclk = hclkfreq_get () / (1 << pclkdiv);

	return pclk;
}

/* ----- */

static LIST_HEAD(clocks);
static DECLARE_MUTEX(clocks_sem);

struct clk *clk_get (struct device *dev, const char *id)
{
	struct clk *p;
	struct clk *clk = ERR_PTR(-ENOENT);

	down (&clocks_sem);
	list_for_each_entry (p, &clocks, node) {
		if (strcmp (id, p->name) == 0
		    && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}
	up (&clocks_sem);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put (struct clk *clk)
{
	module_put(clk->owner);
}
EXPORT_SYMBOL(clk_put);

int clk_enable (struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable (struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

int clk_use (struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_use);

void clk_unuse (struct clk *clk)
{
}
EXPORT_SYMBOL(clk_unuse);

unsigned long clk_get_rate (struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate (struct clk *clk, unsigned long rate)
{
	return rate;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate (struct clk *clk, unsigned long rate)
{
	int ret = -EIO;
	return ret;
}
EXPORT_SYMBOL(clk_set_rate);

#if 0
/*
 * These are fixed clocks.
 */
static struct clk kmi_clk = {
	.name	= "KMIREFCLK",
	.rate	= 24000000,
};

static struct clk uart_clk = {
	.name	= "UARTCLK",
	.rate	= 24000000,
};

static struct clk mmci_clk = {
	.name	= "MCLK",
	.rate	= 33000000,
};
#endif

static struct clk clcd_clk = {
	.name	= "CLCDCLK",
	.rate	= 0,
};

int clk_register (struct clk *clk)
{
	down (&clocks_sem);
	list_add (&clk->node, &clocks);
	up (&clocks_sem);
	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister (struct clk *clk)
{
	down (&clocks_sem);
	list_del (&clk->node);
	up (&clocks_sem);
}
EXPORT_SYMBOL(clk_unregister);

static int __init clk_init (void)
{
	clk_register(&clcd_clk);
	return 0;
}
arch_initcall(clk_init);
