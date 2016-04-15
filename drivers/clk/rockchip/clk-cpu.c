/*
 * Copyright (c) 2014 MundoReader S.L.
 * Author: Heiko Stuebner <heiko@sntech.de>
 *
 * based on clk/samsung/clk-cpu.c
 * Copyright (c) 2014 Samsung Electronics Co., Ltd.
 * Author: Thomas Abraham <thomas.ab@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * A CPU clock is defined as a clock supplied to a CPU or a group of CPUs.
 * The CPU clock is typically derived from a hierarchy of clock
 * blocks which includes mux and divider blocks. There are a number of other
 * auxiliary clocks supplied to the CPU domain such as the debug blocks and AXI
 * clock for CPU domain. The rates of these auxiliary clocks are related to the
 * CPU clock rate and this relation is usually specified in the hardware manual
 * of the SoC or supplied after the SoC characterization.
 *
 * The below implementation of the CPU clock allows the rate changes of the CPU
 * clock and the corresponding rate changes of the auxillary clocks of the CPU
 * domain. The platform clock driver provides a clock register configuration
 * for each configurable rate which is then used to program the clock hardware
 * registers to acheive a fast co-oridinated rate change for all the CPU domain
 * clocks.
 *
 * On a rate change request for the CPU clock, the rate change is propagated
 * upto the PLL supplying the clock to the CPU domain clock blocks. While the
 * CPU domain PLL is reconfigured, the CPU domain clocks are driven using an
 * alternate clock source. If required, the alternate clock source is divided
 * down in order to keep the output clock rate within the previous OPP limits.
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/io.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "clk.h"

/**
 * struct rockchip_cpuclk: information about clock supplied to a CPU core.
 * @hw:		handle between ccf and cpu clock.
 * @alt_parent:	alternate parent clock to use when switching the speed
 *		of the primary parent clock.
 * @reg_base:	base register for cpu-clock values.
 * @clk_nb:	clock notifier registered for changes in clock speed of the
 *		primary parent clock.
 * @rate_count:	number of rates in the rate_table
 * @rate_table:	pll-rates and their associated dividers
 * @reg_data:	cpu-specific register settings
 * @lock:	clock lock
 */
struct rockchip_cpuclk {
	struct clk_hw				hw;

	struct clk_mux				cpu_mux;
	const struct clk_ops			*cpu_mux_ops;

	struct clk				*alt_parent;
	void __iomem				*reg_base;
	struct notifier_block			clk_nb;
	unsigned int				rate_count;
	struct rockchip_cpuclk_rate_table	*rate_table;
	const struct rockchip_cpuclk_reg_data	*reg_data;
	spinlock_t				*lock;
};

#define to_rockchip_cpuclk_hw(hw) container_of(hw, struct rockchip_cpuclk, hw)
#define to_rockchip_cpuclk_nb(nb) \
			container_of(nb, struct rockchip_cpuclk, clk_nb)

static const struct rockchip_cpuclk_rate_table *rockchip_get_cpuclk_settings(
			    struct rockchip_cpuclk *cpuclk, unsigned long rate)
{
	const struct rockchip_cpuclk_rate_table *rate_table =
							cpuclk->rate_table;
	int i;

	for (i = 0; i < cpuclk->rate_count; i++) {
		if (rate == rate_table[i].prate)
			return &rate_table[i];
	}

	return NULL;
}

static unsigned long rockchip_cpuclk_recalc_rate(struct clk_hw *hw,
					unsigned long parent_rate)
{
	struct rockchip_cpuclk *cpuclk = to_rockchip_cpuclk_hw(hw);
	const struct rockchip_cpuclk_reg_data *reg_data = cpuclk->reg_data;
	u32 clksel0 = readl_relaxed(cpuclk->reg_base + reg_data->core_reg);

	clksel0 >>= reg_data->div_core_shift;
	clksel0 &= reg_data->div_core_mask;
	return parent_rate / (clksel0 + 1);
}

static const struct clk_ops rockchip_cpuclk_ops = {
	.recalc_rate = rockchip_cpuclk_recalc_rate,
};

