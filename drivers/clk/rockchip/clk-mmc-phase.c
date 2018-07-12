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
	int		cached_phase;
	struct notifier_block clk_rate_change_nb;
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

	/* See the comment for rockchip_mmc_set_phase below */
	if (!rate) {
		pr_err("%s: invalid clk rate\n", __func__);
		return -EINVAL;
	}

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

	/*
	 * The below calculation is based on the output clock from
	 * MMC host to the card, which expects the phase clock inherits
	 * the clock rate from its parent, namely the output clock
	 * provider of MMC host. However, things may go wrong if
	 * (1) It is orphan.
	 * (2) It is assigned to the wrong parent.
	 *
	 * This check help debug the case (1), which seems to be the
	 * most likely problem we often face and which makes it difficult
	 * for people to debug unstable mmc tuning results.
	 */
	if (!rate) {
		pr_err("%s: invalid clk rate\n", __func__);
		return -EINVAL;
	}

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
	writel(HIWORD_UPDATE(raw_value, 0x07ff, mmc_clock->shift),
	       mmc_clock->reg);

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

#define to_rockchip_mmc_clock(x) \
	container_of(x, struct rockchip_mmc_clock, clk_rate_change_nb)
static int rockchip_mmc_clk_rate_notify(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct rockchip_mmc_clock *mmc_clock = to_rockchip_mmc_clock(nb);
	struct clk_notifier_data *ndata = data;

	/*
	 * rockchip_mmc_clk is mostly used by mmc controllers to sample
	 * the intput data, which expects the fixed phase after the tuning
	 * process. However if the clock rate is changed, the phase is stale
	 * and may break the data sampling. So here we try to restore the phase
	 * for that case, except that
	 * (1) cached_phase is invaild since we inevitably cached it when the
	 * clock provider be reparented from orphan to its real parent in the
	 * first place. Otherwise we may mess up the initialization of MMC cards
	 * since we only set the default sample phase and drive phase later on.
	 * (2) the new coming rate is higher than the older one since mmc driver
	 * set the max-frequency to match the boards' ability but we can't go
	 * over the heads of that, otherwise the tests smoke out the issue.
	 */
	if (ndata->old_rate <= ndata->new_rate)
		return NOTIFY_DONE;

	if (event == PRE_RATE_CHANGE)
		mmc_clock->cached_phase =
			rockchip_mmc_get_phase(&mmc_clock->hw);
	else if (mmc_clock->cached_phase != -EINVAL &&
		 event == POST_RATE_CHANGE)
		rockchip_mmc_set_phase(&mmc_clock->hw, mmc_clock->cached_phase);

	return NOTIFY_DONE;
}

struct clk *rockchip_clk_register_mmc(const char *name,
				const char *const *parent_names, u8 num_parents,
				void __iomem *reg, int shift)
{
	struct clk_init_data init;
	struct rockchip_mmc_clock *mmc_clock;
	struct clk *clk;
	int ret;

	mmc_clock = kmalloc(sizeof(*mmc_clock), GFP_KERNEL);
	if (!mmc_clock)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.flags = 0;
	init.num_parents = num_parents;
	init.parent_names = parent_names;
	init.ops = &rockchip_mmc_clk_ops;

	mmc_clock->hw.init = &init;
	mmc_clock->reg = reg;
	mmc_clock->shift = shift;

	clk = clk_register(NULL, &mmc_clock->hw);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		goto err_register;
	}

	mmc_clock->clk_rate_change_nb.notifier_call =
				&rockchip_mmc_clk_rate_notify;
	ret = clk_notifier_register(clk, &mmc_clock->clk_rate_change_nb);
	if (ret)
		goto err_notifier;

	return clk;
err_notifier:
	clk_unregister(clk);
err_register:
	kfree(mmc_clock);
	return ERR_PTR(ret);
}
