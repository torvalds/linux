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
	unsigned char volt_shift;
	unsigned char en_bit;
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

static int da9052_regulator_enable(struct regulator_dev *rdev)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);

	return da9052_reg_update(regulator->da9052,
				 DA9052_BUCKCORE_REG + offset,
				 1 << info->en_bit, 1 << info->en_bit);
}

static int da9052_regulator_disable(struct regulator_dev *rdev)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);

	return da9052_reg_update(regulator->da9052,
				 DA9052_BUCKCORE_REG + offset,
				 1 << info->en_bit, 0);
}

static int da9052_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);
	int ret;

	ret = da9052_reg_read(regulator->da9052, DA9052_BUCKCORE_REG + offset);
	if (ret < 0)
		return ret;

	return ret & (1 << info->en_bit);
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

	if (min_uA > da9052_current_limits[row][DA9052_MAX_UA] ||
	    max_uA < da9052_current_limits[row][DA9052_MIN_UA])
		return -EINVAL;

	for (i = 0; i < DA9052_CURRENT_RANGE; i++) {
		if (min_uA <= da9052_current_limits[row][i]) {
			reg_val = i;
			break;
		}
	}

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

static int da9052_list_buckperi_voltage(struct regulator_dev *rdev,
					 unsigned int selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int volt_uV;

	if ((regulator->da9052->chip_id == DA9052) &&
	    (selector >= DA9052_BUCK_PERI_REG_MAP_UPTO_3uV)) {
		volt_uV = ((DA9052_BUCK_PERI_REG_MAP_UPTO_3uV * info->step_uV)
			    + info->min_uV);
		volt_uV += (selector - DA9052_BUCK_PERI_REG_MAP_UPTO_3uV)
			    * (DA9052_BUCK_PERI_3uV_STEP);
	} else
			volt_uV = (selector * info->step_uV) + info->min_uV;

	if (volt_uV > info->max_uV)
		return -EINVAL;

	return volt_uV;
}

static int da9052_list_voltage(struct regulator_dev *rdev,
				unsigned int selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int volt_uV;

	volt_uV = info->min_uV + info->step_uV * selector;

	if (volt_uV > info->max_uV)
		return -EINVAL;

	return volt_uV;
}

static int da9052_regulator_set_voltage_int(struct regulator_dev *rdev,
					     int min_uV, int max_uV,
					     unsigned int *selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);
	int ret;

	ret = verify_range(info, min_uV, max_uV);
	if (ret < 0)
		return ret;

	if (min_uV < info->min_uV)
		min_uV = info->min_uV;

	*selector = (min_uV - info->min_uV) / info->step_uV;

	ret = da9052_list_voltage(rdev, *selector);
	if (ret < 0)
		return ret;

	return da9052_reg_update(regulator->da9052,
				 DA9052_BUCKCORE_REG + offset,
				 (1 << info->volt_shift) - 1, *selector);
}

static int da9052_set_ldo_voltage(struct regulator_dev *rdev,
				   int min_uV, int max_uV,
				   unsigned int *selector)
{
	return da9052_regulator_set_voltage_int(rdev, min_uV, max_uV, selector);
}

static int da9052_set_ldo5_6_voltage(struct regulator_dev *rdev,
				      int min_uV, int max_uV,
				      unsigned int *selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int ret;

	ret = da9052_regulator_set_voltage_int(rdev, min_uV, max_uV, selector);
	if (ret < 0)
		return ret;

	/* Some LDOs are DVC controlled which requires enabling of
	 * the LDO activate bit to implment the changes on the
	 * LDO output.
	*/
	return da9052_reg_update(regulator->da9052, DA9052_SUPPLY_REG,
				 info->activate_bit, info->activate_bit);
}

static int da9052_set_dcdc_voltage(struct regulator_dev *rdev,
				    int min_uV, int max_uV,
				    unsigned int *selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int ret;

	ret = da9052_regulator_set_voltage_int(rdev, min_uV, max_uV, selector);
	if (ret < 0)
		return ret;

	/* Some DCDCs are DVC controlled which requires enabling of
	 * the DCDC activate bit to implment the changes on the
	 * DCDC output.
	*/
	return da9052_reg_update(regulator->da9052, DA9052_SUPPLY_REG,
				 info->activate_bit, info->activate_bit);
}

static int da9052_get_regulator_voltage_sel(struct regulator_dev *rdev)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);
	int ret;

	ret = da9052_reg_read(regulator->da9052, DA9052_BUCKCORE_REG + offset);
	if (ret < 0)
		return ret;

	ret &= ((1 << info->volt_shift) - 1);

	return ret;
}

