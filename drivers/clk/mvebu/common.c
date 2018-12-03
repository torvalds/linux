// SPDX-License-Identifier: GPL-2.0
/*
 * Marvell EBU SoC common clock handling
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 */

#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/syscore_ops.h>

#include "common.h"

/*
 * Core Clocks
 */

#define SSCG_CONF_MODE(reg)	(((reg) >> 16) & 0x3)
#define SSCG_SPREAD_DOWN	0x0
#define SSCG_SPREAD_UP		0x1
#define SSCG_SPREAD_CENTRAL	0x2
#define SSCG_CONF_LOW(reg)	(((reg) >> 8) & 0xFF)
#define SSCG_CONF_HIGH(reg)	((reg) & 0xFF)

static struct clk_onecell_data clk_data;

/*
 * This function can be used by the Kirkwood, the Armada 370, the
 * Armada XP and the Armada 375 SoC. The name of the function was
 * chosen following the dt convention: using the first known SoC
 * compatible with it.
 */
u32 kirkwood_fix_sscg_deviation(u32 system_clk)
{
	struct device_node *sscg_np = NULL;
	void __iomem *sscg_map;
	u32 sscg_reg;
	s32 low_bound, high_bound;
	u64 freq_swing_half;

	sscg_np = of_find_node_by_name(NULL, "sscg");
	if (sscg_np == NULL) {
		pr_err("cannot get SSCG register node\n");
		return system_clk;
	}

	sscg_map = of_iomap(sscg_np, 0);
	if (sscg_map == NULL) {
		pr_err("cannot map SSCG register\n");
		goto out;
	}

	sscg_reg = readl(sscg_map);
	high_bound = SSCG_CONF_HIGH(sscg_reg);
	low_bound = SSCG_CONF_LOW(sscg_reg);

	if ((high_bound - low_bound) <= 0)
		goto out;
	/*
	 * From Marvell engineer we got the following formula (when
	 * this code was written, the datasheet was erroneous)
	 * Spread percentage = 1/96 * (H - L) / H
	 * H = SSCG_High_Boundary
	 * L = SSCG_Low_Boundary
	 *
	 * As the deviation is half of spread then it lead to the
	 * following formula in the code.
	 *
	 * To avoid an overflow and not lose any significant digit in
	 * the same time we have to use a 64 bit integer.
	 */

	freq_swing_half = (((u64)high_bound - (u64)low_bound)
			* (u64)system_clk);
	do_div(freq_swing_half, (2 * 96 * high_bound));

	switch (SSCG_CONF_MODE(sscg_reg)) {
	case SSCG_SPREAD_DOWN:
		system_clk -= freq_swing_half;
		break;
	case SSCG_SPREAD_UP:
		system_clk += freq_swing_half;
		break;
	case SSCG_SPREAD_CENTRAL:
	default:
		break;
	}

	iounmap(sscg_map);

out:
	of_node_put(sscg_np);

	return system_clk;
}

