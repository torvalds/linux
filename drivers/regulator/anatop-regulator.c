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
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/mfd/anatop.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

struct anatop_regulator {
	const char *name;
	u32 control_reg;
	struct anatop *mfd;
	int vol_bit_shift;
	int vol_bit_width;
	int min_bit_val;
	int min_voltage;
	int max_voltage;
	struct regulator_desc rdesc;
	struct regulator_init_data *initdata;
};

static int anatop_set_voltage(struct regulator_dev *reg, int min_uV,
				  int max_uV, unsigned *selector)
{
	struct anatop_regulator *anatop_reg = rdev_get_drvdata(reg);
	u32 val, sel;
	int uv;

	uv = min_uV;
	dev_dbg(&reg->dev, "%s: uv %d, min %d, max %d\n", __func__,
		uv, anatop_reg->min_voltage,
		anatop_reg->max_voltage);

	if (uv < anatop_reg->min_voltage) {
		if (max_uV > anatop_reg->min_voltage)
			uv = anatop_reg->min_voltage;
		else
			return -EINVAL;
	}

	if (!anatop_reg->control_reg)
		return -ENOTSUPP;

	sel = DIV_ROUND_UP(uv - anatop_reg->min_voltage, 25000);
	if (sel * 25000 + anatop_reg->min_voltage > anatop_reg->max_voltage)
		return -EINVAL;
	val = anatop_reg->min_bit_val + sel;
	*selector = sel;
	dev_dbg(&reg->dev, "%s: calculated val %d\n", __func__, val);
	anatop_set_bits(anatop_reg->mfd,
			anatop_reg->control_reg,
			anatop_reg->vol_bit_shift,
			anatop_reg->vol_bit_width,
			val);

	return 0;
}

static int anatop_get_voltage_sel(struct regulator_dev *reg)
{
	struct anatop_regulator *anatop_reg = rdev_get_drvdata(reg);
	u32 val;

	if (!anatop_reg->control_reg)
		return -ENOTSUPP;

	val = anatop_get_bits(anatop_reg->mfd,
			      anatop_reg->control_reg,
			      anatop_reg->vol_bit_shift,
			      anatop_reg->vol_bit_width);

	return val - anatop_reg->min_bit_val;
}

static int anatop_list_voltage(struct regulator_dev *reg, unsigned selector)
{
	struct anatop_regulator *anatop_reg = rdev_get_drvdata(reg);
	int uv;

	uv = anatop_reg->min_voltage + selector * 25000;
	dev_dbg(&reg->dev, "vddio = %d, selector = %u\n", uv, selector);

	return uv;
}

static struct regulator_ops anatop_rops = {
	.set_voltage     = anatop_set_voltage,
	.get_voltage_sel = anatop_get_voltage_sel,
	.list_voltage    = anatop_list_voltage,
};

static int __devinit anatop_regulator_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct anatop_regulator *sreg;
	struct regulator_init_data *initdata;
	struct anatop *anatopmfd = dev_get_drvdata(pdev->dev.parent);
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
	sreg->mfd = anatopmfd;
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

	/* register regulator */
	rdev = regulator_register(rdesc, dev,
				  initdata, sreg, pdev->dev.of_node);
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

static struct platform_driver anatop_regulator = {
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
	return platform_driver_register(&anatop_regulator);
}
postcore_initcall(anatop_regulator_init);

static void __exit anatop_regulator_exit(void)
{
	platform_driver_unregister(&anatop_regulator);
}
module_exit(anatop_regulator_exit);

MODULE_AUTHOR("Nancy Chen <Nancy.Chen@freescale.com>, "
	      "Ying-Chun Liu (PaulLiu) <paul.liu@linaro.org>");
MODULE_DESCRIPTION("ANATOP Regulator driver");
MODULE_LICENSE("GPL v2");
