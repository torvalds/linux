/*
* da9052-regulator.c: Regulator driver for DA9052
*
* Copyright(c) 2011 Dialog Semiconductor Ltd.
*
* Author: David Dajun Chen <dchen@diasemi.com>
*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>

#include <linux/mfd/da9052/da9052.h>
#include <linux/mfd/da9052/reg.h>
#include <linux/mfd/da9052/pdata.h>

/* Buck step size */
#define DA9052_BUCK_PERI_3uV_STEP		100000
#define DA9052_BUCK_PERI_REG_MAP_UPTO_3uV	24
#define DA9052_CONST_3uV			3000000

#define DA9052_MIN_UA		0
#define DA9052_MAX_UA		3
#define DA9052_CURRENT_RANGE	4

/* Bit masks */
#define DA9052_BUCK_ILIM_MASK_EVEN	0x0c
#define DA9052_BUCK_ILIM_MASK_ODD	0xc0

/* DA9052 REGULATOR IDs */
#define DA9052_ID_BUCK1		0
#define DA9052_ID_BUCK2		1
#define DA9052_ID_BUCK3		2
#define DA9052_ID_BUCK4		3
#define DA9052_ID_LDO1		4
#define DA9052_ID_LDO2		5
#define DA9052_ID_LDO3		6
#define DA9052_ID_LDO4		7
#define DA9052_ID_LDO5		8
#define DA9052_ID_LDO6		9
#define DA9052_ID_LDO7		10
#define DA9052_ID_LDO8		11
#define DA9052_ID_LDO9		12
#define DA9052_ID_LDO10		13

static const u32 da9052_current_limits[3][4] = {
	{700000, 800000, 1000000, 1200000},	/* DA9052-BC BUCKs */
	{1600000, 2000000, 2400000, 3000000},	/* DA9053-AA/Bx BUCK-CORE */
	{800000, 1000000, 1200000, 1500000},	/* DA9053-AA/Bx BUCK-PRO,
						 * BUCK-MEM and BUCK-PERI
						*/
};

struct da9052_regulator_info {
	struct regulator_desc reg_desc;
	int step_uV;
	int min_uV;
	int max_uV;
	unsigned char activate_bit;
};

struct da9052_regulator {
	struct da9052 *da9052;
	struct da9052_regulator_info *info;
	struct regulator_dev *rdev;
};

static int verify_range(struct da9052_regulator_info *info,
			 int min_uV, int max_uV)
{
	if (min_uV > info->max_uV || max_uV < info->min_uV)
		return -EINVAL;

	return 0;
}

static int da9052_dcdc_get_current_limit(struct regulator_dev *rdev)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	int offset = rdev_get_id(rdev);
	int ret, row = 2;

	ret = da9052_reg_read(regulator->da9052, DA9052_BUCKA_REG + offset/2);
	if (ret < 0)
		return ret;

	/* Determine the even or odd position of the buck current limit
	 * register field
	*/
	if (offset % 2 == 0)
		ret = (ret & DA9052_BUCK_ILIM_MASK_EVEN) >> 2;
	else
		ret = (ret & DA9052_BUCK_ILIM_MASK_ODD) >> 6;

	/* Select the appropriate current limit range */
	if (regulator->da9052->chip_id == DA9052)
		row = 0;
	else if (offset == 0)
		row = 1;

	return da9052_current_limits[row][ret];
}