void __init mvebu_coreclk_setup(struct device_node *np,
				const struct coreclk_soc_desc *desc)
{
	const char *tclk_name = "tclk";
	const char *cpuclk_name = "cpuclk";
	void __iomem *base;
	unsigned long rate;
	int n;

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		return;

	/* Allocate struct for TCLK, cpu clk, and core ratio clocks */
	clk_data.clk_num = 2 + desc->num_ratios;

	/* One more clock for the optional refclk */
	if (desc->get_refclk_freq)
		clk_data.clk_num += 1;

	clk_data.clks = kcalloc(clk_data.clk_num, sizeof(*clk_data.clks),
				GFP_KERNEL);
	if (WARN_ON(!clk_data.clks)) {
		iounmap(base);
		return;
	}

	/* Register TCLK */
	of_property_read_string_index(np, "clock-output-names", 0,
				      &tclk_name);
	rate = desc->get_tclk_freq(base);
	clk_data.clks[0] = clk_register_fixed_rate(NULL, tclk_name, NULL, 0,
						   rate);
	WARN_ON(IS_ERR(clk_data.clks[0]));

	/* Register CPU clock */
	of_property_read_string_index(np, "clock-output-names", 1,
				      &cpuclk_name);
	rate = desc->get_cpu_freq(base);

	if (desc->is_sscg_enabled && desc->fix_sscg_deviation
		&& desc->is_sscg_enabled(base))
		rate = desc->fix_sscg_deviation(rate);

	clk_data.clks[1] = clk_register_fixed_rate(NULL, cpuclk_name, NULL, 0,
						   rate);
	WARN_ON(IS_ERR(clk_data.clks[1]));

	/* Register fixed-factor clocks derived from CPU clock */
	for (n = 0; n < desc->num_ratios; n++) {
		const char *rclk_name = desc->ratios[n].name;
		int mult, div;

		of_property_read_string_index(np, "clock-output-names",
					      2+n, &rclk_name);
		desc->get_clk_ratio(base, desc->ratios[n].id, &mult, &div);
		clk_data.clks[2+n] = clk_register_fixed_factor(NULL, rclk_name,
				       cpuclk_name, 0, mult, div);
		WARN_ON(IS_ERR(clk_data.clks[2+n]));
	}

	/* Register optional refclk */
	if (desc->get_refclk_freq) {
		const char *name = "refclk";
		of_property_read_string_index(np, "clock-output-names",
					      2 + desc->num_ratios, &name);
		rate = desc->get_refclk_freq(base);
		clk_data.clks[2 + desc->num_ratios] =
			clk_register_fixed_rate(NULL, name, NULL, 0, rate);
		WARN_ON(IS_ERR(clk_data.clks[2 + desc->num_ratios]));
	}

	/* SAR register isn't needed anymore */
	iounmap(base);

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

/*
 * Clock Gating Control
 */

DEFINE_SPINLOCK(ctrl_gating_lock);

struct clk_gating_ctrl {
	spinlock_t *lock;
	struct clk **gates;
	int num_gates;
	void __iomem *base;
	u32 saved_reg;
};

static struct clk_gating_ctrl *ctrl;

static struct clk *clk_gating_get_src(
	struct of_phandle_args *clkspec, void *data)
{
	int n;

	if (clkspec->args_count < 1)
		return ERR_PTR(-EINVAL);

	for (n = 0; n < ctrl->num_gates; n++) {
		struct clk_gate *gate =
			to_clk_gate(__clk_get_hw(ctrl->gates[n]));
		if (clkspec->args[0] == gate->bit_idx)
			return ctrl->gates[n];
	}
	return ERR_PTR(-ENODEV);
}

static int mvebu_clk_gating_suspend(void)
{
	ctrl->saved_reg = readl(ctrl->base);
	return 0;
}

static void mvebu_clk_gating_resume(void)
{
	writel(ctrl->saved_reg, ctrl->base);
}

static struct syscore_ops clk_gate_syscore_ops = {
	.suspend = mvebu_clk_gating_suspend,
	.resume = mvebu_clk_gating_resume,
};

void __init mvebu_clk_gating_setup(struct device_node *np,
				   const struct clk_gating_soc_desc *desc)
{
	struct clk *clk;
	void __iomem *base;
	const char *default_parent = NULL;
	int n;

	if (ctrl) {
		pr_err("mvebu-clk-gating: cannot instantiate more than one gatable clock device\n");
		return;
	}

	base = of_iomap(np, 0);
	if (WARN_ON(!base))
		return;

	clk = of_clk_get(np, 0);
	if (!IS_ERR(clk)) {
		default_parent = __clk_get_name(clk);
		clk_put(clk);
	}

	ctrl = kzalloc(sizeof(*ctrl), GFP_KERNEL);
	if (WARN_ON(!ctrl))
		goto ctrl_out;

	/* lock must already be initialized */
	ctrl->lock = &ctrl_gating_lock;

	ctrl->base = base;

	/* Count, allocate, and register clock gates */
	for (n = 0; desc[n].name;)
		n++;

	ctrl->num_gates = n;
	ctrl->gates = kcalloc(ctrl->num_gates, sizeof(*ctrl->gates),
			      GFP_KERNEL);
	if (WARN_ON(!ctrl->gates))
		goto gates_out;

	for (n = 0; n < ctrl->num_gates; n++) {
		const char *parent =
			(desc[n].parent) ? desc[n].parent : default_parent;
		ctrl->gates[n] = clk_register_gate(NULL, desc[n].name, parent,
					desc[n].flags, base, desc[n].bit_idx,
					0, ctrl->lock);
		WARN_ON(IS_ERR(ctrl->gates[n]));
	}

	of_clk_add_provider(np, clk_gating_get_src, ctrl);

	register_syscore_ops(&clk_gate_syscore_ops);

	return;
gates_out:
	kfree(ctrl);
ctrl_out:
	iounmap(base);
}
