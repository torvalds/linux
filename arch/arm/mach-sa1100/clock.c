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
#include <linux/mutex.h>

#include <mach/hardware.h>

/*
 * Very simple clock implementation - we only have one clock to
 * deal with at the moment, so we only match using the "name".
 */
struct clk {
	struct list_head	node;
	unsigned long		rate;
	const char		*name;
	unsigned int		enabled;
	void			(*enable)(void);
	void			(*disable)(void);
};

static LIST_HEAD(clocks);
static DEFINE_MUTEX(clocks_mutex);
static DEFINE_SPINLOCK(clocks_lock);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	mutex_lock(&clocks_mutex);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0) {
			clk = p;
			break;
		}
	}
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
		clk->enable();
	spin_unlock_irqrestore(&clocks_lock, flags);
	return 0;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	unsigned long flags;

	WARN_ON(clk->enabled == 0);

	spin_lock_irqsave(&clocks_lock, flags);
	if (--clk->enabled == 0)
		clk->disable();
	spin_unlock_irqrestore(&clocks_lock, flags);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	return clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);


static void clk_gpio27_enable(void)
{
	/*
	 * First, set up the 3.6864MHz clock on GPIO 27 for the SA-1111:
	 * (SA-1110 Developer's Manual, section 9.1.2.1)
	 */
	GAFR |= GPIO_32_768kHz;
	GPDR |= GPIO_32_768kHz;
	TUCR = TUCR_3_6864MHz;
}

static void clk_gpio27_disable(void)
{
	TUCR = 0;
	GPDR &= ~GPIO_32_768kHz;
	GAFR &= ~GPIO_32_768kHz;
}

static struct clk clk_gpio27 = {
	.name		= "SA1111_CLK",
	.rate		= 3686400,
	.enable		= clk_gpio27_enable,
	.disable	= clk_gpio27_disable,
};

int clk_register(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_add(&clk->node, &clocks);
	mutex_unlock(&clocks_mutex);
	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	mutex_lock(&clocks_mutex);
	list_del(&clk->node);
	mutex_unlock(&clocks_mutex);
}
EXPORT_SYMBOL(clk_unregister);

static int __init clk_init(void)
{
	clk_register(&clk_gpio27);
	return 0;
}
arch_initcall(clk_init);
