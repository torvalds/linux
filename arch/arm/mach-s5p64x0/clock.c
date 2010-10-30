/* linux/arch/arm/mach-s5p64x0/clock.c
 *
 * Copyright (c) 2010 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * S5P64X0 - Clock support
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/init.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/errno.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/sysdev.h>
#include <linux/io.h>

#include <mach/hardware.h>
#include <mach/map.h>
#include <mach/regs-clock.h>

#include <plat/cpu-freq.h>
#include <plat/clock.h>
#include <plat/cpu.h>
#include <plat/pll.h>
#include <plat/s5p-clock.h>
#include <plat/clock-clksrc.h>
#include <plat/s5p6440.h>
#include <plat/s5p6450.h>

struct clksrc_clk clk_mout_apll = {
	.clk	= {
		.name		= "mout_apll",
		.id		= -1,
	},
	.sources	= &clk_src_apll,
	.reg_src	= { .reg = S5P64X0_CLK_SRC0, .shift = 0, .size = 1 },
};

struct clksrc_clk clk_mout_mpll = {
	.clk	= {
		.name		= "mout_mpll",
		.id		= -1,
	},
	.sources	= &clk_src_mpll,
	.reg_src	= { .reg = S5P64X0_CLK_SRC0, .shift = 1, .size = 1 },
};

struct clksrc_clk clk_mout_epll = {
	.clk	= {
		.name		= "mout_epll",
		.id		= -1,
	},
	.sources	= &clk_src_epll,
	.reg_src	= { .reg = S5P64X0_CLK_SRC0, .shift = 2, .size = 1 },
};

enum perf_level {
	L0 = 532*1000,
	L1 = 266*1000,
	L2 = 133*1000,
};

static const u32 clock_table[][3] = {
	/*{ARM_CLK, DIVarm, DIVhclk}*/
	{L0 * 1000, (0 << ARM_DIV_RATIO_SHIFT), (3 << S5P64X0_CLKDIV0_HCLK_SHIFT)},
	{L1 * 1000, (1 << ARM_DIV_RATIO_SHIFT), (1 << S5P64X0_CLKDIV0_HCLK_SHIFT)},
	{L2 * 1000, (3 << ARM_DIV_RATIO_SHIFT), (0 << S5P64X0_CLKDIV0_HCLK_SHIFT)},
};

int s5p64x0_epll_enable(struct clk *clk, int enable)
{
	unsigned int ctrlbit = clk->ctrlbit;
	unsigned int epll_con = __raw_readl(S5P64X0_EPLL_CON) & ~ctrlbit;

	if (enable)
		__raw_writel(epll_con | ctrlbit, S5P64X0_EPLL_CON);
	else
		__raw_writel(epll_con, S5P64X0_EPLL_CON);

	return 0;
}

unsigned long s5p64x0_epll_get_rate(struct clk *clk)
{
	return clk->rate;
}

unsigned long s5p64x0_armclk_get_rate(struct clk *clk)
{
	unsigned long rate = clk_get_rate(clk->parent);
	u32 clkdiv;

	/* divisor mask starts at bit0, so no need to shift */
	clkdiv = __raw_readl(ARM_CLK_DIV) & ARM_DIV_MASK;

	return rate / (clkdiv + 1);
}

unsigned long s5p64x0_armclk_round_rate(struct clk *clk, unsigned long rate)
{
	u32 iter;

	for (iter = 1 ; iter < ARRAY_SIZE(clock_table) ; iter++) {
		if (rate > clock_table[iter][0])
			return clock_table[iter-1][0];
	}

	return clock_table[ARRAY_SIZE(clock_table) - 1][0];
}

