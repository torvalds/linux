/*
 * arch/sh/kernel/cpu/sh4a/clock-sh7785.c
 *
 * SH7785 support for the clock framework
 *
 *  Copyright (C) 2007 - 2009  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/cpufreq.h>
#include <asm/clock.h>
#include <asm/freq.h>

static unsigned int div2[] = { 1, 2, 4, 6, 8, 12, 16, 18,
			       24, 32, 36, 48 };
struct clk_priv {
	unsigned int			shift;

	/* allowable divisor bitmap */
	unsigned long			div_bitmap;

	/* Supportable frequencies + termination entry */
	struct cpufreq_frequency_table	freq_table[ARRAY_SIZE(div2)+1];
};

#define FRQMR_CLK_DATA(_name, _shift, _div_bitmap)	\
static struct clk_priv _name##_data = {			\
	.shift		= _shift,			\
	.div_bitmap	= _div_bitmap,			\
							\
	.freq_table[0]	= {				\
		.index = 0,				\
		.frequency = CPUFREQ_TABLE_END,		\
	},						\
}

FRQMR_CLK_DATA(pfc,  0, 0x0f80);
FRQMR_CLK_DATA(s3fc, 4, 0x0ff0);
FRQMR_CLK_DATA(s2fc, 8, 0x0030);
FRQMR_CLK_DATA(mfc, 12, 0x000c);
FRQMR_CLK_DATA(bfc, 16, 0x0fe0);
FRQMR_CLK_DATA(sfc, 20, 0x000c);
FRQMR_CLK_DATA(ufc, 24, 0x000c);
FRQMR_CLK_DATA(ifc, 28, 0x000e);

static unsigned long frqmr_recalc(struct clk *clk)
{
	struct clk_priv *data = clk->priv;
	unsigned int idx;

	idx = (__raw_readl(FRQMR1) >> data->shift) & 0x000f;

	/*
	 * XXX: PLL1 multiplier is locked for the default clock mode,
	 * when mode pin detection and configuration support is added,
	 * select the multiplier dynamically.
	 */
	return clk->parent->rate * 36 / div2[idx];
}

static void frqmr_build_rate_table(struct clk *clk)
{
	struct clk_priv *data = clk->priv;
	int i, entry;

	for (i = entry = 0; i < ARRAY_SIZE(div2); i++) {
		if ((data->div_bitmap & (1 << i)) == 0)
			continue;

		data->freq_table[entry].index = entry;
		data->freq_table[entry].frequency =
			clk->parent->rate * 36 / div2[i];

		entry++;
	}

	if (entry == 0) {
		pr_warning("clkfwk: failed to build frequency table "
			   "for \"%s\" clk!\n", clk->name);
		return;
	}

	/* Termination entry */
	data->freq_table[entry].index = entry;
	data->freq_table[entry].frequency = CPUFREQ_TABLE_END;
}

static long frqmr_round_rate(struct clk *clk, unsigned long rate)
{
	struct clk_priv *data = clk->priv;
	unsigned long rate_error, rate_error_prev = ~0UL;
	unsigned long rate_best_fit = rate;
	unsigned long highest, lowest;
	int i;

	highest = lowest = 0;

	for (i = 0; data->freq_table[i].frequency != CPUFREQ_TABLE_END; i++) {
		unsigned long freq = data->freq_table[i].frequency;

		if (freq == CPUFREQ_ENTRY_INVALID)
			continue;

		if (freq > highest)
			highest = freq;
		if (freq < lowest)
			lowest = freq;

		rate_error = abs(freq - rate);
		if (rate_error < rate_error_prev) {
			rate_best_fit = freq;
			rate_error_prev = rate_error;
		}

		if (rate_error == 0)
			break;
	}

	if (rate >= highest)
		rate_best_fit = highest;
	if (rate <= lowest)
		rate_best_fit = lowest;

	return rate_best_fit;
}

static struct clk_ops frqmr_clk_ops = {
	.recalc			= frqmr_recalc,
	.build_rate_table	= frqmr_build_rate_table,
	.round_rate		= frqmr_round_rate,
};

/*
 * Default rate for the root input clock, reset this with clk_set_rate()
 * from the platform code.
 */
static struct clk extal_clk = {
	.name		= "extal",
	.id		= -1,
	.rate		= 33333333,
};

static struct clk cpu_clk = {
	.name		= "cpu_clk",		/* Ick */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
	.priv		= &ifc_data,
};

static struct clk shyway_clk = {
	.name		= "shyway_clk",		/* SHck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
	.priv		= &sfc_data,
};

static struct clk peripheral_clk = {
	.name		= "peripheral_clk",	/* Pck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
	.priv		= &pfc_data,
};

static struct clk ddr_clk = {
	.name		= "ddr_clk",		/* DDRck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
	.priv		= &mfc_data,
};

static struct clk bus_clk = {
	.name		= "bus_clk",		/* Bck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
	.priv		= &bfc_data,
};

static struct clk ga_clk = {
	.name		= "ga_clk",		/* GAck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.priv		= &s2fc_data,
};

static struct clk du_clk = {
	.name		= "du_clk",		/* DUck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.priv		= &s3fc_data,
};

static struct clk umem_clk = {
	.name		= "umem_clk",		/* uck */
	.id		= -1,
	.ops		= &frqmr_clk_ops,
	.parent		= &extal_clk,
	.flags		= CLK_ENABLE_ON_INIT,
	.priv		= &ufc_data,
};

static struct clk *clks[] = {
	&extal_clk,
	&cpu_clk,
	&shyway_clk,
	&peripheral_clk,
	&ddr_clk,
	&bus_clk,
	&ga_clk,
	&du_clk,
	&umem_clk,
};

int __init arch_clk_init(void)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(clks); i++)
		ret |= clk_register(clks[i]);

	return ret;
}
