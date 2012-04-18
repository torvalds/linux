/*
 *  linux/arch/arm/mach-sa1100/clock.c
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/string.h>
#include <linux/clk.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/io.h>
#include <linux/clkdev.h>

#include <mach/hardware.h>

struct clkops {
	void			(*enable)(struct clk *);
	void			(*disable)(struct clk *);
};

struct clk {
	const struct clkops	*ops;
	unsigned int		enabled;
};

#define DEFINE_CLK(_name, _ops)				\
struct clk clk_##_name = {				\
		.ops	= _ops,				\
	}

static DEFINE_SPINLOCK(clocks_lock);

static void clk_gpio27_enable(struct clk *clk)
{
	/*
	 * First, set up the 3.6864MHz clock on GPIO 27 for the SA-1111:
	 * (SA-1110 Developer's Manual, section 9.1.2.1)
	 */
	GAFR |= GPIO_32_768kHz;
	GPDR |= GPIO_32_768kHz;
	TUCR = TUCR_3_6864MHz;
}

static void clk_gpio27_disable(struct clk *clk)
{
	TUCR = 0;
	GPDR &= ~GPIO_32_768kHz;
	GAFR &= ~GPIO_32_768kHz;
}

int clk_enable(struct clk *clk)
{
	unsigned long flags;

	if (clk) {
		spin_lock_irqsave(&clocks_lock, flags);
		if (clk->enabled++ == 0)
			clk->ops->enable(clk);
		spin_unlock_irqrestore(&clocks_lock, flags);
	}

	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	if (clk) {
		WARN_ON(clk->enabled == 0);
		spin_lock_irqsave(&clocks_lock, flags);
		if (--clk->enabled == 0)
			clk->ops->disable(clk);
		spin_unlock_irqrestore(&clocks_lock, flags);
	}
}
EXPORT_SYMBOL(clk_disable);

const struct clkops clk_gpio27_ops = {
	.enable		= clk_gpio27_enable,
	.disable	= clk_gpio27_disable,
};

static DEFINE_CLK(gpio27, &clk_gpio27_ops);

static struct clk_lookup sa11xx_clkregs[] = {
	CLKDEV_INIT("sa1111.0", NULL, &clk_gpio27),
	CLKDEV_INIT("sa1100-rtc", NULL, NULL),
};

static int __init sa11xx_clk_init(void)
{
	clkdev_add_table(sa11xx_clkregs, ARRAY_SIZE(sa11xx_clkregs));
	return 0;
}
core_initcall(sa11xx_clk_init);
