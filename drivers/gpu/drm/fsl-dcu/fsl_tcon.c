// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright 2015 Toradex AG
 *
 * Stefan Agner <stefan@agner.ch>
 *
 * Freescale TCON device driver
 */

#include <linux/clk.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "fsl_tcon.h"

void fsl_tcon_bypass_disable(struct fsl_tcon *tcon)
{
	regmap_update_bits(tcon->regs, FSL_TCON_CTRL1,
			   FSL_TCON_CTRL1_TCON_BYPASS, 0);
}

void fsl_tcon_bypass_enable(struct fsl_tcon *tcon)
{
	regmap_update_bits(tcon->regs, FSL_TCON_CTRL1,
			   FSL_TCON_CTRL1_TCON_BYPASS,
			   FSL_TCON_CTRL1_TCON_BYPASS);
}

static const struct regmap_config fsl_tcon_regmap_config = {
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

	if (of_address_to_resource(np, 0, &res))
		return -EINVAL;

	regs = devm_ioremap_resource(dev, &res);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	tcon->regs = devm_regmap_init_mmio(dev, regs,
					   &fsl_tcon_regmap_config);
	return PTR_ERR_OR_ZERO(tcon->regs);
}

struct fsl_tcon *fsl_tcon_init(struct device *dev)
{
	struct fsl_tcon *tcon;
	struct device_node *np;
	int ret;

	/* TCON node is not mandatory, some devices do not provide TCON */
	np = of_parse_phandle(dev->of_node, "fsl,tcon", 0);
	if (!np)
		return NULL;

	tcon = devm_kzalloc(dev, sizeof(*tcon), GFP_KERNEL);
	if (!tcon)
		goto err_node_put;

	ret = fsl_tcon_init_regmap(dev, tcon, np);
	if (ret) {
		dev_err(dev, "Couldn't create the TCON regmap\n");
		goto err_node_put;
	}

	tcon->ipg_clk = of_clk_get_by_name(np, "ipg");
	if (IS_ERR(tcon->ipg_clk)) {
		dev_err(dev, "Couldn't get the TCON bus clock\n");
		goto err_node_put;
	}

	ret = clk_prepare_enable(tcon->ipg_clk);
	if (ret) {
		dev_err(dev, "Couldn't enable the TCON clock\n");
		goto err_node_put;
	}

	of_node_put(np);
	dev_info(dev, "Using TCON in bypass mode\n");

	return tcon;

err_node_put:
	of_node_put(np);
	return NULL;
}

void fsl_tcon_free(struct fsl_tcon *tcon)
{
	clk_disable_unprepare(tcon->ipg_clk);
	clk_put(tcon->ipg_clk);
}

