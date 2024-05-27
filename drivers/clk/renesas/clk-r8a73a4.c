// SPDX-License-Identifier: GPL-2.0
/*
 * r8a73a4 Core CPG Clocks
 *
 * Copyright (C) 2014  Ulrich Hecht
 */

#include <linux/clk-provider.h>
#include <linux/clk/renesas.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/spinlock.h>

struct r8a73a4_cpg {
	struct clk_onecell_data data;
	spinlock_t lock;
};

#define CPG_CKSCR	0xc0
#define CPG_FRQCRA	0x00
#define CPG_FRQCRB	0x04
#define CPG_FRQCRC	0xe0
#define CPG_PLL0CR	0xd8
#define CPG_PLL1CR	0x28
#define CPG_PLL2CR	0x2c
#define CPG_PLL2HCR	0xe4
#define CPG_PLL2SCR	0xf4

struct div4_clk {
	const char *name;
	unsigned int reg;
	unsigned int shift;
};

static struct div4_clk div4_clks[] = {
	{ "i",	CPG_FRQCRA, 20 },
	{ "m3", CPG_FRQCRA, 12 },
	{ "b",	CPG_FRQCRA,  8 },
	{ "m1", CPG_FRQCRA,  4 },
	{ "m2", CPG_FRQCRA,  0 },
	{ "zx", CPG_FRQCRB, 12 },
	{ "zs", CPG_FRQCRB,  8 },
	{ "hp", CPG_FRQCRB,  4 },
	{ NULL, 0, 0 },
};

static const struct clk_div_table div4_div_table[] = {
	{ 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 6 }, { 4, 8 }, { 5, 12 },
	{ 6, 16 }, { 7, 18 }, { 8, 24 }, { 10, 36 }, { 11, 48 },
	{ 12, 10 }, { 0, 0 }
};

static struct clk * __init
r8a73a4_cpg_register_clock(struct device_node *np, struct r8a73a4_cpg *cpg,
			   void __iomem *base, const char *name)
{
	const struct clk_div_table *table = NULL;
	const char *parent_name;
	unsigned int shift, reg;
	unsigned int mult = 1;
	unsigned int div = 1;


	if (!strcmp(name, "main")) {
		u32 ckscr = readl(base + CPG_CKSCR);

		switch ((ckscr >> 28) & 3) {
		case 0:	/* extal1 */
			parent_name = of_clk_get_parent_name(np, 0);
			break;
		case 1:	/* extal1 / 2 */
			parent_name = of_clk_get_parent_name(np, 0);
			div = 2;
			break;
		case 2: /* extal2 */
			parent_name = of_clk_get_parent_name(np, 1);
			break;
		case 3: /* extal2 / 2 */
			parent_name = of_clk_get_parent_name(np, 1);
			div = 2;
			break;
		}
	} else if (!strcmp(name, "pll0")) {
		/* PLL0/1 are configurable multiplier clocks. Register them as
		 * fixed factor clocks for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		u32 value = readl(base + CPG_PLL0CR);

		parent_name = "main";
		mult = ((value >> 24) & 0x7f) + 1;
		if (value & BIT(20))
			div = 2;
	} else if (!strcmp(name, "pll1")) {
		u32 value = readl(base + CPG_PLL1CR);

		parent_name = "main";
		/* XXX: enable bit? */
		mult = ((value >> 24) & 0x7f) + 1;
		if (value & BIT(7))
			div = 2;
	} else if (!strncmp(name, "pll2", 4)) {
		u32 value, cr;

		switch (name[4]) {
		case 0:
			cr = CPG_PLL2CR;
			break;
		case 's':
			cr = CPG_PLL2SCR;
			break;
		case 'h':
			cr = CPG_PLL2HCR;
			break;
		default:
			return ERR_PTR(-EINVAL);
		}
		value = readl(base + cr);
		switch ((value >> 5) & 7) {
		case 0:
			parent_name = "main";
			div = 2;
			break;
		case 1:
			parent_name = "extal2";
			div = 2;
			break;
		case 3:
			parent_name = "extal2";
			div = 4;
			break;
		case 4:
			parent_name = "main";
			break;
		case 5:
			parent_name = "extal2";
			break;
		default:
			pr_warn("%s: unexpected parent of %s\n", __func__,
				name);
			return ERR_PTR(-EINVAL);
		}
		/* XXX: enable bit? */
		mult = ((value >> 24) & 0x7f) + 1;
	} else if (!strcmp(name, "z") || !strcmp(name, "z2")) {
		u32 shift = 8;

		parent_name = "pll0";
		if (name[1] == '2') {
			div = 2;
			shift = 0;
		}
		div *= 32;
		mult = 0x20 - ((readl(base + CPG_FRQCRC) >> shift) & 0x1f);
	} else {
		struct div4_clk *c;

		for (c = div4_clks; c->name; c++) {
			if (!strcmp(name, c->name))
				break;
		}
		if (!c->name)
			return ERR_PTR(-EINVAL);

		parent_name = "pll1";
		table = div4_div_table;
		reg = c->reg;
		shift = c->shift;
	}

	if (!table) {
		return clk_register_fixed_factor(NULL, name, parent_name, 0,
						 mult, div);
	} else {
		return clk_register_divider_table(NULL, name, parent_name, 0,
						  base + reg, shift, 4, 0,
						  table, &cpg->lock);
	}
}

static void __init r8a73a4_cpg_clocks_init(struct device_node *np)
{
	struct r8a73a4_cpg *cpg;
	void __iomem *base;
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

	base = of_iomap(np, 0);
	if (WARN_ON(base == NULL))
		return;

	for (i = 0; i < num_clks; ++i) {
		const char *name;
		struct clk *clk;

		of_property_read_string_index(np, "clock-output-names", i,
					      &name);

		clk = r8a73a4_cpg_register_clock(np, cpg, base, name);
		if (IS_ERR(clk))
			pr_err("%s: failed to register %pOFn %s clock (%ld)\n",
			       __func__, np, name, PTR_ERR(clk));
		else
			cpg->data.clks[i] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &cpg->data);
}
CLK_OF_DECLARE(r8a73a4_cpg_clks, "renesas,r8a73a4-cpg-clocks",
	       r8a73a4_cpg_clocks_init);
