// SPDX-License-Identifier: GPL-2.0
/*
 * r8a7740 Core CPG Clocks
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

struct r8a7740_cpg {
	struct clk_onecell_data data;
	spinlock_t lock;
};

#define CPG_FRQCRA	0x00
#define CPG_FRQCRB	0x04
#define CPG_PLLC2CR	0x2c
#define CPG_USBCKCR	0x8c
#define CPG_FRQCRC	0xe0

struct div4_clk {
	const char *name;
	unsigned int reg;
	unsigned int shift;
};

static struct div4_clk div4_clks[] = {
	{ "i", CPG_FRQCRA, 20 },
	{ "zg", CPG_FRQCRA, 16 },
	{ "b", CPG_FRQCRA,  8 },
	{ "m1", CPG_FRQCRA,  4 },
	{ "hp", CPG_FRQCRB,  4 },
	{ "hpp", CPG_FRQCRC, 20 },
	{ "usbp", CPG_FRQCRC, 16 },
	{ "s", CPG_FRQCRC, 12 },
	{ "zb", CPG_FRQCRC,  8 },
	{ "m3", CPG_FRQCRC,  4 },
	{ "cp", CPG_FRQCRC,  0 },
	{ NULL, 0, 0 },
};

static const struct clk_div_table div4_div_table[] = {
	{ 0, 2 }, { 1, 3 }, { 2, 4 }, { 3, 6 }, { 4, 8 }, { 5, 12 },
	{ 6, 16 }, { 7, 18 }, { 8, 24 }, { 9, 32 }, { 10, 36 }, { 11, 48 },
	{ 13, 72 }, { 14, 96 }, { 0, 0 }
};

static u32 cpg_mode __initdata;

static struct clk * __init
r8a7740_cpg_register_clock(struct device_node *np, struct r8a7740_cpg *cpg,
			   void __iomem *base, const char *name)
{
	const struct clk_div_table *table = NULL;
	const char *parent_name;
	unsigned int shift, reg;
	unsigned int mult = 1;
	unsigned int div = 1;

	if (!strcmp(name, "r")) {
		switch (cpg_mode & (BIT(2) | BIT(1))) {
		case BIT(1) | BIT(2):
			/* extal1 */
			parent_name = of_clk_get_parent_name(np, 0);
			div = 2048;
			break;
		case BIT(2):
			/* extal1 */
			parent_name = of_clk_get_parent_name(np, 0);
			div = 1024;
			break;
		default:
			/* extalr */
			parent_name = of_clk_get_parent_name(np, 2);
			break;
		}
	} else if (!strcmp(name, "system")) {
		parent_name = of_clk_get_parent_name(np, 0);
		if (cpg_mode & BIT(1))
			div = 2;
	} else if (!strcmp(name, "pllc0")) {
		/* PLLC0/1 are configurable multiplier clocks. Register them as
		 * fixed factor clocks for now as there's no generic multiplier
		 * clock implementation and we currently have no need to change
		 * the multiplier value.
		 */
		u32 value = readl(base + CPG_FRQCRC);
		parent_name = "system";
		mult = ((value >> 24) & 0x7f) + 1;
	} else if (!strcmp(name, "pllc1")) {
		u32 value = readl(base + CPG_FRQCRA);
		parent_name = "system";
		mult = ((value >> 24) & 0x7f) + 1;
		div = 2;
	} else if (!strcmp(name, "pllc2")) {
		u32 value = readl(base + CPG_PLLC2CR);
		parent_name = "system";
		mult = ((value >> 24) & 0x3f) + 1;
	} else if (!strcmp(name, "usb24s")) {
		u32 value = readl(base + CPG_USBCKCR);
		if (value & BIT(7))
			/* extal2 */
			parent_name = of_clk_get_parent_name(np, 1);
		else
			parent_name = "system";
		if (!(value & BIT(6)))
			div = 2;
	} else {
		struct div4_clk *c;
		for (c = div4_clks; c->name; c++) {
			if (!strcmp(name, c->name)) {
				parent_name = "pllc1";
				table = div4_div_table;
				reg = c->reg;
				shift = c->shift;
				break;
			}
		}
		if (!c->name)
			return ERR_PTR(-EINVAL);
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

static void __init r8a7740_cpg_clocks_init(struct device_node *np)
{
	struct r8a7740_cpg *cpg;
	void __iomem *base;
	struct clk **clks;
	unsigned int i;
	int num_clks;

	if (of_property_read_u32(np, "renesas,mode", &cpg_mode))
		pr_warn("%s: missing renesas,mode property\n", __func__);

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

		clk = r8a7740_cpg_register_clock(np, cpg, base, name);
		if (IS_ERR(clk))
			pr_err("%s: failed to register %pOFn %s clock (%ld)\n",
			       __func__, np, name, PTR_ERR(clk));
		else
			cpg->data.clks[i] = clk;
	}

	of_clk_add_provider(np, of_clk_src_onecell_get, &cpg->data);
}
CLK_OF_DECLARE(r8a7740_cpg_clks, "renesas,r8a7740-cpg-clocks",
	       r8a7740_cpg_clocks_init);
