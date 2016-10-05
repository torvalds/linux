/*
 * Copyright (c) 2015 Endless Mobile, Inc.
 * Author: Carlo Caione <carlo@endlessm.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * CPU clock path:
 *
 *                           +-[/N]-----|3|
 *             MUX2  +--[/3]-+----------|2| MUX1
 * [sys_pll]---|1|   |--[/2]------------|1|-|1|
 *             | |---+------------------|0| | |----- [a5_clk]
 *          +--|0|                          | |
 * [xtal]---+-------------------------------|0|
 *
 *
 *
 */

#include <linux/delay.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>

#define MESON_CPU_CLK_CNTL1		0x00
#define MESON_CPU_CLK_CNTL		0x40

#define MESON_CPU_CLK_MUX1		BIT(7)
#define MESON_CPU_CLK_MUX2		BIT(0)

#define MESON_N_WIDTH			9
#define MESON_N_SHIFT			20
#define MESON_SEL_WIDTH			2
#define MESON_SEL_SHIFT			2

#include "clkc.h"

#define to_meson_clk_cpu_hw(_hw) container_of(_hw, struct meson_clk_cpu, hw)
#define to_meson_clk_cpu_nb(_nb) container_of(_nb, struct meson_clk_cpu, clk_nb)

static long meson_clk_cpu_round_rate(struct clk_hw *hw, unsigned long rate,
				     unsigned long *prate)
{
	struct meson_clk_cpu *clk_cpu = to_meson_clk_cpu_hw(hw);

	return divider_round_rate(hw, rate, prate, clk_cpu->div_table,
				  MESON_N_WIDTH, CLK_DIVIDER_ROUND_CLOSEST);
}

static int meson_clk_cpu_set_rate(struct clk_hw *hw, unsigned long rate,
				  unsigned long parent_rate)
{
	struct meson_clk_cpu *clk_cpu = to_meson_clk_cpu_hw(hw);
	unsigned int div, sel, N = 0;
	u32 reg;

	div = DIV_ROUND_UP(parent_rate, rate);

	if (div <= 3) {
		sel = div - 1;
	} else {
		sel = 3;
		N = div / 2;
	}

	reg = readl(clk_cpu->base + clk_cpu->reg_off + MESON_CPU_CLK_CNTL1);
	reg = PARM_SET(MESON_N_WIDTH, MESON_N_SHIFT, reg, N);
	writel(reg, clk_cpu->base + clk_cpu->reg_off + MESON_CPU_CLK_CNTL1);

	reg = readl(clk_cpu->base + clk_cpu->reg_off + MESON_CPU_CLK_CNTL);
	reg = PARM_SET(MESON_SEL_WIDTH, MESON_SEL_SHIFT, reg, sel);
	writel(reg, clk_cpu->base + clk_cpu->reg_off + MESON_CPU_CLK_CNTL);

	return 0;
}

static unsigned long meson_clk_cpu_recalc_rate(struct clk_hw *hw,
					       unsigned long parent_rate)
{
	struct meson_clk_cpu *clk_cpu = to_meson_clk_cpu_hw(hw);
	unsigned int N, sel;
	unsigned int div = 1;
	u32 reg;

	reg = readl(clk_cpu->base + clk_cpu->reg_off + MESON_CPU_CLK_CNTL1);
	N = PARM_GET(MESON_N_WIDTH, MESON_N_SHIFT, reg);

	reg = readl(clk_cpu->base + clk_cpu->reg_off + MESON_CPU_CLK_CNTL);
	sel = PARM_GET(MESON_SEL_WIDTH, MESON_SEL_SHIFT, reg);

	if (sel < 3)
		div = sel + 1;
	else
		div = 2 * N;

	return parent_rate / div;
}

/* FIXME MUX1 & MUX2 should be struct clk_hw objects */
static int meson_clk_cpu_pre_rate_change(struct meson_clk_cpu *clk_cpu,
					 struct clk_notifier_data *ndata)
{
	u32 cpu_clk_cntl;

	/* switch MUX1 to xtal */
	cpu_clk_cntl = readl(clk_cpu->base + clk_cpu->reg_off
				+ MESON_CPU_CLK_CNTL);
	cpu_clk_cntl &= ~MESON_CPU_CLK_MUX1;
	writel(cpu_clk_cntl, clk_cpu->base + clk_cpu->reg_off
				+ MESON_CPU_CLK_CNTL);
	udelay(100);

	/* switch MUX2 to sys-pll */
	cpu_clk_cntl |= MESON_CPU_CLK_MUX2;
	writel(cpu_clk_cntl, clk_cpu->base + clk_cpu->reg_off
				+ MESON_CPU_CLK_CNTL);

	return 0;
}

/* FIXME MUX1 & MUX2 should be struct clk_hw objects */
static int meson_clk_cpu_post_rate_change(struct meson_clk_cpu *clk_cpu,
					  struct clk_notifier_data *ndata)
{
	u32 cpu_clk_cntl;

	/* switch MUX1 to divisors' output */
	cpu_clk_cntl = readl(clk_cpu->base + clk_cpu->reg_off
				+ MESON_CPU_CLK_CNTL);
	cpu_clk_cntl |= MESON_CPU_CLK_MUX1;
	writel(cpu_clk_cntl, clk_cpu->base + clk_cpu->reg_off
				+ MESON_CPU_CLK_CNTL);
	udelay(100);

	return 0;
}

/*
 * This clock notifier is called when the frequency of the of the parent
 * PLL clock is to be changed. We use the xtal input as temporary parent
 * while the PLL frequency is stabilized.
 */
int meson_clk_cpu_notifier_cb(struct notifier_block *nb,
				     unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct meson_clk_cpu *clk_cpu = to_meson_clk_cpu_nb(nb);
	int ret = 0;

	if (event == PRE_RATE_CHANGE)
		ret = meson_clk_cpu_pre_rate_change(clk_cpu, ndata);
	else if (event == POST_RATE_CHANGE)
		ret = meson_clk_cpu_post_rate_change(clk_cpu, ndata);

	return notifier_from_errno(ret);
}

const struct clk_ops meson_clk_cpu_ops = {
	.recalc_rate	= meson_clk_cpu_recalc_rate,
	.round_rate	= meson_clk_cpu_round_rate,
	.set_rate	= meson_clk_cpu_set_rate,
};
