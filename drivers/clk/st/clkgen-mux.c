/*
 * clkgen-mux.c: ST GEN-MUX Clock driver
 *
 * Copyright (C) 2014 STMicroelectronics (R&D) Limited
 *
 * Authors: Stephen Gallimore <stephen.gallimore@st.com>
 *	    Pankaj Dev <pankaj.dev@st.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/slab.h>
#include <linux/of_address.h>
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include "clkgen.h"

static const char ** __init clkgen_mux_get_parents(struct device_node *np,
						       int *num_parents)
{
	const char **parents;
	unsigned int nparents;

	nparents = of_clk_get_parent_count(np);
	if (WARN_ON(!nparents))
		return ERR_PTR(-EINVAL);

	parents = kcalloc(nparents, sizeof(const char *), GFP_KERNEL);
	if (!parents)
		return ERR_PTR(-ENOMEM);

	*num_parents = of_clk_parent_fill(np, parents, nparents);
	return parents;
}

struct clkgen_mux_data {
	u32 offset;
	u8 shift;
	u8 width;
	spinlock_t *lock;
	unsigned long clk_flags;
	u8 mux_flags;
};

static struct clkgen_mux_data stih407_a9_mux_data = {
	.offset = 0x1a4,
	.shift = 0,
	.width = 2,
	.lock = &clkgen_a9_lock,
};

static void __init st_of_clkgen_mux_setup(struct device_node *np,
		struct clkgen_mux_data *data)
{
	struct clk *clk;
	void __iomem *reg;
	const char **parents;
	int num_parents = 0;

	reg = of_iomap(np, 0);
	if (!reg) {
		pr_err("%s: Failed to get base address\n", __func__);
		return;
	}

	parents = clkgen_mux_get_parents(np, &num_parents);
	if (IS_ERR(parents)) {
		pr_err("%s: Failed to get parents (%ld)\n",
				__func__, PTR_ERR(parents));
		goto err_parents;
	}

	clk = clk_register_mux(NULL, np->name, parents, num_parents,
				data->clk_flags | CLK_SET_RATE_PARENT,
				reg + data->offset,
				data->shift, data->width, data->mux_flags,
				data->lock);
	if (IS_ERR(clk))
		goto err;

	pr_debug("%s: parent %s rate %u\n",
			__clk_get_name(clk),
			__clk_get_name(clk_get_parent(clk)),
			(unsigned int)clk_get_rate(clk));

	kfree(parents);
	of_clk_add_provider(np, of_clk_src_simple_get, clk);
	return;

err:
	kfree(parents);
err_parents:
	iounmap(reg);
}

static void __init st_of_clkgen_a9_mux_setup(struct device_node *np)
{
	st_of_clkgen_mux_setup(np, &stih407_a9_mux_data);
}
CLK_OF_DECLARE(clkgen_a9mux, "st,stih407-clkgen-a9-mux",
		st_of_clkgen_a9_mux_setup);
