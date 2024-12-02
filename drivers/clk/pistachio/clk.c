// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2014 Google, Inc.
 */

#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/slab.h>

#include "clk.h"

struct pistachio_clk_provider *
pistachio_clk_alloc_provider(struct device_node *node, unsigned int num_clks)
{
	struct pistachio_clk_provider *p;

	p = kzalloc(sizeof(*p), GFP_KERNEL);
	if (!p)
		return p;

	p->clk_data.clks = kcalloc(num_clks, sizeof(struct clk *), GFP_KERNEL);
	if (!p->clk_data.clks)
		goto free_provider;
	p->clk_data.clk_num = num_clks;
	p->node = node;
	p->base = of_iomap(node, 0);
	if (!p->base) {
		pr_err("Failed to map clock provider registers\n");
		goto free_clks;
	}

	return p;

free_clks:
	kfree(p->clk_data.clks);
free_provider:
	kfree(p);
	return NULL;
}

void pistachio_clk_register_provider(struct pistachio_clk_provider *p)
{
	unsigned int i;

	for (i = 0; i < p->clk_data.clk_num; i++) {
		if (IS_ERR(p->clk_data.clks[i]))
			pr_warn("Failed to register clock %d: %ld\n", i,
				PTR_ERR(p->clk_data.clks[i]));
	}

	of_clk_add_provider(p->node, of_clk_src_onecell_get, &p->clk_data);
}

void pistachio_clk_register_gate(struct pistachio_clk_provider *p,
				 struct pistachio_gate *gate,
				 unsigned int num)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < num; i++) {
		clk = clk_register_gate(NULL, gate[i].name, gate[i].parent,
					CLK_SET_RATE_PARENT,
					p->base + gate[i].reg, gate[i].shift,
					0, NULL);
		p->clk_data.clks[gate[i].id] = clk;
	}
}

void pistachio_clk_register_mux(struct pistachio_clk_provider *p,
				struct pistachio_mux *mux,
				unsigned int num)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < num; i++) {
		clk = clk_register_mux(NULL, mux[i].name, mux[i].parents,
				       mux[i].num_parents,
				       CLK_SET_RATE_NO_REPARENT,
				       p->base + mux[i].reg, mux[i].shift,
				       get_count_order(mux[i].num_parents),
				       0, NULL);
		p->clk_data.clks[mux[i].id] = clk;
	}
}

void pistachio_clk_register_div(struct pistachio_clk_provider *p,
				struct pistachio_div *div,
				unsigned int num)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < num; i++) {
		clk = clk_register_divider(NULL, div[i].name, div[i].parent,
					   0, p->base + div[i].reg, 0,
					   div[i].width, div[i].div_flags,
					   NULL);
		p->clk_data.clks[div[i].id] = clk;
	}
}

void pistachio_clk_register_fixed_factor(struct pistachio_clk_provider *p,
					 struct pistachio_fixed_factor *ff,
					 unsigned int num)
{
	struct clk *clk;
	unsigned int i;

	for (i = 0; i < num; i++) {
		clk = clk_register_fixed_factor(NULL, ff[i].name, ff[i].parent,
						0, 1, ff[i].div);
		p->clk_data.clks[ff[i].id] = clk;
	}
}

void pistachio_clk_force_enable(struct pistachio_clk_provider *p,
				unsigned int *clk_ids, unsigned int num)
{
	unsigned int i;
	int err;

	for (i = 0; i < num; i++) {
		struct clk *clk = p->clk_data.clks[clk_ids[i]];

		if (IS_ERR(clk))
			continue;

		err = clk_prepare_enable(clk);
		if (err)
			pr_err("Failed to enable clock %s: %d\n",
			       __clk_get_name(clk), err);
	}
}
