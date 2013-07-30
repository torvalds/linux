/*
 * FPGA to/from HPS Bridge Driver for Altera SoCFPGA Devices
 *
 *  Copyright (C) 2013 Altera Corporation, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/kernel.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_platform.h>
#include <linux/of_address.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/err.h>
#include <linux/regmap.h>
#include "fpga-bridge.h"

#define SOCFPGA_RSTMGR_BRGMODRST		0x1c
#define ALT_RSTMGR_BRGMODRST_H2F_MSK		0x00000001
#define ALT_RSTMGR_BRGMODRST_LWH2F_MSK		0x00000002
#define ALT_RSTMGR_BRGMODRST_F2H_MSK		0x00000004

#define ALT_L3_REMAP_OFST			0x0
#define ALT_L3_REMAP_MPUZERO_MSK		0x00000001
#define ALT_L3_REMAP_H2F_MSK			0x00000008
#define ALT_L3_REMAP_LWH2F_MSK			0x00000010

static struct of_device_id altera_fpga_of_match[];

struct altera_hps2fpga_data {
	char	name[48];
	struct platform_device *pdev;
	struct device_node *np;
	struct regmap *rstreg;
	struct regmap *l3reg;
	int mask;
	int remap_mask;
};

static int alt_hps2fpga_enable_show(struct fpga_bridge *bridge)
{
	struct altera_hps2fpga_data *priv = bridge->priv;
	unsigned int value;

	regmap_read(priv->rstreg, SOCFPGA_RSTMGR_BRGMODRST, &value);

	return ((value & priv->mask) == 0);
}

static void alt_hps2fpga_enable_set(struct fpga_bridge *bridge, bool enable)
{
	struct altera_hps2fpga_data *priv = bridge->priv;
	int value;

	/* bring bridge out of reset */
	if (enable)
		value = 0;
	else
		value = priv->mask;

	regmap_update_bits(priv->rstreg, SOCFPGA_RSTMGR_BRGMODRST,
			   priv->mask, value);

	/* Allow bridge to be visible to L3 masters or not */
	if (priv->remap_mask) {
		value = ALT_L3_REMAP_MPUZERO_MSK;

		if (enable)
			value |= priv->remap_mask;

		regmap_write(priv->l3reg, ALT_L3_REMAP_OFST, value);
	}
}

struct fpga_bridge_ops altera_hps2fpga_br_ops = {
	.enable_set = alt_hps2fpga_enable_set,
	.enable_show = alt_hps2fpga_enable_show,
};

static struct altera_hps2fpga_data hps2fpga_data  = {
	.name = "hps2fpga",
	.mask = ALT_RSTMGR_BRGMODRST_H2F_MSK,
	.remap_mask = ALT_L3_REMAP_H2F_MSK,
};

static struct altera_hps2fpga_data lwhps2fpga_data  = {
	.name = "lshps2fpga",
	.mask = ALT_RSTMGR_BRGMODRST_LWH2F_MSK,
	.remap_mask = ALT_L3_REMAP_LWH2F_MSK,
};

static struct altera_hps2fpga_data fpga2hps_data  = {
	.name = "fpga2hps",
	.mask = ALT_RSTMGR_BRGMODRST_F2H_MSK,
};

static int alt_fpga_bridge_probe(struct platform_device *pdev)
{
	struct altera_hps2fpga_data *priv;
	const struct of_device_id *of_id;

	of_id = of_match_device(altera_fpga_of_match, &pdev->dev);
	priv = (struct altera_hps2fpga_data *)of_id->data;
	WARN_ON(!priv);

	priv->np = pdev->dev.of_node;
	priv->pdev = pdev;

	priv->rstreg = syscon_regmap_lookup_by_compatible("altr,rst-mgr");
	if (IS_ERR(priv->rstreg)) {
		dev_err(&priv->pdev->dev,
			"regmap for altr,rst-mgr lookup failed.\n");
		return PTR_ERR(priv->rstreg);
	}

	priv->l3reg = syscon_regmap_lookup_by_compatible("altr,l3regs");
	if (IS_ERR(priv->l3reg)) {
		dev_err(&priv->pdev->dev,
			"regmap for altr,l3regs lookup failed.\n");
		return PTR_ERR(priv->l3reg);
	}

	return register_fpga_bridge(pdev, &altera_hps2fpga_br_ops,
				    priv->name, priv);
}

static int alt_fpga_bridge_remove(struct platform_device *pdev)
{
	remove_fpga_bridge(pdev);
	return 0;
}

static struct of_device_id altera_fpga_of_match[] = {
	{ .compatible = "altr,socfpga-hps2fpga-bridge", .data = &hps2fpga_data },
	{ .compatible = "altr,socfpga-lwhps2fpga-bridge", .data = &lwhps2fpga_data },
	{ .compatible = "altr,socfpga-fpga2hps-bridge", .data = &fpga2hps_data },
	{},
};

MODULE_DEVICE_TABLE(of, altera_fpga_of_match);

static struct platform_driver altera_fpga_driver = {
	.remove = alt_fpga_bridge_remove,
	.driver = {
		.name	= "altera_hps2fpga_bridge",
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(altera_fpga_of_match),
	},
};

static int __init alt_fpga_bridge_init(void)
{
	return platform_driver_probe(&altera_fpga_driver,
				     alt_fpga_bridge_probe);
}

static void __exit alt_fpga_bridge_exit(void)
{
	platform_driver_unregister(&altera_fpga_driver);
}

module_init(alt_fpga_bridge_init);
module_exit(alt_fpga_bridge_exit);

MODULE_DESCRIPTION("Altera SoCFPGA HPS to FPGA Bridge");
MODULE_AUTHOR("Alan Tull <atull@altera.com>");
MODULE_LICENSE("GPL v2");
