/*
 * rz Core CPG Clocks
 *
 * Copyright (C) 2013 Ideas On Board SPRL
 * Copyright (C) 2014 Wolfram Sang, Sang Engineering <wsa@sang-engineering.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

struct rz_cpg {
	struct clk_onecell_data data;
	void __iomem *reg;
};

#define CPG_FRQCR	0x10
#define CPG_FRQCR2	0x14

/* -----------------------------------------------------------------------------
 * Initialization
 */

static struct clk * __init
rz_cpg_register_clock(struct device_node *np, struct rz_cpg *cpg, const char *name)
{
	u32 val;
	unsigned mult;
	static const unsigned frqcr_tab[4] = { 3, 2, 0, 1 };

	if (strcmp(name, "pll") == 0) {
		/* FIXME: cpg_mode should be read from GPIO. But no GPIO support yet */
		unsigned cpg_mode = 0; /* hardcoded to EXTAL for now */
		const char *parent_name = of_clk_get_parent_name(np, cpg_mode);

		mult = cpg_mode ? (32 / 4) : 30;

		return clk_register_fixed_factor(NULL, name, parent_name, 0, mult, 1);
	}

	/* If mapping regs failed, skip non-pll clocks. System will boot anyhow */
	if (!cpg->reg)
		return ERR_PTR(-ENXIO);

	/* FIXME:"i" and "g" are variable clocks with non-integer dividers (e.g. 2/3)
	 * and the constraint that always g <= i. To get the rz platform started,
	 * let them run at fixed current speed and implement the details later.
	 */
	if (strcmp(name, "i") == 0)
		val = (clk_readl(cpg->reg + CPG_FRQCR) >> 8) & 3;
	else if (strcmp(name, "g") == 0)
		val = clk_readl(cpg->reg + CPG_FRQCR2) & 3;
	else
		return ERR_PTR(-EINVAL);

	mult = frqcr_tab[val];
	return clk_register_fixed_factor(NULL, name, "pll", 0, mult, 3);
}

static void __init rz_cpg_clocks_init(struct device_node *np)
{
	struct rz_cpg *cpg;
	struct clk **clks;
	unsigned i;
	int num_clks;

	num_clks = of_property_count_strings(np, "clock-output-names");
	if (WARN(num_clks <= 0, "can't count CPG clocks\n"))
		return;

	cpg = kzalloc(sizeof(*cpg), GFP_KERNEL);
	clks = kzalloc(num_clks * sizeof(*clks), GFP_KERNEL);
	BUG_ON(!cpg || !clks);

	cpg->data.clks = clks;
	cpg->data.clk_num = num_clks;

	cpg->reg = of_iomap(np, 0);

	for (i = 0; i < num_clks; ++i) {
		const char *name;
		struct clk *clk;

		of_property_read_string_index(np, "clock-output-names", i, &name);

		clk = rz_cpg_register_clock(np, cpg, name);
		if (IS_ERR(clk))
			pr_err("%s: failed to register %s %s clock (%ld)\n",
			       __func__, np->name, name, PTR_ERR(clk));
		else
			cpg->data.clks[i] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &cpg->data);

	cpg_mstp_add_clk_domain(np);
}
CLK_OF_DECLARE(rz_cpg_clks, "renesas,rz-cpg-clocks", rz_cpg_clocks_init);
