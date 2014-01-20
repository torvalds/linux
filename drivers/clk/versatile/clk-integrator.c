/*
 * Clock driver for the ARM Integrator/AP and Integrator/CP boards
 * Copyright (C) 2012 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/clk-provider.h>
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_address.h>

#include "clk-icst.h"

#define INTEGRATOR_HDR_LOCK_OFFSET	0x14

/* Base offset for the core module */
static void __iomem *cm_base;

static const struct icst_params cp_auxosc_params = {
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min 	= 8,
	.vd_max 	= 263,
	.rd_min 	= 3,
	.rd_max 	= 65,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct clk_icst_desc __initdata cm_auxosc_desc = {
	.params = &cp_auxosc_params,
	.vco_offset = 0x1c,
	.lock_offset = INTEGRATOR_HDR_LOCK_OFFSET,
};

static void __init of_integrator_cm_osc_setup(struct device_node *np)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
	const struct clk_icst_desc *desc = &cm_auxosc_desc;
	const char *parent_name;

	if (!cm_base) {
		/* Remap the core module base if not done yet */
		struct device_node *parent;

		parent = of_get_parent(np);
		if (!np) {
			pr_err("no parent on core module clock\n");
			return;
		}
		cm_base = of_iomap(parent, 0);
		if (!cm_base) {
			pr_err("could not remap core module base\n");
			return;
		}
	}

	parent_name = of_clk_get_parent_name(np, 0);
	clk = icst_clk_register(NULL, desc, clk_name, parent_name, cm_base);
	if (!IS_ERR(clk))
		of_clk_add_provider(np, of_clk_src_simple_get, clk);
}
CLK_OF_DECLARE(integrator_cm_auxosc_clk,
	"arm,integrator-cm-auxosc", of_integrator_cm_osc_setup);
