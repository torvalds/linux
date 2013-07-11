/*
 * FPGA to sdram Bridge Driver for Altera SoCFPGA Devices
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
#include <linux/slab.h>
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

#define ALT_SDR_CTL_FPGAPORTRST_OFST		0x80
#define ALT_SDR_CTL_FPGAPORTRST_PORTRSTN_MSK	0x00003fff
#define ALT_SDR_CTL_FPGAPORTRST_RD_SHIFT	0
#define ALT_SDR_CTL_FPGAPORTRST_WR_SHIFT	4
#define ALT_SDR_CTL_FPGAPORTRST_CTRL_SHIFT	8

static struct of_device_id altera_fpga_of_match[];

struct alt_fpga2sdram_data {
	char	name[48];
	struct platform_device *pdev;
	struct device_node *np;
	struct regmap *sdrctl;
	int mask;
};

static int alt_fpga2sdram_enable_show(struct fpga_bridge *bridge)
{
	struct alt_fpga2sdram_data *priv = bridge->priv;
	int value;

	regmap_read(priv->sdrctl, ALT_SDR_CTL_FPGAPORTRST_OFST, &value);

	return ((value & priv->mask) != 0);
}

static void alt_fpga2sdram_enable_set(struct fpga_bridge *bridge, bool enable)
{
	struct alt_fpga2sdram_data *priv = bridge->priv;
	int value;

	if (enable)
		value = priv->mask;
	else
		value = 0;

	regmap_update_bits(priv->sdrctl, ALT_SDR_CTL_FPGAPORTRST_OFST,
			   priv->mask, value);
}

static int alt_fpga2sdram_get_mask(struct alt_fpga2sdram_data *priv)
{
	struct device_node *np = priv->np;
	int mask, ctrl_shift, ctrl_mask;
	u32 read, write, control[2];

	if (of_property_read_u32(np, "read-port", &read)) {
		dev_err(&priv->pdev->dev,
			"read-port property missing\n");
		return -EINVAL;
	}
	if ((read < 0) || (read > 3)) {
		dev_err(&priv->pdev->dev,
			"read-port property out of bounds\n");
		return -EINVAL;
	}

	if (of_property_read_u32(np, "write-port", &write)) {
		dev_err(&priv->pdev->dev,
			"write-port property invalid or missing\n");
		return -EINVAL;
	}
	if ((write < 0) || (write > 3)) {
		dev_err(&priv->pdev->dev,
			"write-port property out of bounds\n");
		return -EINVAL;
	}

	/* There can be 1 or 2 control ports specified */
	if (of_property_read_u32_array(np, "control-ports", control,
				       ARRAY_SIZE(control))) {
		dev_err(&priv->pdev->dev,
			"control-ports property missing\n");
		return -EINVAL;
	}
	if ((control[0] < 0) || (control[0] > 5) ||
	    (control[1] < 1) || (control[1] > 2)) {
		dev_err(&priv->pdev->dev,
			"control-ports property out of bounds\n");
		return -EINVAL;
	}

	ctrl_shift = ALT_SDR_CTL_FPGAPORTRST_CTRL_SHIFT + control[0];

	if (control[1] == 1)
		ctrl_mask = 0x1;
	else
		ctrl_mask = 0x3;

	mask = (1 << (ALT_SDR_CTL_FPGAPORTRST_RD_SHIFT + read)) |
		(1 << (ALT_SDR_CTL_FPGAPORTRST_WR_SHIFT + write)) |
		(ctrl_mask << ctrl_shift);

	WARN_ON((mask & ALT_SDR_CTL_FPGAPORTRST_PORTRSTN_MSK) != mask);
	priv->mask = mask;

	return 0;
}

struct fpga_bridge_ops altera_fpga2sdram_br_ops = {
	.enable_set = alt_fpga2sdram_enable_set,
	.enable_show = alt_fpga2sdram_enable_show,
};

static struct alt_fpga2sdram_data fpga2sdram_data  = {
	.name = "fpga2sdram",
};

static int alt_fpga_bridge_probe(struct platform_device *pdev)
{
	struct alt_fpga2sdram_data *priv;
	struct alt_fpga2sdram_data *data;
	const struct of_device_id *of_id = of_match_device(altera_fpga_of_match,
						     &pdev->dev);
	int ret = 0;

	data = (struct alt_fpga2sdram_data *)of_id->data;
	WARN_ON(!data);

	priv = devm_kzalloc(&pdev->dev, sizeof(struct alt_fpga2sdram_data),
		       GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	priv->np = pdev->dev.of_node;
	priv->pdev = pdev;
	priv->mask = data->mask;
	strncpy(priv->name, data->name, ARRAY_SIZE(priv->name));

	priv->sdrctl = syscon_regmap_lookup_by_compatible("altr,sdr-ctl");
	if (IS_ERR(priv->sdrctl)) {
		devm_kfree(&pdev->dev, priv);
		dev_err(&priv->pdev->dev,
			"regmap for altr,sdr-ctl lookup failed.\n");
		return PTR_ERR(priv->sdrctl);
	}

	ret = alt_fpga2sdram_get_mask(priv);
	if (ret) {
		devm_kfree(&pdev->dev, priv);
		return ret;
	}

	return register_fpga_bridge(pdev, &altera_fpga2sdram_br_ops,
				    priv->name, priv);
}

static int alt_fpga_bridge_remove(struct platform_device *pdev)
{
	remove_fpga_bridge(pdev);
	return 0;
}

static struct of_device_id altera_fpga_of_match[] = {
	{ .compatible = "altr,socfpga-fpga2sdram-bridge", .data = &fpga2sdram_data },
	{},
};

MODULE_DEVICE_TABLE(of, altera_fpga_of_match);

static struct platform_driver altera_fpga_driver = {
	.remove = alt_fpga_bridge_remove,
	.driver = {
		.name	= "altera_fpga2sdram_bridge",
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

MODULE_DESCRIPTION("Altera SoCFPGA FPGA to SDRAM Bridge");
MODULE_AUTHOR("Alan Tull <atull@altera.com>");
MODULE_LICENSE("GPL v2");
