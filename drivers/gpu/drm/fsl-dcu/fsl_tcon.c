/*
 * Copyright 2015 Toradex AG
 *
 * Stefan Agner <stefan@agner.ch>
 *
 * Freescale TCON device driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "fsl_tcon.h"

void fsl_tcon_disable(struct fsl_tcon *tcon)
{
	regmap_update_bits(tcon->regs, FSL_TCON_CTRL1,
			   FSL_TCON_CTRL1_TCON_BYPASS, 0);

	tcon->enabled = false;
}

void fsl_tcon_enable(struct fsl_tcon *tcon)
{
	regmap_update_bits(tcon->regs, FSL_TCON_CTRL1,
			   FSL_TCON_CTRL1_TCON_BYPASS,
			   FSL_TCON_CTRL1_TCON_BYPASS);

	tcon->enabled = true;
}

void fsl_tcon_suspend(struct fsl_tcon *tcon)
{
	regmap_update_bits(tcon->regs, FSL_TCON_CTRL1,
			   FSL_TCON_CTRL1_TCON_BYPASS, 0);

	clk_disable_unprepare(tcon->ipg_clk);
}

void fsl_tcon_resume(struct fsl_tcon *tcon)
{
	clk_prepare_enable(tcon->ipg_clk);

	if (!tcon->enabled)
		return;

	regmap_update_bits(tcon->regs, FSL_TCON_CTRL1,
			   FSL_TCON_CTRL1_TCON_BYPASS,
			   FSL_TCON_CTRL1_TCON_BYPASS);
}

static struct regmap_config fsl_tcon_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,

	.name = "tcon",
};

static int fsl_tcon_init_regmap(struct device *dev,
				struct fsl_tcon *tcon,
				struct device_node *np)
{
	struct resource res;
	void __iomem *regs;
	int ret;

	ret = of_address_to_resource(np, 0, &res);
	regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(regs)) {
		dev_err(dev, "Couldn't map the TCON registers\n");
		return PTR_ERR(regs);
	}

	tcon->regs = devm_regmap_init_mmio(dev, regs,
					   &fsl_tcon_regmap_config);
	if (IS_ERR(tcon->regs)) {
		dev_err(dev, "Couldn't create the TCON regmap\n");
		return PTR_ERR(tcon->regs);
	}

	return 0;
}

struct fsl_tcon *fsl_tcon_init(struct device *dev)
{
	struct fsl_tcon *tcon;
	struct device_node *np;
	int ret;

	tcon = devm_kzalloc(dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		return NULL;

	np = of_parse_phandle(dev->of_node, "fsl,tcon", 0);
	if (!np) {
		dev_warn(dev, "Couldn't find the tcon node\n");
		return NULL;
	}

	ret = fsl_tcon_init_regmap(dev, tcon, np);
	if (ret)
		goto err_node_put;

	tcon->ipg_clk = of_clk_get_by_name(np, "ipg");
	if (IS_ERR(tcon->ipg_clk)) {
		dev_err(dev, "Couldn't get the TCON bus clock\n");
		goto err_free_regmap;
	}

	clk_prepare_enable(tcon->ipg_clk);

	return tcon;

err_free_regmap:
err_node_put:
	of_node_put(np);
	return NULL;
}

void fsl_tcon_free(struct fsl_tcon *tcon)
{
	clk_disable_unprepare(tcon->ipg_clk);
	clk_put(tcon->ipg_clk);
}

