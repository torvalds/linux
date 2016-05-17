/*
 * Driver for the ICST307 VCO clock found in the ARM Reference designs.
 * We wrap the custom interface from <asm/hardware/icst.h> into the generic
 * clock framework.
 *
 * Copyright (C) 2012-2015 Linus Walleij
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * TODO: when all ARM reference designs are migrated to generic clocks, the
 * ICST clock code from the ARM tree should probably be merged into this
 * file.
 */
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>
#include <linux/regmap.h>
#include <linux/mfd/syscon.h>

#include "clk-icst.h"

/* Magic unlocking token used on all Versatile boards */
#define VERSATILE_LOCK_VAL	0xA05F

/**
 * struct clk_icst - ICST VCO clock wrapper
 * @hw: corresponding clock hardware entry
 * @vcoreg: VCO register address
 * @lockreg: VCO lock register address
 * @params: parameters for this ICST instance
 * @rate: current rate
 */
struct clk_icst {
	struct clk_hw hw;
	struct regmap *map;
	u32 vcoreg_off;
	u32 lockreg_off;
	struct icst_params *params;
	unsigned long rate;
};

#define to_icst(_hw) container_of(_hw, struct clk_icst, hw)

/**
 * vco_get() - get ICST VCO settings from a certain ICST
 * @icst: the ICST clock to get
 * @vco: the VCO struct to return the value in
 */
static int vco_get(struct clk_icst *icst, struct icst_vco *vco)
{
	u32 val;
	int ret;

	ret = regmap_read(icst->map, icst->vcoreg_off, &val);
	if (ret)
		return ret;
	vco->v = val & 0x1ff;
	vco->r = (val >> 9) & 0x7f;
	vco->s = (val >> 16) & 03;
	return 0;
}

/**
 * vco_set() - commit changes to an ICST VCO
 * @icst: the ICST clock to set
 * @vco: the VCO struct to set the changes from
 */
static int vco_set(struct clk_icst *icst, struct icst_vco vco)
{
	u32 val;
	int ret;

	ret = regmap_read(icst->map, icst->vcoreg_off, &val);
	if (ret)
		return ret;

	/* Mask the 18 bits used by the VCO */
	val &= ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	/* This magic unlocks the VCO so it can be controlled */
	ret = regmap_write(icst->map, icst->lockreg_off, VERSATILE_LOCK_VAL);
	if (ret)
		return ret;
	ret = regmap_write(icst->map, icst->vcoreg_off, val);
	if (ret)
		return ret;
	/* This locks the VCO again */
	ret = regmap_write(icst->map, icst->lockreg_off, 0);
	if (ret)
		return ret;
	return 0;
}

static unsigned long icst_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;
	int ret;

	if (parent_rate)
		icst->params->ref = parent_rate;
	ret = vco_get(icst, &vco);
	if (ret) {
		pr_err("ICST: could not get VCO setting\n");
		return 0;
	}
	icst->rate = icst_hz(icst->params, vco);
	return icst->rate;
}

static long icst_round_rate(struct clk_hw *hw, unsigned long rate,
			    unsigned long *prate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	vco = icst_hz_to_vco(icst->params, rate);
	return icst_hz(icst->params, vco);
}

static int icst_set_rate(struct clk_hw *hw, unsigned long rate,
			 unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	if (parent_rate)
		icst->params->ref = parent_rate;
	vco = icst_hz_to_vco(icst->params, rate);
	icst->rate = icst_hz(icst->params, vco);
	return vco_set(icst, vco);
}

static const struct clk_ops icst_ops = {
	.recalc_rate = icst_recalc_rate,
	.round_rate = icst_round_rate,
	.set_rate = icst_set_rate,
};

static struct clk *icst_clk_setup(struct device *dev,
				  const struct clk_icst_desc *desc,
				  const char *name,
				  const char *parent_name,
				  struct regmap *map)
{
	struct clk *clk;
	struct clk_icst *icst;
	struct clk_init_data init;
	struct icst_params *pclone;

	icst = kzalloc(sizeof(struct clk_icst), GFP_KERNEL);
	if (!icst) {
		pr_err("could not allocate ICST clock!\n");
		return ERR_PTR(-ENOMEM);
	}

	pclone = kmemdup(desc->params, sizeof(*pclone), GFP_KERNEL);
	if (!pclone) {
		kfree(icst);
		pr_err("could not clone ICST params\n");
		return ERR_PTR(-ENOMEM);
	}

