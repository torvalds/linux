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
#include <linux/clk.h>
#include <linux/clkdev.h>
#include <linux/err.h>
#include <linux/clk-provider.h>
#include <linux/io.h>

#include "clk-icst.h"

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
	void __iomem *vcoreg;
	void __iomem *lockreg;
	struct icst_params *params;
	unsigned long rate;
};

#define to_icst(_hw) container_of(_hw, struct clk_icst, hw)

/**
 * vco_get() - get ICST VCO settings from a certain register
 * @vcoreg: register containing the VCO settings
 */
static struct icst_vco vco_get(void __iomem *vcoreg)
{
	u32 val;
	struct icst_vco vco;

	val = readl(vcoreg);
	vco.v = val & 0x1ff;
	vco.r = (val >> 9) & 0x7f;
	vco.s = (val >> 16) & 03;
	return vco;
}

/**
 * vco_set() - commit changes to an ICST VCO
 * @locreg: register to poke to unlock the VCO for writing
 * @vcoreg: register containing the VCO settings
 * @vco: ICST VCO parameters to commit
 */
static void vco_set(void __iomem *lockreg,
			void __iomem *vcoreg,
			struct icst_vco vco)
{
	u32 val;

	val = readl(vcoreg) & ~0x7ffff;
	val |= vco.v | (vco.r << 9) | (vco.s << 16);

	/* This magic unlocks the VCO so it can be controlled */
	writel(0xa05f, lockreg);
	writel(val, vcoreg);
	/* This locks the VCO again */
	writel(0, lockreg);
}


static unsigned long icst_recalc_rate(struct clk_hw *hw,
				      unsigned long parent_rate)
{
	struct clk_icst *icst = to_icst(hw);
	struct icst_vco vco;

	if (parent_rate)
		icst->params->ref = parent_rate;
	vco = vco_get(icst->vcoreg);
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
	vco_set(icst->lockreg, icst->vcoreg, vco);
	return 0;
}

static const struct clk_ops icst_ops = {
	.recalc_rate = icst_recalc_rate,
	.round_rate = icst_round_rate,
	.set_rate = icst_set_rate,
};

struct clk *icst_clk_register(struct device *dev,
			const struct clk_icst_desc *desc,
			const char *name,
			const char *parent_name,
			void __iomem *base)
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
	icst->hw.init = &init;
	icst->params = pclone;
	icst->vcoreg = base + desc->vco_offset;
	icst->lockreg = base + desc->lock_offset;

	clk = clk_register(dev, &icst->hw);
	if (IS_ERR(clk))
		kfree(icst);

	return clk;
}
EXPORT_SYMBOL_GPL(icst_clk_register);
