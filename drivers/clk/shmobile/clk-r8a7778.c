/*
 * r8a7778 Core CPG Clocks
 *
 * Copyright (C) 2014  Ulrich Hecht
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 */

#include <linux/clk-provider.h>
#include <linux/clk/shmobile.h>
#include <linux/of_address.h>
#include <linux/slab.h>

struct r8a7778_cpg {
	struct clk_onecell_data data;
	spinlock_t lock;
	void __iomem *reg;
};

/* PLL multipliers per bits 11, 12, and 18 of MODEMR */
struct {
	unsigned long plla_mult;
	unsigned long pllb_mult;
} r8a7778_rates[] __initdata = {
	[0] = { 21, 21 },
	[1] = { 24, 24 },
	[2] = { 28, 28 },
	[3] = { 32, 32 },
	[5] = { 24, 21 },
	[6] = { 28, 21 },
	[7] = { 32, 24 },
};

/* Clock dividers per bits 1 and 2 of MODEMR */
struct {
	const char *name;
	unsigned int div[4];
} r8a7778_divs[6] __initdata = {
	{ "b",   { 12, 12, 16, 18 } },
	{ "out", { 12, 12, 16, 18 } },
	{ "p",   { 16, 12, 16, 12 } },
	{ "s",   { 4,  3,  4,  3  } },
	{ "s1",  { 8,  6,  8,  6  } },
};

static u32 cpg_mode_rates __initdata;
static u32 cpg_mode_divs __initdata;

static struct clk * __init
r8a7778_cpg_register_clock(struct device_node *np, struct r8a7778_cpg *cpg,
			     const char *name)
{
	if (!strcmp(name, "plla")) {
		return clk_register_fixed_factor(NULL, "plla",
			of_clk_get_parent_name(np, 0), 0,
			r8a7778_rates[cpg_mode_rates].plla_mult, 1);
	} else if (!strcmp(name, "pllb")) {
		return clk_register_fixed_factor(NULL, "pllb",
			of_clk_get_parent_name(np, 0), 0,
			r8a7778_rates[cpg_mode_rates].pllb_mult, 1);
	} else {
		unsigned int i;

		for (i = 0; i < ARRAY_SIZE(r8a7778_divs); i++) {
			if (!strcmp(name, r8a7778_divs[i].name)) {
				return clk_register_fixed_factor(NULL,
					r8a7778_divs[i].name,
					"plla", 0, 1,
					r8a7778_divs[i].div[cpg_mode_divs]);
			}
		}
	}

	return ERR_PTR(-EINVAL);
}


static void __init r8a7778_cpg_clocks_init(struct device_node *np)
{
	struct r8a7778_cpg *cpg;
	struct clk **clks;
	unsigned int i;
	int num_clks;

	num_clks = of_property_count_strings(np, "clock-output-names");
	if (num_clks < 0) {
		pr_err("%s: failed to count clocks\n", __func__);
		return;
	}

	cpg = kzalloc(sizeof(*cpg), GFP_KERNEL);
	clks = kcalloc(num_clks, sizeof(*clks), GFP_KERNEL);
	if (cpg == NULL || clks == NULL) {
		/* We're leaking memory on purpose, there's no point in cleaning
		 * up as the system won't boot anyway.
		 */
		return;
	}

	spin_lock_init(&cpg->lock);

	cpg->data.clks = clks;
	cpg->data.clk_num = num_clks;

	cpg->reg = of_iomap(np, 0);
	if (WARN_ON(cpg->reg == NULL))
		return;

	for (i = 0; i < num_clks; ++i) {
		const char *name;
		struct clk *clk;

		of_property_read_string_index(np, "clock-output-names", i,
					      &name);

		clk = r8a7778_cpg_register_clock(np, cpg, name);
		if (IS_ERR(clk))
			pr_err("%s: failed to register %s %s clock (%ld)\n",
			       __func__, np->name, name, PTR_ERR(clk));
		else
			cpg->data.clks[i] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &cpg->data);
}

CLK_OF_DECLARE(r8a7778_cpg_clks, "renesas,r8a7778-cpg-clocks",
	       r8a7778_cpg_clocks_init);

void __init r8a7778_clocks_init(u32 mode)
{
	BUG_ON(!(mode & BIT(19)));

	cpg_mode_rates = (!!(mode & BIT(18)) << 2) |
			 (!!(mode & BIT(12)) << 1) |
			 (!!(mode & BIT(11)));
	cpg_mode_divs = (!!(mode & BIT(2)) << 1) |
			(!!(mode & BIT(1)));

	of_clk_init(NULL);
}
