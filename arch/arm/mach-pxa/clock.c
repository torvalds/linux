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

#include <asm/arch/pxa-regs.h>
#include <asm/hardware.h>
#include <asm/semaphore.h>

struct clk {
	struct list_head	node;
	unsigned long		rate;
	struct module		*owner;
	const char		*name;
	unsigned int		enabled;
	void			(*enable)(void);
	void			(*disable)(void);
};

static LIST_HEAD(clocks);
static DECLARE_MUTEX(clocks_sem);
static DEFINE_SPINLOCK(clocks_lock);

struct clk *clk_get(struct device *dev, const char *id)
{
	struct clk *p, *clk = ERR_PTR(-ENOENT);

	down(&clocks_sem);
	list_for_each_entry(p, &clocks, node) {
		if (strcmp(id, p->name) == 0 && try_module_get(p->owner)) {
			clk = p;
			break;
		}
	}
	up(&clocks_sem);

	return clk;
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	module_put(clk->owner);
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
	pxa_gpio_mode(GPIO11_3_6MHz_MD);
}

static void clk_gpio27_disable(void)
{
}

static struct clk clk_gpio27 = {
	.name		= "GPIO27_CLK",
	.rate		= 3686400,
	.enable		= clk_gpio27_enable,
	.disable	= clk_gpio27_disable,
};

int clk_register(struct clk *clk)
{
	down(&clocks_sem);
	list_add(&clk->node, &clocks);
	up(&clocks_sem);
	return 0;
}
EXPORT_SYMBOL(clk_register);

void clk_unregister(struct clk *clk)
{
	down(&clocks_sem);
	list_del(&clk->node);
	up(&clocks_sem);
}
EXPORT_SYMBOL(clk_unregister);

static int __init clk_init(void)
{
	clk_register(&clk_gpio27);
	return 0;
}
arch_initcall(clk_init);
