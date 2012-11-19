/*
 * Copyright (C) 2011 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/mfd/syscon.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

struct anatop_regulator {
	const char *name;
	u32 control_reg;
	struct regmap *anatop;
	int vol_bit_shift;
	int vol_bit_width;
	int min_bit_val;
	int min_voltage;
	int max_voltage;
	struct regulator_desc rdesc;
	struct regulator_init_data *initdata;
};

static int anatop_regmap_set_voltage_sel(struct regulator_dev *reg,
					unsigned selector)
{
	struct anatop_regulator *anatop_reg = rdev_get_drvdata(reg);
	u32 val, mask;

	if (!anatop_reg->control_reg)
		return -ENOTSUPP;

	val = anatop_reg->min_bit_val + selector;
	dev_dbg(&reg->dev, "%s: calculated val %d\n", __func__, val);
	mask = ((1 << anatop_reg->vol_bit_width) - 1) <<
		anatop_reg->vol_bit_shift;
	val <<= anatop_reg->vol_bit_shift;
	regmap_update_bits(anatop_reg->anatop, anatop_reg->control_reg,
				mask, val);

	return 0;
}

static int anatop_regmap_get_voltage_sel(struct regulator_dev *reg)
{
	struct anatop_regulator *anatop_reg = rdev_get_drvdata(reg);
	u32 val, mask;

	if (!anatop_reg->control_reg)
		return -ENOTSUPP;

	regmap_read(anatop_reg->anatop, anatop_reg->control_reg, &val);
	mask = ((1 << anatop_reg->vol_bit_width) - 1) <<
		anatop_reg->vol_bit_shift;
	val = (val & mask) >> anatop_reg->vol_bit_shift;

	return val - anatop_reg->min_bit_val;
}

static struct regulator_ops anatop_rops = {
	.set_voltage_sel = anatop_regmap_set_voltage_sel,
	.get_voltage_sel = anatop_regmap_get_voltage_sel,
	.list_voltage = regulator_list_voltage_linear,
	.map_voltage = regulator_map_voltage_linear,
};

static int anatop_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct device_node *anatop_np;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct anatop_regulator *sreg;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };
	int ret = 0;

	initdata = of_get_regulator_init_data(dev, np);
	sreg = devm_kzalloc(dev, sizeof(*sreg), GFP_KERNEL);
	if (!sreg)
		return -ENOMEM;
	sreg->initdata = initdata;
	sreg->name = kstrdup(of_get_property(np, "regulator-name", NULL),
			     GFP_KERNEL);
	rdesc = &sreg->rdesc;
	memset(rdesc, 0, sizeof(*rdesc));
	rdesc->name = sreg->name;
	rdesc->ops = &anatop_rops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->owner = THIS_MODULE;

	anatop_np = of_get_parent(np);
	if (!anatop_np)
		return -ENODEV;
	sreg->anatop = syscon_node_to_regmap(anatop_np);
	of_node_put(anatop_np);
	if (IS_ERR(sreg->anatop))
		return PTR_ERR(sreg->anatop);

	ret = of_property_read_u32(np, "anatop-reg-offset",
				   &sreg->control_reg);
	if (ret) {
		dev_err(dev, "no anatop-reg-offset property set\n");
		goto anatop_probe_end;
	}
	ret = of_property_read_u32(np, "anatop-vol-bit-width",
				   &sreg->vol_bit_width);
	if (ret) {
		dev_err(dev, "no anatop-vol-bit-width property set\n");
		goto anatop_probe_end;
	}
	ret = of_property_read_u32(np, "anatop-vol-bit-shift",
				   &sreg->vol_bit_shift);
	if (ret) {
		dev_err(dev, "no anatop-vol-bit-shift property set\n");
		goto anatop_probe_end;
	}
	ret = of_property_read_u32(np, "anatop-min-bit-val",
				   &sreg->min_bit_val);
	if (ret) {
		dev_err(dev, "no anatop-min-bit-val property set\n");
		goto anatop_probe_end;
	}
	ret = of_property_read_u32(np, "anatop-min-voltage",
				   &sreg->min_voltage);
	if (ret) {
		dev_err(dev, "no anatop-min-voltage property set\n");
		goto anatop_probe_end;
	}
	ret = of_property_read_u32(np, "anatop-max-voltage",
				   &sreg->max_voltage);
	if (ret) {
		dev_err(dev, "no anatop-max-voltage property set\n");
		goto anatop_probe_end;
	}

	rdesc->n_voltages = (sreg->max_voltage - sreg->min_voltage)
		/ 25000 + 1;
	rdesc->min_uV = sreg->min_voltage;
	rdesc->uV_step = 25000;

	config.dev = &pdev->dev;
	config.init_data = initdata;
	config.driver_data = sreg;
	config.of_node = pdev->dev.of_node;

	/* register regulator */
	rdev = regulator_register(rdesc, &config);
	if (IS_ERR(rdev)) {
		dev_err(dev, "failed to register %s\n",
			rdesc->name);
		ret = PTR_ERR(rdev);
		goto anatop_probe_end;
	}

	platform_set_drvdata(pdev, rdev);

anatop_probe_end:
	if (ret)
		kfree(sreg->name);

	return ret;
}

static int __devexit anatop_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);
	struct anatop_regulator *sreg = rdev_get_drvdata(rdev);
	const char *name = sreg->name;

	regulator_unregister(rdev);
	kfree(name);

	return 0;
}

static struct of_device_id __devinitdata of_anatop_regulator_match_tbl[] = {
	{ .compatible = "fsl,anatop-regulator", },
	{ /* end */ }
};

static struct platform_driver anatop_regulator_driver = {
	.driver = {
		.name	= "anatop_regulator",
		.owner  = THIS_MODULE,
		.of_match_table = of_anatop_regulator_match_tbl,
	},
	.probe	= anatop_regulator_probe,
	.remove	= anatop_regulator_remove,
};

static int __init anatop_regulator_init(void)
{
	return platform_driver_register(&anatop_regulator_driver);
}
postcore_initcall(anatop_regulator_init);

static void __exit anatop_regulator_exit(void)
{
	platform_driver_unregister(&anatop_regulator_driver);
}
module_exit(anatop_regulator_exit);

MODULE_AUTHOR("Nancy Chen <Nancy.Chen@freescale.com>, "
	      "Ying-Chun Liu (PaulLiu) <paul.liu@linaro.org>");
MODULE_DESCRIPTION("ANATOP Regulator driver");
MODULE_LICENSE("GPL v2");
