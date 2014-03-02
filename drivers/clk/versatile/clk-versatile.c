/*
 * Clock driver for the ARM Integrator/AP, Integrator/CP, Versatile AB and
 * Versatile PB boards.
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

#define VERSATILE_SYS_OSCCLCD_OFFSET	0x1c
#define VERSATILE_SYS_LOCK_OFFSET	0x20

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

static const struct icst_params versatile_auxosc_params = {
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

static const struct clk_icst_desc versatile_auxosc_desc __initconst = {
	.params = &versatile_auxosc_params,
	.vco_offset = VERSATILE_SYS_OSCCLCD_OFFSET,
	.lock_offset = VERSATILE_SYS_LOCK_OFFSET,
};
static void __init cm_osc_setup(struct device_node *np,
				const struct clk_icst_desc *desc)
{
	struct clk *clk = ERR_PTR(-EINVAL);
	const char *clk_name = np->name;
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

static void __init of_integrator_cm_osc_setup(struct device_node *np)
{
	cm_osc_setup(np, &cm_auxosc_desc);
}
CLK_OF_DECLARE(integrator_cm_auxosc_clk,
	"arm,integrator-cm-auxosc", of_integrator_cm_osc_setup);

static void __init of_versatile_cm_osc_setup(struct device_node *np)
{
	cm_osc_setup(np, &versatile_auxosc_desc);
}
CLK_OF_DECLARE(versatile_cm_auxosc_clk,
	       "arm,versatile-cm-auxosc", of_versatile_cm_osc_setup);
