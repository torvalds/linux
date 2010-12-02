/*
 * Helper routines for SuperH Clock Pulse Generator blocks (CPG).
 *
 *  Copyright (C) 2010  Magnus Damm
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/sh_clk.h>

static int sh_clk_mstp32_enable(struct clk *clk)
{
	__raw_writel(__raw_readl(clk->enable_reg) & ~(1 << clk->enable_bit),
		     clk->enable_reg);
	return 0;
}

static void sh_clk_mstp32_disable(struct clk *clk)
{
	__raw_writel(__raw_readl(clk->enable_reg) | (1 << clk->enable_bit),
		     clk->enable_reg);
}

static struct clk_ops sh_clk_mstp32_clk_ops = {
	.enable		= sh_clk_mstp32_enable,
	.disable	= sh_clk_mstp32_disable,
	.recalc		= followparent_recalc,
};

int __init sh_clk_mstp32_register(struct clk *clks, int nr)
{
	struct clk *clkp;
	int ret = 0;
	int k;

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;
		clkp->ops = &sh_clk_mstp32_clk_ops;
		ret |= clk_register(clkp);
	}

	return ret;
}

static long sh_clk_div_round_rate(struct clk *clk, unsigned long rate)
{
	return clk_rate_table_round(clk, clk->freq_table, rate);
}

static int sh_clk_div6_divisors[64] = {
	1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
	17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32,
	33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48,
	49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 60, 61, 62, 63, 64
};

static struct clk_div_mult_table sh_clk_div6_table = {
	.divisors = sh_clk_div6_divisors,
	.nr_divisors = ARRAY_SIZE(sh_clk_div6_divisors),
};

static unsigned long sh_clk_div6_recalc(struct clk *clk)
{
	struct clk_div_mult_table *table = &sh_clk_div6_table;
	unsigned int idx;

	clk_rate_table_build(clk, clk->freq_table, table->nr_divisors,
			     table, NULL);

	idx = __raw_readl(clk->enable_reg) & 0x003f;

	return clk->freq_table[idx].frequency;
}

static int sh_clk_div6_set_parent(struct clk *clk, struct clk *parent)
{
	struct clk_div_mult_table *table = &sh_clk_div6_table;
	u32 value;
	int ret, i;

	if (!clk->parent_table || !clk->parent_num)
		return -EINVAL;

	/* Search the parent */
	for (i = 0; i < clk->parent_num; i++)
		if (clk->parent_table[i] == parent)
			break;

	if (i == clk->parent_num)
		return -ENODEV;

	ret = clk_reparent(clk, parent);
	if (ret < 0)
		return ret;

	value = __raw_readl(clk->enable_reg) &
		~(((1 << clk->src_width) - 1) << clk->src_shift);

	__raw_writel(value | (i << clk->src_shift), clk->enable_reg);

	/* Rebuild the frequency table */
	clk_rate_table_build(clk, clk->freq_table, table->nr_divisors,
			     table, &clk->arch_flags);

	return 0;
}

static int sh_clk_div6_set_rate(struct clk *clk, unsigned long rate)
{
	unsigned long value;
	int idx;

	idx = clk_rate_table_find(clk, clk->freq_table, rate);
	if (idx < 0)
		return idx;

	value = __raw_readl(clk->enable_reg);
	value &= ~0x3f;
	value |= idx;
	__raw_writel(value, clk->enable_reg);
	return 0;
}

static int sh_clk_div6_enable(struct clk *clk)
{
	unsigned long value;
	int ret;

	ret = sh_clk_div6_set_rate(clk, clk->rate);
	if (ret == 0) {
		value = __raw_readl(clk->enable_reg);
		value &= ~0x100; /* clear stop bit to enable clock */
		__raw_writel(value, clk->enable_reg);
	}
	return ret;
}

static void sh_clk_div6_disable(struct clk *clk)
{
	unsigned long value;

	value = __raw_readl(clk->enable_reg);
	value |= 0x100; /* stop clock */
	value |= 0x3f; /* VDIV bits must be non-zero, overwrite divider */
	__raw_writel(value, clk->enable_reg);
}

static struct clk_ops sh_clk_div6_clk_ops = {
	.recalc		= sh_clk_div6_recalc,
	.round_rate	= sh_clk_div_round_rate,
	.set_rate	= sh_clk_div6_set_rate,
	.enable		= sh_clk_div6_enable,
	.disable	= sh_clk_div6_disable,
};

static struct clk_ops sh_clk_div6_reparent_clk_ops = {
	.recalc		= sh_clk_div6_recalc,
	.round_rate	= sh_clk_div_round_rate,
	.set_rate	= sh_clk_div6_set_rate,
	.enable		= sh_clk_div6_enable,
	.disable	= sh_clk_div6_disable,
	.set_parent	= sh_clk_div6_set_parent,
};

static int __init sh_clk_div6_register_ops(struct clk *clks, int nr,
					   struct clk_ops *ops)
{
	struct clk *clkp;
	void *freq_table;
	int nr_divs = sh_clk_div6_table.nr_divisors;
	int freq_table_size = sizeof(struct cpufreq_frequency_table);
	int ret = 0;
	int k;

	freq_table_size *= (nr_divs + 1);
	freq_table = kzalloc(freq_table_size * nr, GFP_KERNEL);
	if (!freq_table) {
		pr_err("sh_clk_div6_register: unable to alloc memory\n");
		return -ENOMEM;
	}

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;