static int da9052_dcdc_set_current_limit(struct regulator_dev *rdev, int min_uA,
					  int max_uA)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	int offset = rdev_get_id(rdev);
	int reg_val = 0;
	int i, row = 2;

	/* Select the appropriate current limit range */
	if (regulator->da9052->chip_id == DA9052)
		row = 0;
	else if (offset == 0)
		row = 1;

	for (i = DA9052_CURRENT_RANGE - 1; i >= 0; i--) {
		if ((min_uA <= da9052_current_limits[row][i]) &&
		    (da9052_current_limits[row][i] <= max_uA)) {
			reg_val = i;
			break;
		}
	}

	if (i < 0)
		return -EINVAL;

	/* Determine the even or odd position of the buck current limit
	 * register field
	*/
	if (offset % 2 == 0)
		return da9052_reg_update(regulator->da9052,
					 DA9052_BUCKA_REG + offset/2,
					 DA9052_BUCK_ILIM_MASK_EVEN,
					 reg_val << 2);
	else
		return da9052_reg_update(regulator->da9052,
					 DA9052_BUCKA_REG + offset/2,
					 DA9052_BUCK_ILIM_MASK_ODD,
					 reg_val << 6);
}

static int da9052_list_voltage(struct regulator_dev *rdev,
				unsigned int selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int id = rdev_get_id(rdev);
	int volt_uV;

	if ((id == DA9052_ID_BUCK4) && (regulator->da9052->chip_id == DA9052)
		&& (selector >= DA9052_BUCK_PERI_REG_MAP_UPTO_3uV)) {
		volt_uV = ((DA9052_BUCK_PERI_REG_MAP_UPTO_3uV * info->step_uV)
			  + info->min_uV);
		volt_uV += (selector - DA9052_BUCK_PERI_REG_MAP_UPTO_3uV)
				    * (DA9052_BUCK_PERI_3uV_STEP);
	} else {
		volt_uV = (selector * info->step_uV) + info->min_uV;
	}

	if (volt_uV > info->max_uV)
		return -EINVAL;

	return volt_uV;
}

static int da9052_map_voltage(struct regulator_dev *rdev,
			      int min_uV, int max_uV)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int id = rdev_get_id(rdev);
	int ret, sel;

	ret = verify_range(info, min_uV, max_uV);
	if (ret < 0)
		return ret;

	if (min_uV < info->min_uV)
		min_uV = info->min_uV;

	if ((id == DA9052_ID_BUCK4) && (regulator->da9052->chip_id == DA9052)
		&& (min_uV >= DA9052_CONST_3uV)) {
			sel = DA9052_BUCK_PERI_REG_MAP_UPTO_3uV +
			      DIV_ROUND_UP(min_uV - DA9052_CONST_3uV,
					   DA9052_BUCK_PERI_3uV_STEP);
	} else {
		sel = DIV_ROUND_UP(min_uV - info->min_uV, info->step_uV);
	}

	ret = da9052_list_voltage(rdev, sel);
	if (ret < 0)
		return ret;

	return sel;
}

static int da9052_regulator_set_voltage_sel(struct regulator_dev *rdev,
					    unsigned int selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int id = rdev_get_id(rdev);
	int ret;

	ret = da9052_reg_update(regulator->da9052, rdev->desc->vsel_reg,
				rdev->desc->vsel_mask, selector);
	if (ret < 0)
		return ret;

	/* Some LDOs and DCDCs are DVC controlled which requires enabling of
	 * the activate bit to implment the changes on the output.
	 */
	switch (id) {
	case DA9052_ID_BUCK1:
	case DA9052_ID_BUCK2:
	case DA9052_ID_BUCK3:
	case DA9052_ID_LDO2:
	case DA9052_ID_LDO3:
		ret = da9052_reg_update(regulator->da9052, DA9052_SUPPLY_REG,
					info->activate_bit, info->activate_bit);
		break;
	}

	return ret;
}

static int da9052_regulator_set_voltage_time_sel(struct regulator_dev *rdev,
						 unsigned int old_sel,
						 unsigned int new_sel)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int id = rdev_get_id(rdev);
	int ret = 0;

	/* The DVC controlled LDOs and DCDCs ramp with 6.25mV/µs after enabling
	 * the activate bit.
	 */
	switch (id) {
	case DA9052_ID_BUCK1:
	case DA9052_ID_BUCK2:
	case DA9052_ID_BUCK3:
	case DA9052_ID_LDO2:
	case DA9052_ID_LDO3:
		ret = (new_sel - old_sel) * info->step_uV / 6250;
		break;
	}

	return ret;
}

