/*
 * Copyright (c) 2011 Zhang, Keguang <keguang.zhang@gmail.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <linux/module.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/clk.h>
#include <linux/err.h>
#include <asm/clock.h>
#include <asm/time.h>

#include <loongson1.h>

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);

struct clk *clk_get(struct device *dev, const char *name)
{
	struct clk *c;
	struct clk *ret = NULL;

	mutex_lock(&clocks_mutex);
	list_for_each_entry(c, &clocks, node) {
		if (!strcmp(c->name, name)) {
			ret = c;
			break;
		}
	}
	mutex_unlock(&clocks_mutex);

	return ret;
}
EXPORT_SYMBOL(clk_get);

int clk_enable(struct clk *clk)
{
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

static void pll_clk_init(struct clk *clk)
{
	u32 pll;

	pll = __raw_readl(LS1X_CLK_PLL_FREQ);
	clk->rate = (12 + (pll & 0x3f)) * 33 / 2
			+ ((pll >> 8) & 0x3ff) * 33 / 1024 / 2;
	clk->rate *= 1000000;
}

static void cpu_clk_init(struct clk *clk)
{
	u32 pll, ctrl;

	pll = clk_get_rate(clk->parent);
	ctrl = __raw_readl(LS1X_CLK_PLL_DIV) & DIV_CPU;
	clk->rate = pll / (ctrl >> DIV_CPU_SHIFT);
}

static void ddr_clk_init(struct clk *clk)
{
	u32 pll, ctrl;

	pll = clk_get_rate(clk->parent);
	ctrl = __raw_readl(LS1X_CLK_PLL_DIV) & DIV_DDR;
	clk->rate = pll / (ctrl >> DIV_DDR_SHIFT);
}

static void dc_clk_init(struct clk *clk)
{
	u32 pll, ctrl;

	pll = clk_get_rate(clk->parent);
	ctrl = __raw_readl(LS1X_CLK_PLL_DIV) & DIV_DC;
	clk->rate = pll / (ctrl >> DIV_DC_SHIFT);
}

static struct clk_ops pll_clk_ops = {
	.init	= pll_clk_init,
};

static struct clk_ops cpu_clk_ops = {
	.init	= cpu_clk_init,
};

static struct clk_ops ddr_clk_ops = {
	.init	= ddr_clk_init,
};

static struct clk_ops dc_clk_ops = {
	.init	= dc_clk_init,
};

static struct clk pll_clk = {
	.name	= "pll",
	.ops	= &pll_clk_ops,
};

static struct clk cpu_clk = {
	.name	= "cpu",
	.parent = &pll_clk,
	.ops	= &cpu_clk_ops,
};

static struct clk ddr_clk = {
	.name	= "ddr",
	.parent = &pll_clk,
	.ops	= &ddr_clk_ops,
};

static struct clk dc_clk = {
	.name	= "dc",
	.parent = &pll_clk,
	.ops	= &dc_clk_ops,
};

int clk_register(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	if (clk->ops->init)
		clk->ops->init(clk);
	mutex_unlock(&clocks_mutex);

	return 0;
}
EXPORT_SYMBOL(clk_register);

static struct clk *ls1x_clks[] = {
	&pll_clk,
	&cpu_clk,
	&ddr_clk,
	&dc_clk,
};

int __init ls1x_clock_init(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(ls1x_clks); i++)
		clk_register(ls1x_clks[i]);

	return 0;
}

void __init plat_time_init(void)
{
	struct clk *clk;

	/* Initialize LS1X clocks */
	ls1x_clock_init();

	/* setup mips r4k timer */
	clk = clk_get(NULL, "cpu");
	if (IS_ERR(clk))
		panic("unable to get dc clock, err=%ld", PTR_ERR(clk));

	mips_hpt_frequency = clk_get_rate(clk) / 2;
}
