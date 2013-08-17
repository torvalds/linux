/*
 * Dummy clk implementations for powerpc.
 * These need to be overridden in platform code.
 */

#include <linux/clk.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/export.h>
#include <asm/clk_interface.h>

struct clk_interface clk_functions;

struct clk *clk_get(struct device *dev, const char *id)
{
	if (clk_functions.clk_get)
		return clk_functions.clk_get(dev, id);
	return ERR_PTR(-ENOSYS);
}
EXPORT_SYMBOL(clk_get);

void clk_put(struct clk *clk)
{
	if (clk_functions.clk_put)
		clk_functions.clk_put(clk);
}
EXPORT_SYMBOL(clk_put);

int clk_enable(struct clk *clk)
{
	if (clk_functions.clk_enable)
		return clk_functions.clk_enable(clk);
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_enable);

void clk_disable(struct clk *clk)
{
	if (clk_functions.clk_disable)
		clk_functions.clk_disable(clk);
}
EXPORT_SYMBOL(clk_disable);

unsigned long clk_get_rate(struct clk *clk)
{
	if (clk_functions.clk_get_rate)
		return clk_functions.clk_get_rate(clk);
	return 0;
}
EXPORT_SYMBOL(clk_get_rate);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (clk_functions.clk_round_rate)
		return clk_functions.clk_round_rate(clk, rate);
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_round_rate);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	if (clk_functions.clk_set_rate)
		return clk_functions.clk_set_rate(clk, rate);
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_set_rate);

struct clk *clk_get_parent(struct clk *clk)
{
	if (clk_functions.clk_get_parent)
		return clk_functions.clk_get_parent(clk);
	return ERR_PTR(-ENOSYS);
}
EXPORT_SYMBOL(clk_get_parent);

int clk_set_parent(struct clk *clk, struct clk *parent)
{
	if (clk_functions.clk_set_parent)
		return clk_functions.clk_set_parent(clk, parent);
	return -ENOSYS;
}
EXPORT_SYMBOL(clk_set_parent);
