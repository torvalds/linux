/*
 *  linux/arch/arm/mach-sa1100/clock.c
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/platform_device.h>
#include <linux/delay.h>

#include <asm/arch/pxa-regs.h>
#include <asm/arch/pxa2xx-gpio.h>
#include <asm/hardware.h>

#include "devices.h"
#include "generic.h"
#include "clock.h"

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clocks_lock);

static struct clk *clk_lookup(struct device *dev, const char *id)
{
	struct clk *p;

	list_for_each_entry(p, &clocks, node)
		if (strcmp(id, p->name) == 0 && p->dev == dev)
			return p;

	return NULL;
}

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	mutex_lock(&clocks_mutex);
	p = clk_lookup(dev, id);
	if (!p)
		p = clk_lookup(NULL, id);
	if (p)
		clk = p;
	mutex_unlock(&clocks_mutex);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	spin_lock_irqsave(&clocks_lock, flags);
	if (clk->enabled++ == 0)
		clk->ops->enable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);

	if (clk->delay)
		udelay(clk->delay);

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	WARN_ON(clk->enabled == 0);

	spin_lock_irqsave(&clocks_lock, flags);
	if (--clk->enabled == 0)
		clk->ops->disable(clk);
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	unsigned long rate;

	rate = clk->rate;
	if (clk->ops->getrate)
		rate = clk->ops->getrate(clk);

	return rate;
}
EXPORT_SYMBOL(clk_get_rate);


static void clk_gpio27_enable(struct clk *clk)
{
	pxa_gpio_mode(GPIO11_3_6MHz_MD);
}

static void clk_gpio27_disable(struct clk *clk)
{
}

static const struct clkops clk_gpio27_ops = {
	.enable		= clk_gpio27_enable,
	.disable	= clk_gpio27_disable,
};


void clk_cken_enable(struct clk *clk)
{
	CKEN |= 1 << clk->cken;
}

void clk_cken_disable(struct clk *clk)
{
	CKEN &= ~(1 << clk->cken);
}

const struct clkops clk_cken_ops = {
	.enable		= clk_cken_enable,
	.disable	= clk_cken_disable,
};

static struct clk common_clks[] = {
	{
		.name		= "GPIO27_CLK",
		.ops		= &clk_gpio27_ops,
		.rate		= 3686400,
	},
};

void clks_register(struct clk *clks, size_t num)
{
	int i;

	mutex_lock(&clocks_mutex);
	for (i = 0; i < num; i++)
		list_add(&clks[i].node, &clocks);
	mutex_unlock(&clocks_mutex);
}

static int __init clk_init(void)
{
	clks_register(common_clks, ARRAY_SIZE(common_clks));
	return 0;
}
arch_initcall(clk_init);