int s5p64x0_armclk_set_rate(struct clk *clk, unsigned long rate)
{
	u32 round_tmp;
	u32 iter;
	u32 clk_div0_tmp;
	u32 cur_rate = clk->ops->get_rate(clk);
	unsigned long flags;

	round_tmp = clk->ops->round_rate(clk, rate);
	if (round_tmp == cur_rate)
		return 0;


	for (iter = 0 ; iter < ARRAY_SIZE(clock_table) ; iter++) {
		if (round_tmp == clock_table[iter][0])
			break;
	}

	if (iter >= ARRAY_SIZE(clock_table))
		iter = ARRAY_SIZE(clock_table) - 1;

	local_irq_save(flags);
	if (cur_rate > round_tmp) {
		/* Frequency Down */
		clk_div0_tmp = __raw_readl(ARM_CLK_DIV) & ~(ARM_DIV_MASK);
		clk_div0_tmp |= clock_table[iter][1];
		__raw_writel(clk_div0_tmp, ARM_CLK_DIV);

		clk_div0_tmp = __raw_readl(ARM_CLK_DIV) &
				~(S5P64X0_CLKDIV0_HCLK_MASK);
		clk_div0_tmp |= clock_table[iter][2];
		__raw_writel(clk_div0_tmp, ARM_CLK_DIV);


	} else {
		/* Frequency Up */
		clk_div0_tmp = __raw_readl(ARM_CLK_DIV) &
				~(S5P64X0_CLKDIV0_HCLK_MASK);
		clk_div0_tmp |= clock_table[iter][2];
		__raw_writel(clk_div0_tmp, ARM_CLK_DIV);

		clk_div0_tmp = __raw_readl(ARM_CLK_DIV) & ~(ARM_DIV_MASK);
		clk_div0_tmp |= clock_table[iter][1];
		__raw_writel(clk_div0_tmp, ARM_CLK_DIV);
	}
	local_irq_restore(flags);

	clk->rate = clock_table[iter][0];

	return 0;
}

struct clk_ops s5p64x0_clkarm_ops = {
	.get_rate	= s5p64x0_armclk_get_rate,
	.set_rate	= s5p64x0_armclk_set_rate,
	.round_rate	= s5p64x0_armclk_round_rate,
};

struct clksrc_clk clk_armclk = {
	.clk	= {
		.name		= "armclk",
		.id		= 1,
		.parent		= &clk_mout_apll.clk,
		.ops		= &s5p64x0_clkarm_ops,
	},
	.reg_div	= { .reg = S5P64X0_CLK_DIV0, .shift = 0, .size = 4 },
};

struct clksrc_clk clk_dout_mpll = {
	.clk	= {
		.name		= "dout_mpll",
		.id		= -1,
		.parent		= &clk_mout_mpll.clk,
	},
	.reg_div	= { .reg = S5P64X0_CLK_DIV0, .shift = 4, .size = 1 },
};

struct clk *clkset_hclk_low_list[] = {
	&clk_mout_apll.clk,
	&clk_mout_mpll.clk,
};

struct clksrc_sources clkset_hclk_low = {
	.sources	= clkset_hclk_low_list,
	.nr_sources	= ARRAY_SIZE(clkset_hclk_low_list),
};

int s5p64x0_pclk_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P64X0_CLK_GATE_PCLK, clk, enable);
}

int s5p64x0_hclk0_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P64X0_CLK_GATE_HCLK0, clk, enable);
}

int s5p64x0_hclk1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P64X0_CLK_GATE_HCLK1, clk, enable);
}

int s5p64x0_sclk_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P64X0_CLK_GATE_SCLK0, clk, enable);
}

int s5p64x0_sclk1_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P64X0_CLK_GATE_SCLK1, clk, enable);
}

int s5p64x0_mem_ctrl(struct clk *clk, int enable)
{
	return s5p_gatectrl(S5P64X0_CLK_GATE_MEM0, clk, enable);
}

int s5p64x0_clk48m_ctrl(struct clk *clk, int enable)
{
	unsigned long flags;
	u32 val;

	/* can't rely on clock lock, this register has other usages */
	local_irq_save(flags);

	val = __raw_readl(S5P64X0_OTHERS);
	if (enable)
		val |= S5P64X0_OTHERS_USB_SIG_MASK;
	else
		val &= ~S5P64X0_OTHERS_USB_SIG_MASK;

	__raw_writel(val, S5P64X0_OTHERS);

	local_irq_restore(flags);

	return 0;
}
