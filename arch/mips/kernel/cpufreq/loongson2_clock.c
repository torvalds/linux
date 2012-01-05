/*
 * Copyright (C) 2006 - 2008 Lemote Inc. & Insititute of Computing Technology
 * Author: Yanhua, yanh@lemote.com
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */

#include <linux/module.h>
#include <linux/cpufreq.h>
#include <linux/platform_device.h>

#include <asm/clock.h>

#include <loongson.h>

static LIST_HEAD(clock_list);
static DEFINE_SPINLOCK(clock_lock);
static DEFINE_MUTEX(clock_list_sem);

/* Minimum CLK support */
enum {
	DC_ZERO, DC_25PT = 2, DC_37PT, DC_50PT, DC_62PT, DC_75PT,
	DC_87PT, DC_DISABLE, DC_RESV
};

struct cpufreq_frequency_table loongson2_clockmod_table[] = {
	{DC_RESV, CPUFREQ_ENTRY_INVALID},
	{DC_ZERO, CPUFREQ_ENTRY_INVALID},
	{DC_25PT, 0},
	{DC_37PT, 0},
	{DC_50PT, 0},
	{DC_62PT, 0},
	{DC_75PT, 0},
	{DC_87PT, 0},
	{DC_DISABLE, 0},
	{DC_RESV, CPUFREQ_TABLE_END},
};
EXPORT_SYMBOL_GPL(loongson2_clockmod_table);

static struct clk cpu_clk = {
	.name = "cpu_clk",
	.flags = CLK_ALWAYS_ENABLED | CLK_RATE_PROPAGATES,
	.rate = 800000000,
};

struct clk *clk_get(struct device *dev, const char *id)
{
	return &cpu_clk;
}
EXPORT_SYMBOL(clk_get);

static void propagate_rate(struct clk *clk)
{
	struct clk *clkp;

	list_for_each_entry(clkp, &clock_list, node) {
		if (likely(clkp->parent != clk))
			continue;
		if (likely(clkp->ops && clkp->ops->recalc))
			clkp->ops->recalc(clkp);
		if (unlikely(clkp->flags & CLK_RATE_PROPAGATES))
			propagate_rate(clkp);
	}
}

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
	return (unsigned long)clk->rate;
}
EXPORT_SYMBOL(clk_get_rate);

void clk_put(struct clk *clk)
{
}
EXPORT_SYMBOL(clk_put);

int clk_set_rate(struct clk *clk, unsigned long rate)
{
	return clk_set_rate_ex(clk, rate, 0);
}
EXPORT_SYMBOL_GPL(clk_set_rate);

int clk_set_rate_ex(struct clk *clk, unsigned long rate, int algo_id)
{
	int ret = 0;
	int regval;
	int i;

	if (likely(clk->ops && clk->ops->set_rate)) {
		unsigned long flags;

		spin_lock_irqsave(&clock_lock, flags);
		ret = clk->ops->set_rate(clk, rate, algo_id);
		spin_unlock_irqrestore(&clock_lock, flags);
	}

	if (unlikely(clk->flags & CLK_RATE_PROPAGATES))
		propagate_rate(clk);

	for (i = 0; loongson2_clockmod_table[i].frequency != CPUFREQ_TABLE_END;
	     i++) {
		if (loongson2_clockmod_table[i].frequency ==
		    CPUFREQ_ENTRY_INVALID)
			continue;
		if (rate == loongson2_clockmod_table[i].frequency)
			break;
	}
	if (rate != loongson2_clockmod_table[i].frequency)
		return -ENOTSUPP;

	clk->rate = rate;

	regval = LOONGSON_CHIPCFG0;
	regval = (regval & ~0x7) | (loongson2_clockmod_table[i].index - 1);
	LOONGSON_CHIPCFG0 = regval;

	return ret;
}
EXPORT_SYMBOL_GPL(clk_set_rate_ex);

long clk_round_rate(struct clk *clk, unsigned long rate)
{
	if (likely(clk->ops && clk->ops->round_rate)) {
		unsigned long flags, rounded;

		spin_lock_irqsave(&clock_lock, flags);
		rounded = clk->ops->round_rate(clk, rate);
		spin_unlock_irqrestore(&clock_lock, flags);

		return rounded;
	}

	return rate;
}
EXPORT_SYMBOL_GPL(clk_round_rate);

/*
 * This is the simple version of Loongson-2 wait, Maybe we need do this in
 * interrupt disabled content
 */

DEFINE_SPINLOCK(loongson2_wait_lock);
void loongson2_cpu_wait(void)
{
	u32 cpu_freq;
	unsigned long flags;

	spin_lock_irqsave(&loongson2_wait_lock, flags);
	cpu_freq = LOONGSON_CHIPCFG0;
	LOONGSON_CHIPCFG0 &= ~0x7;	/* Put CPU into wait mode */
	LOONGSON_CHIPCFG0 = cpu_freq;	/* Restore CPU state */
	spin_unlock_irqrestore(&loongson2_wait_lock, flags);
}
EXPORT_SYMBOL_GPL(loongson2_cpu_wait);

MODULE_AUTHOR("Yanhua <yanh@lemote.com>");
MODULE_DESCRIPTION("cpufreq driver for Loongson 2F");
MODULE_LICENSE("GPL");