static const struct regulator_ops da9052_dcdc_ops = {
	.get_current_limit = da9052_dcdc_get_current_limit,
	.set_current_limit = da9052_dcdc_set_current_limit,

	.list_voltage = da9052_list_voltage,
	.map_voltage = da9052_map_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = da9052_regulator_set_voltage_sel,
	.set_voltage_time_sel = da9052_regulator_set_voltage_time_sel,
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

static const struct regulator_ops da9052_ldo_ops = {
	.list_voltage = da9052_list_voltage,
	.map_voltage = da9052_map_voltage,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.set_voltage_sel = da9052_regulator_set_voltage_sel,
	.set_voltage_time_sel = da9052_regulator_set_voltage_time_sel,
	.is_enabled = regulator_is_enabled_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
};

#define DA9052_LDO(_id, _name, step, min, max, sbits, ebits, abits) \
{\
	.reg_desc = {\
		.name = #_name,\
		.of_match = of_match_ptr(#_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.ops = &da9052_ldo_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = DA9052_ID_##_id,\
		.n_voltages = (max - min) / step + 1, \
		.owner = THIS_MODULE,\
		.vsel_reg = DA9052_BUCKCORE_REG + DA9052_ID_##_id, \
		.vsel_mask = (1 << (sbits)) - 1,\
		.enable_reg = DA9052_BUCKCORE_REG + DA9052_ID_##_id, \
		.enable_mask = 1 << (ebits),\
	},\
	.min_uV = (min) * 1000,\
	.max_uV = (max) * 1000,\
	.step_uV = (step) * 1000,\
	.activate_bit = (abits),\
}

#define DA9052_DCDC(_id, _name, step, min, max, sbits, ebits, abits) \
{\
	.reg_desc = {\
		.name = #_name,\
		.of_match = of_match_ptr(#_name),\
		.regulators_node = of_match_ptr("regulators"),\
		.ops = &da9052_dcdc_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = DA9052_ID_##_id,\
		.n_voltages = (max - min) / step + 1, \
		.owner = THIS_MODULE,\
		.vsel_reg = DA9052_BUCKCORE_REG + DA9052_ID_##_id, \
		.vsel_mask = (1 << (sbits)) - 1,\
		.enable_reg = DA9052_BUCKCORE_REG + DA9052_ID_##_id, \
		.enable_mask = 1 << (ebits),\
	},\
	.min_uV = (min) * 1000,\
	.max_uV = (max) * 1000,\
	.step_uV = (step) * 1000,\
	.activate_bit = (abits),\
}

static struct da9052_regulator_info da9052_regulator_info[] = {
	DA9052_DCDC(BUCK1, buck1, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBCOREGO),
	DA9052_DCDC(BUCK2, buck2, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBPROGO),
	DA9052_DCDC(BUCK3, buck3, 25, 950, 2525, 6, 6, DA9052_SUPPLY_VBMEMGO),
	DA9052_DCDC(BUCK4, buck4, 50, 1800, 3600, 5, 6, 0),
	DA9052_LDO(LDO1, ldo1, 50, 600, 1800, 5, 6, 0),
	DA9052_LDO(LDO2, ldo2, 25, 600, 1800, 6, 6, DA9052_SUPPLY_VLDO2GO),
	DA9052_LDO(LDO3, ldo3, 25, 1725, 3300, 6, 6, DA9052_SUPPLY_VLDO3GO),
	DA9052_LDO(LDO4, ldo4, 25, 1725, 3300, 6, 6, 0),
	DA9052_LDO(LDO5, ldo5, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO6, ldo6, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO7, ldo7, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO8, ldo8, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO9, ldo9, 50, 1250, 3650, 6, 6, 0),
	DA9052_LDO(LDO10, ldo10, 50, 1200, 3600, 6, 6, 0),
};

static struct da9052_regulator_info da9053_regulator_info[] = {
	DA9052_DCDC(BUCK1, buck1, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBCOREGO),
	DA9052_DCDC(BUCK2, buck2, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBPROGO),
	DA9052_DCDC(BUCK3, buck3, 25, 950, 2525, 6, 6, DA9052_SUPPLY_VBMEMGO),
	DA9052_DCDC(BUCK4, buck4, 25, 950, 2525, 6, 6, 0),
	DA9052_LDO(LDO1, ldo1, 50, 600, 1800, 5, 6, 0),
	DA9052_LDO(LDO2, ldo2, 25, 600, 1800, 6, 6, DA9052_SUPPLY_VLDO2GO),
	DA9052_LDO(LDO3, ldo3, 25, 1725, 3300, 6, 6, DA9052_SUPPLY_VLDO3GO),
	DA9052_LDO(LDO4, ldo4, 25, 1725, 3300, 6, 6, 0),
	DA9052_LDO(LDO5, ldo5, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO6, ldo6, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO7, ldo7, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO8, ldo8, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(LDO9, ldo9, 50, 1250, 3650, 6, 6, 0),
	DA9052_LDO(LDO10, ldo10, 50, 1200, 3600, 6, 6, 0),
};

static inline struct da9052_regulator_info *find_regulator_info(u8 chip_id,
								 int id)
{
	struct da9052_regulator_info *info;
	int i;

	switch (chip_id) {
	case DA9052:
		for (i = 0; i < ARRAY_SIZE(da9052_regulator_info); i++) {
			info = &da9052_regulator_info[i];
			if (info->reg_desc.id == id)
				return info;
		}
		break;
	case DA9053_AA:
	case DA9053_BA:
	case DA9053_BB:
	case DA9053_BC:
		for (i = 0; i < ARRAY_SIZE(da9053_regulator_info); i++) {
			info = &da9053_regulator_info[i];
			if (info->reg_desc.id == id)
				return info;
		}
		break;
	}

	return NULL;
}

static int da9052_regulator_probe(struct platform_device *pdev)
{
	const struct mfd_cell *cell = mfd_get_cell(pdev);
	struct regulator_config config = { };
	struct da9052_regulator *regulator;
	struct da9052 *da9052;
	struct da9052_pdata *pdata;

	regulator = devm_kzalloc(&pdev->dev, sizeof(struct da9052_regulator),
				 GFP_KERNEL);
	if (!regulator)
		return -ENOMEM;

	da9052 = dev_get_drvdata(pdev->dev.parent);
	pdata = dev_get_platdata(da9052->dev);
	regulator->da9052 = da9052;

	regulator->info = find_regulator_info(regulator->da9052->chip_id,
					      cell->id);
	if (regulator->info == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		return -EINVAL;
	}

	config.dev = da9052->dev;
	config.driver_data = regulator;
	config.regmap = da9052->regmap;
	if (pdata)
		config.init_data = pdata->regulators[cell->id];

	regulator->rdev = devm_regulator_register(&pdev->dev,
						  &regulator->info->reg_desc,
						  &config);
	if (IS_ERR(regulator->rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			regulator->info->reg_desc.name);
		return PTR_ERR(regulator->rdev);
	}

	platform_set_drvdata(pdev, regulator);

	return 0;
}

static struct platform_driver da9052_regulator_driver = {
	.probe = da9052_regulator_probe,
	.driver = {
		.name = "da9052-regulator",
	},
};

static int __init da9052_regulator_init(void)
{
	return platform_driver_register(&da9052_regulator_driver);
}
subsys_initcall(da9052_regulator_init);

static void __exit da9052_regulator_exit(void)
{
	platform_driver_unregister(&da9052_regulator_driver);
}
module_exit(da9052_regulator_exit);

MODULE_AUTHOR("David Dajun Chen <dchen@diasemi.com>");
MODULE_DESCRIPTION("Power Regulator driver for Dialog DA9052 PMIC");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:da9052-regulator");
