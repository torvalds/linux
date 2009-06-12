#include <linux/clk.h>
#include <linux/compiler.h>
#include <linux/bootmem.h>
#include <linux/io.h>
#include <asm/clock.h>

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

static int sh_clk_div6_set_rate(struct clk *clk,
				unsigned long rate, int algo_id)
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

	ret = sh_clk_div6_set_rate(clk, clk->rate, 0);
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

int __init sh_clk_div6_register(struct clk *clks, int nr)
{
	struct clk *clkp;
	void *freq_table;
	int nr_divs = sh_clk_div6_table.nr_divisors;
	int freq_table_size = sizeof(struct cpufreq_frequency_table);
	int ret = 0;
	int k;

	freq_table_size *= (nr_divs + 1);

	freq_table = alloc_bootmem(freq_table_size * nr);
	if (!freq_table)
		return -ENOMEM;

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;

		clkp->ops = &sh_clk_div6_clk_ops;
		clkp->id = -1;
		clkp->freq_table = freq_table + (k * freq_table_size);
		clkp->freq_table[nr_divs].frequency = CPUFREQ_TABLE_END;

		ret = clk_register(clkp);
	}

	return ret;
}

static unsigned long sh_clk_div4_recalc(struct clk *clk)
{
	struct clk_div_mult_table *table = clk->priv;
	unsigned int idx;

	clk_rate_table_build(clk, clk->freq_table, table->nr_divisors,
			     table, &clk->arch_flags);

	idx = (__raw_readl(clk->enable_reg) >> clk->enable_bit) & 0x000f;

	return clk->freq_table[idx].frequency;
}

static struct clk_ops sh_clk_div4_clk_ops = {
	.recalc		= sh_clk_div4_recalc,
	.round_rate	= sh_clk_div_round_rate,
};

int __init sh_clk_div4_register(struct clk *clks, int nr,
				struct clk_div_mult_table *table)
{
	struct clk *clkp;
	void *freq_table;
	int nr_divs = table->nr_divisors;
	int freq_table_size = sizeof(struct cpufreq_frequency_table);
	int ret = 0;
	int k;

	freq_table_size *= (nr_divs + 1);

	freq_table = alloc_bootmem(freq_table_size * nr);
	if (!freq_table)
		return -ENOMEM;

	for (k = 0; !ret && (k < nr); k++) {
		clkp = clks + k;

		clkp->ops = &sh_clk_div4_clk_ops;
		clkp->id = -1;
		clkp->priv = table;

		clkp->freq_table = freq_table + (k * freq_table_size);
		clkp->freq_table[nr_divs].frequency = CPUFREQ_TABLE_END;

		ret = clk_register(clkp);
	}

	return ret;
}

#ifdef CONFIG_SH_CLK_CPG_LEGACY
static struct clk master_clk = {
	.name		= "master_clk",
	.flags		= CLK_ENABLE_ON_INIT,
	.rate		= CONFIG_SH_PCLK_FREQ,
};

static struct clk peripheral_clk = {
	.name		= "peripheral_clk",
	.parent		= &master_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk bus_clk = {
	.name		= "bus_clk",
	.parent		= &master_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

static struct clk cpu_clk = {
	.name		= "cpu_clk",
	.parent		= &master_clk,
	.flags		= CLK_ENABLE_ON_INIT,
};

/*
 * The ordering of these clocks matters, do not change it.
 */
static struct clk *onchip_clocks[] = {
	&master_clk,
	&peripheral_clk,
	&bus_clk,
	&cpu_clk,
};

int __init __deprecated cpg_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(onchip_clocks); i++) {
		struct clk *clk = onchip_clocks[i];
		arch_init_clk_ops(&clk->ops, i);
		if (clk->ops)
			ret |= clk_register(clk);
	}

	return ret;
}

/*
 * Placeholder for compatability, until the lazy CPUs do this
 * on their own.
 */
int __init __weak arch_clk_init(void)
{
	return cpg_clk_init();
}
#endif /* CONFIG_SH_CPG_CLK_LEGACY */
