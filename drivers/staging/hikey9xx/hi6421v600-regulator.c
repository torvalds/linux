// SPDX-License-Identifier: GPL-2.0
/*
 * Device driver for regulators in Hisi IC
 *
 * Copyright (c) 2013 Linaro Ltd.
 * Copyright (c) 2011 Hisilicon.
 *
 * Guodong Xu <guodong.xu@linaro.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#include <linux/slab.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/of_address.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/of_regulator.h>
#include <linux/mfd/hi6421-spmi-pmic.h>
#include <linux/delay.h>
#include <linux/time.h>
#include <linux/version.h>
#include <linux/seq_file.h>
#include <linux/uaccess.h>
#include <linux/spmi.h>

struct hi6421v600_regulator {
	struct regulator_desc rdesc;
	struct hisi_pmic *pmic;
	u8 eco_mode_mask;
	u32 eco_uA;
};

static DEFINE_MUTEX(enable_mutex);

/* helper function to ensure when it returns it is at least 'delay_us'
 * microseconds after 'since'.
 */

static int hisi_regulator_is_enabled(struct regulator_dev *rdev)
{
	u32 reg_val;
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;

	reg_val = hisi_pmic_read(pmic, rdev->desc->enable_reg);

	dev_dbg(&rdev->dev,
		"%s: enable_reg=0x%x, val= 0x%x, enable_state=%d\n",
		 __func__, rdev->desc->enable_reg,
		reg_val, (reg_val & rdev->desc->enable_mask));

	return ((reg_val & rdev->desc->enable_mask) != 0);
}

static int hisi_regulator_enable(struct regulator_dev *rdev)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;

	/* keep a distance of off_on_delay from last time disabled */
	usleep_range(rdev->desc->off_on_delay, rdev->desc->off_on_delay + 1000);

	dev_dbg(&rdev->dev, "%s: off_on_delay=%d us\n",
		__func__, rdev->desc->off_on_delay);

	/* cannot enable more than one regulator at one time */
	mutex_lock(&enable_mutex);
	usleep_range(HISI_REGS_ENA_PROTECT_TIME,
		     HISI_REGS_ENA_PROTECT_TIME + 1000);

	/* set enable register */
	hisi_pmic_rmw(pmic, rdev->desc->enable_reg,
		      rdev->desc->enable_mask,
				rdev->desc->enable_mask);
	dev_dbg(&rdev->dev, "%s: enable_reg=0x%x, enable_mask=0x%x\n",
		 __func__, rdev->desc->enable_reg,
		 rdev->desc->enable_mask);

	mutex_unlock(&enable_mutex);

	return 0;
}

static int hisi_regulator_disable(struct regulator_dev *rdev)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;

	/* set enable register to 0 */
	hisi_pmic_rmw(pmic, rdev->desc->enable_reg,
		      rdev->desc->enable_mask, 0);

	return 0;
}

static int hisi_regulator_get_voltage(struct regulator_dev *rdev)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;
	u32 reg_val, selector;
	int vol;

	/* get voltage selector */
	reg_val = hisi_pmic_read(pmic, rdev->desc->vsel_reg);
	selector = (reg_val & rdev->desc->vsel_mask) >>
				(ffs(rdev->desc->vsel_mask) - 1);

	vol = rdev->desc->ops->list_voltage(rdev, selector);

	dev_dbg(&rdev->dev,
		"%s: vsel_reg=0x%x, val=0x%x, entry=0x%x, voltage=%d mV\n",
		 __func__, rdev->desc->vsel_reg, reg_val, selector, vol/ 1000);

	return vol;
}

static int hisi_regulator_set_voltage(struct regulator_dev *rdev,
				      int min_uV, int max_uV, unsigned int *selector)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;
	u32 vsel;
	int uV, ret = 0;

	for (vsel = 0; vsel < rdev->desc->n_voltages; vsel++) {
		uV = rdev->desc->volt_table[vsel];
		dev_dbg(&rdev->dev,
			"%s: min %d, max %d, value[%u] = %d\n",
			__func__, min_uV, max_uV, vsel, uV);

		/* Break at the first in-range value */
		if (min_uV <= uV && uV <= max_uV)
			break;
	}

	/* unlikely to happen. sanity test done by regulator core */
	if (unlikely(vsel == rdev->desc->n_voltages))
		return -EINVAL;

	*selector = vsel;
	/* set voltage selector */
	hisi_pmic_rmw(pmic, rdev->desc->vsel_reg,
		      rdev->desc->vsel_mask,
		      vsel << (ffs(rdev->desc->vsel_mask) - 1));

	dev_dbg(&rdev->dev,
		"%s: vsel_reg=0x%x, vsel_mask=0x%x, value=0x%x, voltage=%d mV\n",
		 __func__,
		 rdev->desc->vsel_reg,
		 rdev->desc->vsel_mask,
		 vsel << (ffs(rdev->desc->vsel_mask) - 1), uV / 1000);

	return ret;
}

