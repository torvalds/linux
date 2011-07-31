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

#include <asm/clkdev.h>
#include <mach/pxa2xx-regs.h>
#include <mach/hardware.h>

#include "devices.h"
#include "generic.h"
#include "clock.h"

static DEFINE_SPINLOCK(clocks_lock);

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
