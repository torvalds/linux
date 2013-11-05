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

#define FOUR_BIT_MASK 0xf
#define SIX_BIT_MASK 0x3f

static struct of_device_id altera_fpga_of_match[];

struct alt_fpga2sdram_data {
	char	name[48];
	struct platform_device *pdev;
	struct device_node *np;
	struct regmap *sdrctl;
	int mask;
};

static atomic_t instances;

static int alt_fpga2sdram_enable_show(struct fpga_bridge *bridge)
{
	struct alt_fpga2sdram_data *priv = bridge->priv;
	int value;

	regmap_read(priv->sdrctl, ALT_SDR_CTL_FPGAPORTRST_OFST, &value);

	return ((value & priv->mask) == priv->mask);
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

struct prop_map {
	char *prop_name;
	uint32_t *prop_value;
	uint32_t prop_max;
};
static int alt_fpga2sdram_get_mask(struct alt_fpga2sdram_data *priv)
{
	int i;
	uint32_t read, write, cmd;
	struct prop_map map[] = {
		{"read-ports-mask", &read, FOUR_BIT_MASK},
		{"write-ports-mask", &write, FOUR_BIT_MASK},
		{"cmd-ports-mask", &cmd, SIX_BIT_MASK},
	};
	for (i = 0; i < ARRAY_SIZE(map); i++) {
		if (of_property_read_u32(priv->np, map[i].prop_name,
			map[i].prop_value)) {
			dev_err(&priv->pdev->dev,
				"failed to find property, %s\n",
				map[i].prop_name);
			return -EINVAL;
		} else if (*map[i].prop_value > map[i].prop_max) {
			dev_err(&priv->pdev->dev,
				"%s value 0x%x > than max 0x%x\n",
				map[i].prop_name,
				*map[i].prop_value,
				map[i].prop_max);
			return -EINVAL;
		}
	}

	priv->mask =
		(read << ALT_SDR_CTL_FPGAPORTRST_RD_SHIFT) |
		(write << ALT_SDR_CTL_FPGAPORTRST_WR_SHIFT) |
		(cmd << ALT_SDR_CTL_FPGAPORTRST_CTRL_SHIFT);

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
	if (atomic_inc_return(&instances) > 1) {
		atomic_dec(&instances);
		dev_err(&pdev->dev,
			"already one instance of driver\n");
		return -ENODEV;
	}

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
		dev_err(&pdev->dev,
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
	atomic_dec(&instances);
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
	atomic_set(&instances, 0);
	return platform_driver_probe(&altera_fpga_driver,
				     alt_fpga_bridge_probe);
}

static void __exit alt_fpga_bridge_exit(void)
{
	platform_driver_unregister(&altera_fpga_driver);
}

arch_initcall(alt_fpga_bridge_init);
module_exit(alt_fpga_bridge_exit);

MODULE_DESCRIPTION("Altera SoCFPGA FPGA to SDRAM Bridge");
MODULE_AUTHOR("Alan Tull <atull@altera.com>");
MODULE_LICENSE("GPL v2");