static unsigned int hisi_regulator_get_mode(struct regulator_dev *rdev)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;
	u32 reg_val;
	unsigned int mode;

	reg_val = hisi_pmic_read(pmic, rdev->desc->enable_reg);

	if (reg_val & sreg->eco_mode_mask)
		mode = REGULATOR_MODE_IDLE;
	else
		mode = REGULATOR_MODE_NORMAL;

	dev_dbg(&rdev->dev,
		"%s: enable_reg=0x%x, eco_mode_mask=0x%x, reg_val=0x%x, %s mode\n",
		 __func__, rdev->desc->enable_reg, sreg->eco_mode_mask, reg_val,
		 mode == REGULATOR_MODE_IDLE ? "idle" : "normal");

	return mode;
}

static int hisi_regulator_set_mode(struct regulator_dev *rdev,
				   unsigned int mode)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);
	struct hisi_pmic *pmic = sreg->pmic;
	u32 val;

	switch (mode) {
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	case REGULATOR_MODE_IDLE:
		val = sreg->eco_mode_mask << (ffs(sreg->eco_mode_mask) - 1);
		break;
	default:
		return -EINVAL;
	}

	/* set mode */
	hisi_pmic_rmw(pmic, rdev->desc->enable_reg,
		      sreg->eco_mode_mask, val);

	dev_dbg(&rdev->dev,
		"%s: enable_reg=0x%x, eco_mode_mask=0x%x, value=0x%x\n",
		 __func__, rdev->desc->enable_reg, sreg->eco_mode_mask, val);

	return 0;
}

static unsigned int hisi_regulator_get_optimum_mode(struct regulator_dev *rdev,
						    int input_uV, int output_uV,
						    int load_uA)
{
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);

	if (load_uA || ((unsigned int)load_uA > sreg->eco_uA)) {
		dev_dbg(&rdev->dev, "%s: normal mode", __func__);
		return REGULATOR_MODE_NORMAL;
	} else {
		dev_dbg(&rdev->dev, "%s: idle mode", __func__);
		return REGULATOR_MODE_IDLE;
	}
}

static int hisi_dt_parse(struct platform_device *pdev,
			 struct hi6421v600_regulator *sreg,
			 struct regulator_desc *rdesc)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	unsigned int register_info[3] = {0};
	unsigned int *v_table;
	int ret;

	/* parse .register_info.enable_reg */
	ret = of_property_read_u32_array(np, "hi6421-ctrl",
					 register_info, 3);
	if (ret) {
		dev_err(dev, "no hi6421-ctrl property set\n");
		return ret;
	}
	rdesc->enable_reg = register_info[0];
	rdesc->enable_mask = register_info[1];
	sreg->eco_mode_mask = register_info[2];

	/* parse .register_info.vsel_reg */
	ret = of_property_read_u32_array(np, "hi6421-vsel",
					 register_info, 2);
	if (ret) {
		dev_err(dev, "no hi6421-vsel property set\n");
		return ret;
	}
	rdesc->vsel_reg = register_info[0];
	rdesc->vsel_mask = register_info[1];

	/* parse .off-on-delay */
	ret = of_property_read_u32(np, "off-on-delay-us",
				   &rdesc->off_on_delay);
	if (ret) {
		dev_err(dev, "no off-on-delay-us property set\n");
		return ret;
	}

	/* parse .enable_time */
	ret = of_property_read_u32(np, "startup-delay-us",
				   &rdesc->enable_time);
	if (ret) {
		dev_err(dev, "no startup-delay-us property set\n");
		return ret;
	}

	/* parse .eco_uA */
	ret = of_property_read_u32(np, "eco-microamp",
				   &sreg->eco_uA);
	if (ret) {
		sreg->eco_uA = 0;
		ret = 0;
	}

	/* parse volt_table */

	rdesc->n_voltages = of_property_count_u32_elems(np, "voltage-table");

	v_table = devm_kzalloc(dev, sizeof(unsigned int) * rdesc->n_voltages,
			       GFP_KERNEL);
	if (unlikely(!v_table))
		return  -ENOMEM;
	rdesc->volt_table = v_table;

	ret = of_property_read_u32_array(np, "voltage-table",
					 v_table, rdesc->n_voltages);
	if (ret) {
		dev_err(dev, "no voltage-table property set\n");
		return ret;
	}

	return 0;
}

static struct regulator_ops hisi_ldo_rops = {
	.is_enabled = hisi_regulator_is_enabled,
	.enable = hisi_regulator_enable,
	.disable = hisi_regulator_disable,
	.list_voltage = regulator_list_voltage_table,
	.get_voltage = hisi_regulator_get_voltage,
	.set_voltage = hisi_regulator_set_voltage,
	.get_mode = hisi_regulator_get_mode,
	.set_mode = hisi_regulator_set_mode,
	.get_optimum_mode = hisi_regulator_get_optimum_mode,
};