static void rockchip_cpuclk_set_dividers(struct rockchip_cpuclk *cpuclk,
				const struct rockchip_cpuclk_rate_table *rate)
{
	int i;

	/* alternate parent is active now. set the dividers */
	for (i = 0; i < ARRAY_SIZE(rate->divs); i++) {
		const struct rockchip_cpuclk_clksel *clksel = &rate->divs[i];

		if (!clksel->reg)
			continue;

		pr_debug("%s: setting reg 0x%x to 0x%x\n",
			 __func__, clksel->reg, clksel->val);
		writel(clksel->val, cpuclk->reg_base + clksel->reg);
	}
}

static int rockchip_cpuclk_pre_rate_change(struct rockchip_cpuclk *cpuclk,
					   struct clk_notifier_data *ndata)
{
	const struct rockchip_cpuclk_reg_data *reg_data = cpuclk->reg_data;
	unsigned long alt_prate, alt_div;
	unsigned long flags;

	alt_prate = clk_get_rate(cpuclk->alt_parent);

	spin_lock_irqsave(cpuclk->lock, flags);

	/*
	 * If the old parent clock speed is less than the clock speed
	 * of the alternate parent, then it should be ensured that at no point
	 * the armclk speed is more than the old_rate until the dividers are
	 * set.
	 */
	if (alt_prate > ndata->old_rate) {
		/* calculate dividers */
		alt_div =  DIV_ROUND_UP(alt_prate, ndata->old_rate) - 1;
		if (alt_div > reg_data->div_core_mask) {
			pr_warn("%s: limiting alt-divider %lu to %d\n",
				__func__, alt_div, reg_data->div_core_mask);
			alt_div = reg_data->div_core_mask;
		}

		/*
		 * Change parents and add dividers in a single transaction.
		 *
		 * NOTE: we do this in a single transaction so we're never
		 * dividing the primary parent by the extra dividers that were
		 * needed for the alt.
		 */
		pr_debug("%s: setting div %lu as alt-rate %lu > old-rate %lu\n",
			 __func__, alt_div, alt_prate, ndata->old_rate);

		writel(HIWORD_UPDATE(alt_div, reg_data->div_core_mask,
					      reg_data->div_core_shift) |
		       HIWORD_UPDATE(reg_data->mux_core_alt,
				     reg_data->mux_core_mask,
				     reg_data->mux_core_shift),
		       cpuclk->reg_base + reg_data->core_reg);
	} else {
		/* select alternate parent */
		writel(HIWORD_UPDATE(reg_data->mux_core_alt,
				     reg_data->mux_core_mask,
				     reg_data->mux_core_shift),
		       cpuclk->reg_base + reg_data->core_reg);
	}

	spin_unlock_irqrestore(cpuclk->lock, flags);
	return 0;
}

static int rockchip_cpuclk_post_rate_change(struct rockchip_cpuclk *cpuclk,
					    struct clk_notifier_data *ndata)
{
	const struct rockchip_cpuclk_reg_data *reg_data = cpuclk->reg_data;
	const struct rockchip_cpuclk_rate_table *rate;
	unsigned long flags;

	rate = rockchip_get_cpuclk_settings(cpuclk, ndata->new_rate);
	if (!rate) {
		pr_err("%s: Invalid rate : %lu for cpuclk\n",
		       __func__, ndata->new_rate);
		return -EINVAL;
	}

	spin_lock_irqsave(cpuclk->lock, flags);

	if (ndata->old_rate < ndata->new_rate)
		rockchip_cpuclk_set_dividers(cpuclk, rate);

	/*
	 * post-rate change event, re-mux to primary parent and remove dividers.
	 *
	 * NOTE: we do this in a single transaction so we're never dividing the
	 * primary parent by the extra dividers that were needed for the alt.
	 */

	writel(HIWORD_UPDATE(0, reg_data->div_core_mask,
				reg_data->div_core_shift) |
	       HIWORD_UPDATE(reg_data->mux_core_main,
				reg_data->mux_core_mask,
				reg_data->mux_core_shift),
	       cpuclk->reg_base + reg_data->core_reg);

	if (ndata->old_rate > ndata->new_rate)
		rockchip_cpuclk_set_dividers(cpuclk, rate);

	spin_unlock_irqrestore(cpuclk->lock, flags);
	return 0;
}

/*
 * This clock notifier is called when the frequency of the parent clock
 * of cpuclk is to be changed. This notifier handles the setting up all
 * the divider clocks, remux to temporary parent and handling the safe
 * frequency levels when using temporary parent.
 */
