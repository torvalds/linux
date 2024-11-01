// SPDX-License-Identifier: GPL-2.0
/*
 * FPGA to/from HPS Bridge Driver for Altera SoCFPGA Devices
 *
 *  Copyright (C) 2013-2016 Altera Corporation, All Rights Reserved.
 *
 * Includes this patch from the mailing list:
 *   fpga: altera-hps2fpga: fix HPS2FPGA bridge visibility to L3 masters
 *   Signed-off-by: Anatolij Gustschin <agust@denx.de>
 */

/*
 * This driver manages bridges on a Altera SOCFPGA between the ARM host
 * processor system (HPS) and the embedded FPGA.
 *
 * This driver supports enabling and disabling of the configured ports, which
 * allows for safe reprogramming of the FPGA, assuming that the new FPGA image
 * uses the same port configuration.  Bridges must be disabled before
 * reprogramming the FPGA and re-enabled after the FPGA has been programmed.
 */

#include <linux/clk.h>
#include <linux/fpga/fpga-bridge.h>
#include <linux/kernel.h>
#include <linux/mfd/syscon.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/spinlock.h>

#define ALT_L3_REMAP_OFST			0x0
#define ALT_L3_REMAP_MPUZERO_MSK		0x00000001
#define ALT_L3_REMAP_H2F_MSK			0x00000008
#define ALT_L3_REMAP_LWH2F_MSK			0x00000010

#define HPS2FPGA_BRIDGE_NAME			"hps2fpga"
#define LWHPS2FPGA_BRIDGE_NAME			"lwhps2fpga"
#define FPGA2HPS_BRIDGE_NAME			"fpga2hps"

struct altera_hps2fpga_data {
	const char *name;
	struct reset_control *bridge_reset;
	struct regmap *l3reg;
	unsigned int remap_mask;
	struct clk *clk;
};

static int alt_hps2fpga_enable_show(struct fpga_bridge *bridge)
{
	struct altera_hps2fpga_data *priv = bridge->priv;

	return reset_control_status(priv->bridge_reset);
}

/* The L3 REMAP register is write only, so keep a cached value. */
static unsigned int l3_remap_shadow;
static DEFINE_SPINLOCK(l3_remap_lock);

static int _alt_hps2fpga_enable_set(struct altera_hps2fpga_data *priv,
				    bool enable)
{
	unsigned long flags;
	int ret;

	/* bring bridge out of reset */
	if (enable)
		ret = reset_control_deassert(priv->bridge_reset);
	else
		ret = reset_control_assert(priv->bridge_reset);
	if (ret)
		return ret;

	/* Allow bridge to be visible to L3 masters or not */
	if (priv->remap_mask) {
		spin_lock_irqsave(&l3_remap_lock, flags);
		l3_remap_shadow |= ALT_L3_REMAP_MPUZERO_MSK;

		if (enable)
			l3_remap_shadow |= priv->remap_mask;
		else
			l3_remap_shadow &= ~priv->remap_mask;

		ret = regmap_write(priv->l3reg, ALT_L3_REMAP_OFST,
				   l3_remap_shadow);
		spin_unlock_irqrestore(&l3_remap_lock, flags);
	}

	return ret;
}

static int alt_hps2fpga_enable_set(struct fpga_bridge *bridge, bool enable)
{
	return _alt_hps2fpga_enable_set(bridge->priv, enable);
}

static const struct fpga_bridge_ops altera_hps2fpga_br_ops = {
	.enable_set = alt_hps2fpga_enable_set,
	.enable_show = alt_hps2fpga_enable_show,
};

static struct altera_hps2fpga_data hps2fpga_data  = {
	.name = HPS2FPGA_BRIDGE_NAME,
	.remap_mask = ALT_L3_REMAP_H2F_MSK,
};

static struct altera_hps2fpga_data lwhps2fpga_data  = {
	.name = LWHPS2FPGA_BRIDGE_NAME,
	.remap_mask = ALT_L3_REMAP_LWH2F_MSK,
};

static struct altera_hps2fpga_data fpga2hps_data  = {
	.name = FPGA2HPS_BRIDGE_NAME,
};

static const struct of_device_id altera_fpga_of_match[] = {
	{ .compatible = "altr,socfpga-hps2fpga-bridge",
	  .data = &hps2fpga_data },
	{ .compatible = "altr,socfpga-lwhps2fpga-bridge",
	  .data = &lwhps2fpga_data },
	{ .compatible = "altr,socfpga-fpga2hps-bridge",
	  .data = &fpga2hps_data },
	{},
};

static int alt_fpga_bridge_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct altera_hps2fpga_data *priv;
	const struct of_device_id *of_id;
	struct fpga_bridge *br;
	u32 enable;
	int ret;

	of_id = of_match_device(altera_fpga_of_match, dev);
	if (!of_id) {
		dev_err(dev, "failed to match device\n");
		return -ENODEV;
	}

	priv = (struct altera_hps2fpga_data *)of_id->data;

	priv->bridge_reset = of_reset_control_get_exclusive_by_index(dev->of_node,
								     0);
	if (IS_ERR(priv->bridge_reset)) {
		dev_err(dev, "Could not get %s reset control\n", priv->name);
		return PTR_ERR(priv->bridge_reset);
	}

	if (priv->remap_mask) {
		priv->l3reg = syscon_regmap_lookup_by_compatible("altr,l3regs");
		if (IS_ERR(priv->l3reg)) {
			dev_err(dev, "regmap for altr,l3regs lookup failed\n");
			return PTR_ERR(priv->l3reg);
		}
	}

	priv->clk = devm_clk_get(dev, NULL);
	if (IS_ERR(priv->clk)) {
		dev_err(dev, "no clock specified\n");
		return PTR_ERR(priv->clk);
	}

	ret = clk_prepare_enable(priv->clk);
	if (ret) {
		dev_err(dev, "could not enable clock\n");
		return -EBUSY;
	}

	if (!of_property_read_u32(dev->of_node, "bridge-enable", &enable)) {
		if (enable > 1) {
			dev_warn(dev, "invalid bridge-enable %u > 1\n", enable);
		} else {
			dev_info(dev, "%s bridge\n",
				 (enable ? "enabling" : "disabling"));

			ret = _alt_hps2fpga_enable_set(priv, enable);
			if (ret)
				goto err;
		}
	}

	br = fpga_bridge_register(dev, priv->name,
				  &altera_hps2fpga_br_ops, priv);
	if (IS_ERR(br)) {
		ret = PTR_ERR(br);
		goto err;
	}

	platform_set_drvdata(pdev, br);

	return 0;

err:
	clk_disable_unprepare(priv->clk);

	return ret;
}

static int alt_fpga_bridge_remove(struct platform_device *pdev)
{
	struct fpga_bridge *bridge = platform_get_drvdata(pdev);
	struct altera_hps2fpga_data *priv = bridge->priv;

	fpga_bridge_unregister(bridge);

	clk_disable_unprepare(priv->clk);

	return 0;
}

MODULE_DEVICE_TABLE(of, altera_fpga_of_match);

static struct platform_driver alt_fpga_bridge_driver = {
	.probe = alt_fpga_bridge_probe,
	.remove = alt_fpga_bridge_remove,
	.driver = {
		.name	= "altera_hps2fpga_bridge",
		.of_match_table = of_match_ptr(altera_fpga_of_match),
	},
};

module_platform_driver(alt_fpga_bridge_driver);

MODULE_DESCRIPTION("Altera SoCFPGA HPS to FPGA Bridge");
MODULE_AUTHOR("Alan Tull <atull@opensource.altera.com>");
MODULE_LICENSE("GPL v2");