		clkp->ops = ops;
		clkp->freq_table = freq_table + (k * freq_table_size);
		clkp->freq_table[nr_divs].frequency = CPUFREQ_TABLE_END;

		ret = clk_register(clkp);
	}

	return ret;
}

int __init sh_clk_div6_register(struct clk *clks, int nr)
{
	return sh_clk_div6_register_ops(clks, nr, &sh_clk_div6_clk_ops);
}

int __init sh_clk_div6_reparent_register(struct clk *clks, int nr)
{
	return sh_clk_div6_register_ops(clks, nr,
					&sh_clk_div6_reparent_clk_ops);
}

static unsigned long sh_clk_div4_recalc(struct clk *clk)
{
	struct clk_div4_table *d4t = clk->priv;
	struct clk_div_mult_table *table = d4t->div_mult_table;
	unsigned int idx;

	clk_rate_table_build(clk, clk->freq_table, table->nr_divisors,
			     table, &clk->arch_flags);

	idx = (__raw_readl(clk->enable_reg) >> clk->enable_bit) & 0x000f;

	return clk->freq_table[idx].frequency;
}

static int sh_clk_div4_set_parent(struct clk *clk, struct clk *parent)
{
	struct clk_div4_table *d4t = clk->priv;
	struct clk_div_mult_table *table = d4t->div_mult_table;
	u32 value;
	int ret;

	/* we really need a better way to determine parent index, but for
	 * now assume internal parent comes with CLK_ENABLE_ON_INIT set,
	 * no CLK_ENABLE_ON_INIT means external clock...
	 */

	if (parent->flags & CLK_ENABLE_ON_INIT)
		value = __raw_readl(clk->enable_reg) & ~(1 << 7);
	else
		value = __raw_readl(clk->enable_reg) | (1 << 7);

	ret = clk_reparent(clk, parent);
	if (ret < 0)
		return ret;

	__raw_writel(value, clk->enable_reg);

	/* Rebiuld the frequency table */
	clk_rate_table_build(clk, clk->freq_table, table->nr_divisors,
			     table, &clk->arch_flags);

	return 0;
}

static int sh_clk_div4_set_rate(struct clk *clk, unsigned long rate)
{
	struct clk_div4_table *d4t = clk->priv;
	unsigned long value;
	int idx = clk_rate_table_find(clk, clk->freq_table, rate);
	if (idx < 0)
		return idx;

	value = __raw_readl(clk->enable_reg);
	value &= ~(0xf << clk->enable_bit);
	value |= (idx << clk->enable_bit);
	__raw_writel(value, clk->enable_reg);

	if (d4t->kick)
		d4t->kick(clk);

	return 0;
}

static int sh_clk_div4_enable(struct clk *clk)
{
	__raw_writel(__raw_readl(clk->enable_reg) & ~(1 << 8), clk->enable_reg);
	return 0;
}

static void sh_clk_div4_disable(struct clk *clk)
{
	__raw_writel(__raw_readl(clk->enable_reg) | (1 << 8), clk->enable_reg);
}

static struct clk_ops sh_clk_div4_clk_ops = {
	.recalc		= sh_clk_div4_recalc,
	.set_rate	= sh_clk_div4_set_rate,
	.round_rate	= sh_clk_div_round_rate,
};

static struct clk_ops sh_clk_div4_enable_clk_ops = {
	.recalc		= sh_clk_div4_recalc,
	.set_rate	= sh_clk_div4_set_rate,
	.round_rate	= sh_clk_div_round_rate,
	.enable		= sh_clk_div4_enable,
	.disable	= sh_clk_div4_disable,
};

static struct clk_ops sh_clk_div4_reparent_clk_ops = {
	.recalc		= sh_clk_div4_recalc,
	.set_rate	= sh_clk_div4_set_rate,
	.round_rate	= sh_clk_div_round_rate,
	.enable		= sh_clk_div4_enable,
	.disable	= sh_clk_div4_disable,
	.set_parent	= sh_clk_div4_set_parent,
};

static int __init sh_clk_div4_register_ops(struct clk *clks, int nr,
			struct clk_div4_table *table, struct clk_ops *ops)
{
	struct clk *clkp;
	void *freq_table;
	int nr_divs = table->div_mult_table->nr_divisors;
	int freq_table_size = sizeof(struct cpufreq_frequency_table);
	int ret = 0;
	int k;

	freq_table_size *= (nr_divs + 1);
	freq_table = kzalloc(freq_table_size * nr, GFP_KERNEL);
	if (!freq_table) {
		pr_err("sh_clk_div4_register: unable to alloc memory\n");
		return -ENOMEM;
	}

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;

		clkp->ops = ops;
		clkp->priv = table;

		clkp->freq_table = freq_table + (k * freq_table_size);
		clkp->freq_table[nr_divs].frequency = CPUFREQ_TABLE_END;

		ret = clk_register(clkp);
	}

	return ret;
}

int __init sh_clk_div4_register(struct clk *clks, int nr,
				struct clk_div4_table *table)
{
	return sh_clk_div4_register_ops(clks, nr, table, &sh_clk_div4_clk_ops);
}

int __init sh_clk_div4_enable_register(struct clk *clks, int nr,
				struct clk_div4_table *table)
{
	return sh_clk_div4_register_ops(clks, nr, table,
					&sh_clk_div4_enable_clk_ops);
}

int __init sh_clk_div4_reparent_register(struct clk *clks, int nr,
				struct clk_div4_table *table)
{
	return sh_clk_div4_register_ops(clks, nr, table,
					&sh_clk_div4_reparent_clk_ops);
}