static int rockchip_cpuclk_notifier_cb(struct notifier_block *nb,
					unsigned long event, void *data)
{
	struct clk_notifier_data *ndata = data;
	struct rockchip_cpuclk *cpuclk = to_rockchip_cpuclk_nb(nb);
	int ret = 0;

	pr_debug("%s: event %lu, old_rate %lu, new_rate: %lu\n",
		 __func__, event, ndata->old_rate, ndata->new_rate);
	if (event == PRE_RATE_CHANGE)
		ret = rockchip_cpuclk_pre_rate_change(cpuclk, ndata);
	else if (event == POST_RATE_CHANGE)
		ret = rockchip_cpuclk_post_rate_change(cpuclk, ndata);

	return notifier_from_errno(ret);
}

struct clk *rockchip_clk_register_cpuclk(const char *name,
			const char *const *parent_names, u8 num_parents,
			const struct rockchip_cpuclk_reg_data *reg_data,
			const struct rockchip_cpuclk_rate_table *rates,
			int nrates, void __iomem *reg_base, spinlock_t *lock)
{
	struct rockchip_cpuclk *cpuclk;
	struct clk_init_data init;
	struct clk *clk, *cclk;
	int ret;

	if (num_parents < 2) {
		pr_err("%s: needs at least two parent clocks\n", __func__);
		return ERR_PTR(-EINVAL);
	}

	cpuclk = kzalloc(sizeof(*cpuclk), GFP_KERNEL);
	if (!cpuclk)
		return ERR_PTR(-ENOMEM);

	init.name = name;
	init.parent_names = &parent_names[reg_data->mux_core_main];
	init.num_parents = 1;
	init.ops = &rockchip_cpuclk_ops;

	/* only allow rate changes when we have a rate table */
	init.flags = (nrates > 0) ? CLK_SET_RATE_PARENT : 0;

	/* disallow automatic parent changes by ccf */
	init.flags |= CLK_SET_RATE_NO_REPARENT;

	init.flags |= CLK_GET_RATE_NOCACHE;

	cpuclk->reg_base = reg_base;
	cpuclk->lock = lock;
	cpuclk->reg_data = reg_data;
	cpuclk->clk_nb.notifier_call = rockchip_cpuclk_notifier_cb;
	cpuclk->hw.init = &init;

	cpuclk->alt_parent = __clk_lookup(parent_names[reg_data->mux_core_alt]);
	if (!cpuclk->alt_parent) {
		pr_err("%s: could not lookup alternate parent: (%d)\n",
		       __func__, reg_data->mux_core_alt);
		ret = -EINVAL;
		goto free_cpuclk;
	}

	ret = clk_prepare_enable(cpuclk->alt_parent);
	if (ret) {
		pr_err("%s: could not enable alternate parent\n",
		       __func__);
		goto free_cpuclk;
	}

	clk = __clk_lookup(parent_names[reg_data->mux_core_main]);
	if (!clk) {
		pr_err("%s: could not lookup parent clock: (%d) %s\n",
		       __func__, reg_data->mux_core_main,
		       parent_names[reg_data->mux_core_main]);
		ret = -EINVAL;
		goto free_alt_parent;
	}

	ret = clk_notifier_register(clk, &cpuclk->clk_nb);
	if (ret) {
		pr_err("%s: failed to register clock notifier for %s\n",
				__func__, name);
		goto free_alt_parent;
	}

	if (nrates > 0) {
		cpuclk->rate_count = nrates;
		cpuclk->rate_table = kmemdup(rates,
					     sizeof(*rates) * nrates,
					     GFP_KERNEL);
		if (!cpuclk->rate_table) {
			pr_err("%s: could not allocate memory for cpuclk rates\n",
			       __func__);
			ret = -ENOMEM;
			goto unregister_notifier;
		}
	}

	cclk = clk_register(NULL, &cpuclk->hw);
	if (IS_ERR(clk)) {
		pr_err("%s: could not register cpuclk %s\n", __func__,	name);
		ret = PTR_ERR(clk);
		goto free_rate_table;
	}

	return cclk;

free_rate_table:
	kfree(cpuclk->rate_table);
unregister_notifier:
	clk_notifier_unregister(clk, &cpuclk->clk_nb);
free_alt_parent:
	clk_disable_unprepare(cpuclk->alt_parent);
free_cpuclk:
	kfree(cpuclk);
	return ERR_PTR(ret);
}