static int da9052_set_buckperi_voltage(struct regulator_dev *rdev, int min_uV,
					int max_uV, unsigned int *selector)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);
	int ret;

	ret = verify_range(info, min_uV, max_uV);
	if (ret < 0)
		return ret;

	if (min_uV < info->min_uV)
		min_uV = info->min_uV;

	if ((regulator->da9052->chip_id == DA9052) &&
	    (min_uV >= DA9052_CONST_3uV))
		*selector = DA9052_BUCK_PERI_REG_MAP_UPTO_3uV +
			    ((min_uV - DA9052_CONST_3uV) /
			    (DA9052_BUCK_PERI_3uV_STEP));
	else
		*selector = (min_uV - info->min_uV) / info->step_uV;

	ret = da9052_list_buckperi_voltage(rdev, *selector);
	if (ret < 0)
		return ret;

	return da9052_reg_update(regulator->da9052,
				 DA9052_BUCKCORE_REG + offset,
				 (1 << info->volt_shift) - 1, *selector);
}

static int da9052_get_buckperi_voltage_sel(struct regulator_dev *rdev)
{
	struct da9052_regulator *regulator = rdev_get_drvdata(rdev);
	struct da9052_regulator_info *info = regulator->info;
	int offset = rdev_get_id(rdev);
	int ret;

	ret = da9052_reg_read(regulator->da9052, DA9052_BUCKCORE_REG + offset);
	if (ret < 0)
		return ret;

	ret &= ((1 << info->volt_shift) - 1);

	return ret;
}

static struct regulator_ops da9052_buckperi_ops = {
	.list_voltage = da9052_list_buckperi_voltage,
	.get_voltage_sel = da9052_get_buckperi_voltage_sel,
	.set_voltage = da9052_set_buckperi_voltage,

	.get_current_limit = da9052_dcdc_get_current_limit,
	.set_current_limit = da9052_dcdc_set_current_limit,

	.is_enabled = da9052_regulator_is_enabled,
	.enable = da9052_regulator_enable,
	.disable = da9052_regulator_disable,
};

static struct regulator_ops da9052_dcdc_ops = {
	.set_voltage = da9052_set_dcdc_voltage,
	.get_current_limit = da9052_dcdc_get_current_limit,
	.set_current_limit = da9052_dcdc_set_current_limit,

	.list_voltage = da9052_list_voltage,
	.get_voltage_sel = da9052_get_regulator_voltage_sel,
	.is_enabled = da9052_regulator_is_enabled,
	.enable = da9052_regulator_enable,
	.disable = da9052_regulator_disable,
};

static struct regulator_ops da9052_ldo5_6_ops = {
	.set_voltage = da9052_set_ldo5_6_voltage,

	.list_voltage = da9052_list_voltage,
	.get_voltage_sel = da9052_get_regulator_voltage_sel,
	.is_enabled = da9052_regulator_is_enabled,
	.enable = da9052_regulator_enable,
	.disable = da9052_regulator_disable,
};

static struct regulator_ops da9052_ldo_ops = {
	.set_voltage = da9052_set_ldo_voltage,

	.list_voltage = da9052_list_voltage,
	.get_voltage_sel = da9052_get_regulator_voltage_sel,
	.is_enabled = da9052_regulator_is_enabled,
	.enable = da9052_regulator_enable,
	.disable = da9052_regulator_disable,
};

#define DA9052_LDO5_6(_id, step, min, max, sbits, ebits, abits) \
{\
	.reg_desc = {\
		.name = "LDO" #_id,\
		.ops = &da9052_ldo5_6_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = _id,\
		.owner = THIS_MODULE,\
	},\
	.min_uV = (min) * 1000,\
	.max_uV = (max) * 1000,\
	.step_uV = (step) * 1000,\
	.volt_shift = (sbits),\
	.en_bit = (ebits),\
	.activate_bit = (abits),\
}

#define DA9052_LDO(_id, step, min, max, sbits, ebits, abits) \
{\
	.reg_desc = {\
		.name = "LDO" #_id,\
		.ops = &da9052_ldo_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = _id,\
		.owner = THIS_MODULE,\
	},\
	.min_uV = (min) * 1000,\
	.max_uV = (max) * 1000,\
	.step_uV = (step) * 1000,\
	.volt_shift = (sbits),\
	.en_bit = (ebits),\
	.activate_bit = (abits),\
}

#define DA9052_DCDC(_id, step, min, max, sbits, ebits, abits) \
{\
	.reg_desc = {\
		.name = "BUCK" #_id,\
		.ops = &da9052_dcdc_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = _id,\
		.owner = THIS_MODULE,\
	},\
	.min_uV = (min) * 1000,\
	.max_uV = (max) * 1000,\
	.step_uV = (step) * 1000,\
	.volt_shift = (sbits),\
	.en_bit = (ebits),\
	.activate_bit = (abits),\
}