	init.name = name;
	init.ops = &icst_ops;
	init.flags = 0;
	init.parent_names = (parent_name ? &parent_name : NULL);
	init.num_parents = (parent_name ? 1 : 0);
	icst->map = map;
	icst->hw.init = &init;
	icst->params = pclone;
	icst->vcoreg_off = desc->vco_offset;
	icst->lockreg_off = desc->lock_offset;

	clk = clk_register(dev, &icst->hw);
	if (IS_ERR(clk)) {
		kfree(pclone);
		kfree(icst);
	}

	return clk;
}

struct clk *icst_clk_register(struct device *dev,
			const struct clk_icst_desc *desc,
			const char *name,
			const char *parent_name,
			void __iomem *base)
{
	struct regmap_config icst_regmap_conf = {
		.reg_bits = 32,
		.val_bits = 32,
		.reg_stride = 4,
	};
	struct regmap *map;

	map = regmap_init_mmio(dev, base, &icst_regmap_conf);
	if (IS_ERR(map)) {
		pr_err("could not initialize ICST regmap\n");
		return ERR_CAST(map);
	}
	return icst_clk_setup(dev, desc, name, parent_name, map);
}
EXPORT_SYMBOL_GPL(icst_clk_register);

#ifdef CONFIG_OF
/*
 * In a device tree, an memory-mapped ICST clock appear as a child
 * of a syscon node. Assume this and probe it only as a child of a
 * syscon.
 */

static const struct icst_params icst525_params = {
	.vco_max	= ICST525_VCO_MAX_5V,
	.vco_min	= ICST525_VCO_MIN,
	.vd_min		= 8,
	.vd_max		= 263,
	.rd_min		= 3,
	.rd_max		= 65,
	.s2div		= icst525_s2div,
	.idx2s		= icst525_idx2s,
};

static const struct icst_params icst307_params = {
	.vco_max	= ICST307_VCO_MAX,
	.vco_min	= ICST307_VCO_MIN,
	.vd_min		= 4 + 8,
	.vd_max		= 511 + 8,
	.rd_min		= 1 + 2,
	.rd_max		= 127 + 2,
	.s2div		= icst307_s2div,
	.idx2s		= icst307_idx2s,
};

static void __init of_syscon_icst_setup(struct device_node *np)
{
	struct device_node *parent;
	struct regmap *map;
	struct clk_icst_desc icst_desc;
	const char *name = np->name;
	const char *parent_name;
	struct clk *regclk;

	/* We do not release this reference, we are using it perpetually */
	parent = of_get_parent(np);
	if (!parent) {
		pr_err("no parent node for syscon ICST clock\n");
		return;
	}
	map = syscon_node_to_regmap(parent);
	if (IS_ERR(map)) {
		pr_err("no regmap for syscon ICST clock parent\n");
		return;
	}

	if (of_property_read_u32(np, "vco-offset", &icst_desc.vco_offset)) {
		pr_err("no VCO register offset for ICST clock\n");
		return;
	}
	if (of_property_read_u32(np, "lock-offset", &icst_desc.lock_offset)) {
		pr_err("no lock register offset for ICST clock\n");
		return;
	}

	if (of_device_is_compatible(np, "arm,syscon-icst525"))
		icst_desc.params = &icst525_params;
	else if (of_device_is_compatible(np, "arm,syscon-icst307"))
		icst_desc.params = &icst307_params;
	else {
		pr_err("unknown ICST clock %s\n", name);
		return;
	}

	/* Parent clock name is not the same as node parent */
	parent_name = of_clk_get_parent_name(np, 0);

	regclk = icst_clk_setup(NULL, &icst_desc, name, parent_name, map);
	if (IS_ERR(regclk)) {
		pr_err("error setting up syscon ICST clock %s\n", name);
		return;
	}
	of_clk_add_provider(np, of_clk_src_simple_get, regclk);
	pr_debug("registered syscon ICST clock %s\n", name);
}

CLK_OF_DECLARE(arm_syscon_icst525_clk,
	       "arm,syscon-icst525", of_syscon_icst_setup);
CLK_OF_DECLARE(arm_syscon_icst307_clk,
	       "arm,syscon-icst307", of_syscon_icst_setup);

#endif