/*
 * Used only for parsing the DT properties
 */

static int hisi_regulator_probe_ldo(struct platform_device *pdev,
				    struct device_node *np,
				    struct hisi_pmic *pmic)
{
	struct device *dev = &pdev->dev;
	struct regulator_desc *rdesc;
	struct regulator_dev *rdev;
	struct hi6421v600_regulator *sreg = NULL;
	struct regulator_init_data *initdata;
	struct regulator_config config = { };
	struct regulation_constraints *constraint;
	const char *supplyname = NULL;
	int ret = 0;

	initdata = of_get_regulator_init_data(dev, np, NULL);
	if (!initdata) {
		dev_err(dev, "failed to get regulator data\n");
		return -EINVAL;
	}

	sreg = kzalloc(sizeof(*sreg), GFP_KERNEL);
	if (!sreg)
		return -ENOMEM;

	sreg->pmic = pmic;
	rdesc = &sreg->rdesc;

	rdesc->name = initdata->constraints.name;
	rdesc->ops = &hisi_ldo_rops;
	rdesc->type = REGULATOR_VOLTAGE;
	rdesc->min_uV = initdata->constraints.min_uV;

	supplyname = of_get_property(np, "supply_name", NULL);
	if (supplyname)
		initdata->supply_regulator = supplyname;

	/* parse device tree data for regulator specific */
	ret = hisi_dt_parse(pdev, sreg, rdesc);
	if (ret)
		goto hisi_probe_end;

	/* hisi regulator supports two modes */
	constraint = &initdata->constraints;

	constraint->valid_modes_mask = REGULATOR_MODE_NORMAL;
	if (sreg->eco_mode_mask) {
		constraint->valid_modes_mask |= REGULATOR_MODE_IDLE;
		constraint->valid_ops_mask |= REGULATOR_CHANGE_MODE;
	}

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
		goto hisi_probe_end;
	}

	dev_dbg(dev, "valid_modes_mask: 0x%x, valid_ops_mask: 0x%x\n",
		 constraint->valid_modes_mask, constraint->valid_ops_mask);

	dev_set_drvdata(dev, rdev);
hisi_probe_end:
	if (ret)
		kfree(sreg);
	return ret;
}


static int hisi_regulator_probe(struct platform_device *pdev)
{
	struct device *pmic_dev = pdev->dev.parent;
	struct device_node *np = pmic_dev->of_node;
	struct device_node *regulators, *child;
	struct platform_device *new_pdev;
	struct hisi_pmic *pmic;
	int ret;

	dev_dbg(&pdev->dev, "probing hi6421v600 regulator\n");
	/*
	 * This driver is meant to be called by hi6421-spmi-core,
	 * which should first set drvdata. If this doesn't happen, hit
	 * a warn on and return.
	 */
	pmic = dev_get_drvdata(pmic_dev);
	if (WARN_ON(!pmic))
		return -ENODEV;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_err(&pdev->dev, "regulator node not found\n");
		return -ENODEV;
	}

	/*
	 * Parse all LDO regulator nodes
	 */
	for_each_child_of_node(regulators, child) {
		dev_dbg(&pdev->dev, "adding child %pOF\n", child);

		new_pdev = platform_device_alloc(child->name, -1);
		new_pdev->dev.parent = pmic_dev;
		new_pdev->dev.of_node = of_node_get(child);

		ret = platform_device_add(new_pdev);
		if (ret < 0) {
			platform_device_put(new_pdev);
			continue;
		}

		ret = hisi_regulator_probe_ldo(new_pdev, child, pmic);
		if (ret < 0)
			platform_device_put(new_pdev);
	}

	of_node_put(regulators);

	return 0;
}

static int hisi_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = dev_get_drvdata(&pdev->dev);
	struct hi6421v600_regulator *sreg = rdev_get_drvdata(rdev);

	regulator_unregister(rdev);

	/* TODO: should i worry about that? devm_kzalloc */
	if (rdev->desc->volt_table)
		devm_kfree(&pdev->dev, (unsigned int *)rdev->desc->volt_table);

	kfree(sreg);

	return 0;
}

static const struct platform_device_id hi6421v600_regulator_table[] = {
	{ .name = "hi6421v600-regulator" },
	{},
};
MODULE_DEVICE_TABLE(platform, hi6421v600_regulator_table);

static struct platform_driver hi6421v600_regulator_driver = {
	.id_table = hi6421v600_regulator_table,
	.driver = {
		.name	= "hi6421v600-regulator",
	},
	.probe	= hisi_regulator_probe,
	.remove	= hisi_regulator_remove,
};
module_platform_driver(hi6421v600_regulator_driver);

MODULE_DESCRIPTION("Hi6421v600 regulator driver");
MODULE_LICENSE("GPL v2");