#define DA9052_BUCKPERI(_id, step, min, max, sbits, ebits, abits) \
{\
	.reg_desc = {\
		.name = "BUCK" #_id,\
		.ops = &da9052_buckperi_ops,\
		.type = REGULATOR_VOLTAGE,\
		.id = _id,\
		.owner = THIS_MODULE,\
	},\
	.min_uV = (min) * 1000,\
	.max_uV = (max) * 1000,\
	.step_uV = (step) * 1000,\
	.volt_shift = (sbits),\
	.en_bit = (ebits),\
	.activate_bit = (abits),\
}

static struct da9052_regulator_info da9052_regulator_info[] = {
	/* Buck1 - 4 */
	DA9052_DCDC(0, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBCOREGO),
	DA9052_DCDC(1, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBPROGO),
	DA9052_DCDC(2, 25, 925, 2500, 6, 6, DA9052_SUPPLY_VBMEMGO),
	DA9052_BUCKPERI(3, 50, 1800, 3600, 5, 6, 0),
	/* LD01 - LDO10 */
	DA9052_LDO(4, 50, 600, 1800, 5, 6, 0),
	DA9052_LDO5_6(5, 25, 600, 1800, 6, 6, DA9052_SUPPLY_VLDO2GO),
	DA9052_LDO5_6(6, 25, 1725, 3300, 6, 6, DA9052_SUPPLY_VLDO3GO),
	DA9052_LDO(7, 25, 1725, 3300, 6, 6, 0),
	DA9052_LDO(8, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(9, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(10, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(11, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(12, 50, 1250, 3650, 6, 6, 0),
	DA9052_LDO(13, 50, 1200, 3600, 6, 6, 0),
};

static struct da9052_regulator_info da9053_regulator_info[] = {
	/* Buck1 - 4 */
	DA9052_DCDC(0, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBCOREGO),
	DA9052_DCDC(1, 25, 500, 2075, 6, 6, DA9052_SUPPLY_VBPROGO),
	DA9052_DCDC(2, 25, 925, 2500, 6, 6, DA9052_SUPPLY_VBMEMGO),
	DA9052_BUCKPERI(3, 25, 925, 2500, 6, 6, 0),
	/* LD01 - LDO10 */
	DA9052_LDO(4, 50, 600, 1800, 5, 6, 0),
	DA9052_LDO5_6(5, 25, 600, 1800, 6, 6, DA9052_SUPPLY_VLDO2GO),
	DA9052_LDO5_6(6, 25, 1725, 3300, 6, 6, DA9052_SUPPLY_VLDO3GO),
	DA9052_LDO(7, 25, 1725, 3300, 6, 6, 0),
	DA9052_LDO(8, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(9, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(10, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(11, 50, 1200, 3600, 6, 6, 0),
	DA9052_LDO(12, 50, 1250, 3650, 6, 6, 0),
	DA9052_LDO(13, 50, 1200, 3600, 6, 6, 0),
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
		for (i = 0; i < ARRAY_SIZE(da9053_regulator_info); i++) {
			info = &da9053_regulator_info[i];
			if (info->reg_desc.id == id)
				return info;
		}
		break;
	}

	return NULL;
}

static int __devinit da9052_regulator_probe(struct platform_device *pdev)
{
	struct da9052_regulator *regulator;
	struct da9052 *da9052;
	struct da9052_pdata *pdata;
	int ret;

	regulator = devm_kzalloc(&pdev->dev, sizeof(struct da9052_regulator),
				 GFP_KERNEL);
	if (!regulator)
		return -ENOMEM;

	da9052 = dev_get_drvdata(pdev->dev.parent);
	pdata = da9052->dev->platform_data;
	regulator->da9052 = da9052;

	regulator->info = find_regulator_info(regulator->da9052->chip_id,
					      pdev->id);
	if (regulator->info == NULL) {
		dev_err(&pdev->dev, "invalid regulator ID specified\n");
		ret = -EINVAL;
		goto err;
	}
	regulator->rdev = regulator_register(&regulator->info->reg_desc,
					     &pdev->dev,
					     pdata->regulators[pdev->id],
					     regulator, NULL);
	if (IS_ERR(regulator->rdev)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			regulator->info->reg_desc.name);
		ret = PTR_ERR(regulator->rdev);
		goto err;
	}

	platform_set_drvdata(pdev, regulator);

	return 0;
err:
	devm_kfree(&pdev->dev, regulator);
	return ret;
}

static int __devexit da9052_regulator_remove(struct platform_device *pdev)
{
	struct da9052_regulator *regulator = platform_get_drvdata(pdev);

	regulator_unregister(regulator->rdev);
	devm_kfree(&pdev->dev, regulator);

	return 0;
}

static struct platform_driver da9052_regulator_driver = {
	.probe = da9052_regulator_probe,
	.remove = __devexit_p(da9052_regulator_remove),
	.driver = {
		.name = "da9052-regulator",
		.owner = THIS_MODULE,
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
