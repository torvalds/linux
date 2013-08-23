/*
 * Marvell EBU SoC common clock handling
 *
 * Copyright (C) 2012 Marvell
 *
 * Gregory CLEMENT <gregory.clement@free-electrons.com>
 * Sebastian Hesselbarth <sebastian.hesselbarth@gmail.com>
 * Andrew Lunn <andrew@lunn.ch>
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "common.h"

/*
 * Core Clocks
 */

static struct clk_onecell_data clk_data;

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
	clk_data.clks = kzalloc(clk_data.clk_num * sizeof(struct clk *),
				GFP_KERNEL);
	if (WARN_ON(!clk_data.clks)) {
		iounmap(base);
		return;
	}

	/* Register TCLK */
	of_property_read_string_index(np, "clock-output-names", 0,
				      &tclk_name);
	rate = desc->get_tclk_freq(base);
	clk_data.clks[0] = clk_register_fixed_rate(NULL, tclk_name, NULL,
						   CLK_IS_ROOT, rate);
	WARN_ON(IS_ERR(clk_data.clks[0]));

	/* Register CPU clock */
	of_property_read_string_index(np, "clock-output-names", 1,
				      &cpuclk_name);
	rate = desc->get_cpu_freq(base);
	clk_data.clks[1] = clk_register_fixed_rate(NULL, cpuclk_name, NULL,
						   CLK_IS_ROOT, rate);
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
	};

	/* SAR register isn't needed anymore */
	iounmap(base);

	of_clk_add_provider(np, of_clk_src_onecell_get, &clk_data);
}

/*
 * Clock Gating Control
 */

struct clk_gating_ctrl {
	spinlock_t lock;
	struct clk **gates;
	int num_gates;
};

#define to_clk_gate(_hw) container_of(_hw, struct clk_gate, hw)

static struct clk *clk_gating_get_src(
	struct of_phandle_args *clkspec, void *data)
{
	struct clk_gating_ctrl *ctrl = (struct clk_gating_ctrl *)data;
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

void __init mvebu_clk_gating_setup(struct device_node *np,
				   const struct clk_gating_soc_desc *desc)
{
	struct clk_gating_ctrl *ctrl;
	struct clk *clk;
	void __iomem *base;
	const char *default_parent = NULL;
	int n;

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

	spin_lock_init(&ctrl->lock);

	/* Count, allocate, and register clock gates */
	for (n = 0; desc[n].name;)
		n++;

	ctrl->num_gates = n;
	ctrl->gates = kzalloc(ctrl->num_gates * sizeof(struct clk *),
			      GFP_KERNEL);
	if (WARN_ON(!ctrl->gates))
		goto gates_out;

	for (n = 0; n < ctrl->num_gates; n++) {
		const char *parent =
			(desc[n].parent) ? desc[n].parent : default_parent;
		ctrl->gates[n] = clk_register_gate(NULL, desc[n].name, parent,
					desc[n].flags, base, desc[n].bit_idx,
					0, &ctrl->lock);
		WARN_ON(IS_ERR(ctrl->gates[n]));
	}

	of_clk_add_provider(np, clk_gating_get_src, ctrl);

	return;
gates_out:
	kfree(ctrl);
ctrl_out:
	iounmap(base);
}
