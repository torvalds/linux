/*
 * Copyright 2014 Google, Inc
 * Author: Alexandru M Stan <amstan@chromium.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include "clk.h"

struct rockchip_mmc_clock {
	struct clk_hw	hw;
	void __iomem	*reg;
	int		id;
	int		shift;
};

#define to_mmc_clock(_hw) container_of(_hw, struct rockchip_mmc_clock, hw)

#define RK3288_MMC_CLKGEN_DIV 2

static unsigned long rockchip_mmc_recalc(struct clk_hw *hw,
					 unsigned long parent_rate)
{
	return parent_rate / RK3288_MMC_CLKGEN_DIV;
}

#define ROCKCHIP_MMC_DELAY_SEL BIT(10)
#define ROCKCHIP_MMC_DEGREE_MASK 0x3
#define ROCKCHIP_MMC_DELAYNUM_OFFSET 2
#define ROCKCHIP_MMC_DELAYNUM_MASK (0xff << ROCKCHIP_MMC_DELAYNUM_OFFSET)
#define ROCKCHIP_MMC_INIT_STATE_RESET 0x1
#define ROCKCHIP_MMC_INIT_STATE_SHIFT 1

#define PSECS_PER_SEC 1000000000000LL

/*
 * Each fine delay is between 44ps-77ps. Assume each fine delay is 60ps to
 * simplify calculations. So 45degs could be anywhere between 33deg and 57.8deg.
 */
#define ROCKCHIP_MMC_DELAY_ELEMENT_PSEC 60

static int rockchip_mmc_get_phase(struct clk_hw *hw)
{
	struct rockchip_mmc_clock *mmc_clock = to_mmc_clock(hw);
	unsigned long rate = clk_get_rate(hw->clk);
	u32 raw_value;
	u16 degrees;
	u32 delay_num = 0;

	raw_value = readl(mmc_clock->reg) >> (mmc_clock->shift);

	degrees = (raw_value & ROCKCHIP_MMC_DEGREE_MASK) * 90;

	if (raw_value & ROCKCHIP_MMC_DELAY_SEL) {
		/* degrees/delaynum * 10000 */
		unsigned long factor = (ROCKCHIP_MMC_DELAY_ELEMENT_PSEC / 10) *
					36 * (rate / 1000000);

		delay_num = (raw_value & ROCKCHIP_MMC_DELAYNUM_MASK);
		delay_num >>= ROCKCHIP_MMC_DELAYNUM_OFFSET;
		degrees += DIV_ROUND_CLOSEST(delay_num * factor, 10000);
	}

	return degrees % 360;
}

static int rockchip_mmc_set_phase(struct clk_hw *hw, int degrees)
{
	struct rockchip_mmc_clock *mmc_clock = to_mmc_clock(hw);
	unsigned long rate = clk_get_rate(hw->clk);
	u8 nineties, remainder;
	u8 delay_num;
	u32 raw_value;
	u32 delay;

	nineties = degrees / 90;
	remainder = (degrees % 90);

	/*
	 * Due to the inexact nature of the "fine" delay, we might
	 * actually go non-monotonic.  We don't go _too_ monotonic
	 * though, so we should be OK.  Here are options of how we may
	 * work:
	 *
	 * Ideally we end up with:
	 *   1.0, 2.0, ..., 69.0, 70.0, ...,  89.0, 90.0
	 *
	 * On one extreme (if delay is actually 44ps):
	 *   .73, 1.5, ..., 50.6, 51.3, ...,  65.3, 90.0
	 * The other (if delay is actually 77ps):
	 *   1.3, 2.6, ..., 88.6. 89.8, ..., 114.0, 90
	 *
	 * It's possible we might make a delay that is up to 25
	 * degrees off from what we think we're making.  That's OK
	 * though because we should be REALLY far from any bad range.
	 */

	/*
	 * Convert to delay; do a little extra work to make sure we
	 * don't overflow 32-bit / 64-bit numbers.
	 */
	delay = 10000000; /* PSECS_PER_SEC / 10000 / 10 */
	delay *= remainder;
	delay = DIV_ROUND_CLOSEST(delay,
			(rate / 1000) * 36 *
				(ROCKCHIP_MMC_DELAY_ELEMENT_PSEC / 10));

	delay_num = (u8) min_t(u32, delay, 255);

	raw_value = delay_num ? ROCKCHIP_MMC_DELAY_SEL : 0;
	raw_value |= delay_num << ROCKCHIP_MMC_DELAYNUM_OFFSET;
	raw_value |= nineties;
	writel(HIWORD_UPDATE(raw_value, 0x07ff, mmc_clock->shift), mmc_clock->reg);

	pr_debug("%s->set_phase(%d) delay_nums=%u reg[0x%p]=0x%03x actual_degrees=%d\n",
		clk_hw_get_name(hw), degrees, delay_num,
		mmc_clock->reg, raw_value>>(mmc_clock->shift),
		rockchip_mmc_get_phase(hw)
	);

	return 0;
}

static const struct clk_ops rockchip_mmc_clk_ops = {
	.recalc_rate	= rockchip_mmc_recalc,
	.get_phase	= rockchip_mmc_get_phase,
	.set_phase	= rockchip_mmc_set_phase,
};

struct clk *rockchip_clk_register_mmc(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg, int shift)
{
	struct clk_init_data init;
	struct rockchip_mmc_clock *mmc_clock;
	struct clk *clk;

	mmc_clock = kmalloc(sizeof(*mmc_clock), GFP_KERNEL);
	if (!mmc_clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.num_parents = num_parents;
	init.parent_names = parent_names;
	init.ops = &rockchip_mmc_clk_ops;

	mmc_clock->hw.init = &init;
	mmc_clock->reg = reg;
	mmc_clock->shift = shift;

	/*
	 * Assert init_state to soft reset the CLKGEN
	 * for mmc tuning phase and degree
	 */
	if (mmc_clock->shift == ROCKCHIP_MMC_INIT_STATE_SHIFT)
		writel(HIWORD_UPDATE(ROCKCHIP_MMC_INIT_STATE_RESET,
				     ROCKCHIP_MMC_INIT_STATE_RESET,
				     mmc_clock->shift), mmc_clock->reg);

	clk = clk_register(NULL, &mmc_clock->hw);
	if (IS_ERR(clk))
		kfree(mmc_clock);

	return clk;
}
