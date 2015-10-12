/*
 * Driver for the ICST307 VCO clock found in the ARM Reference designs.
 * We wrap the custom interface from <asm/hardware/icst.h> into the generic
 * clock framework.
 *
 * Copyright (C) 2012 Linus Walleij
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
	init.flags = CLK_IS_ROOT;
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
