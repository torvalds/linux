/*
 * tps65217-regulator.c
 *
 * Regulator driver for TPS65217 PMIC
 *
 * Copyright (C) 2011 Texas Instruments Incorporated - http://www.ti.com/
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation version 2.
 *
 * This program is distributed "as is" WITHOUT ANY WARRANTY of any
 * kind, whether express or implied; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/platform_device.h>

#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/tps65217.h>

#define TPS65217_REGULATOR(_name, _id, _ops, _n)	\
	{						\
		.name		= _name,		\
		.id		= _id,			\
		.ops		= &_ops,		\
		.n_voltages	= _n,			\
		.type		= REGULATOR_VOLTAGE,	\
		.owner		= THIS_MODULE,		\
	}						\

#define TPS65217_INFO(_nm, _min, _max, _f1, _f2, _t, _n, _em, _vr, _vm)	\
	{						\
		.name		= _nm,			\
		.min_uV		= _min,			\
		.max_uV		= _max,			\
		.vsel_to_uv	= _f1,			\
		.uv_to_vsel	= _f2,			\
		.table		= _t,			\
		.table_len	= _n,			\
		.enable_mask	= _em,			\
		.set_vout_reg	= _vr,			\
		.set_vout_mask	= _vm,			\
	}

static const int LDO1_VSEL_table[] = {
	1000000, 1100000, 1200000, 1250000,
	1300000, 1350000, 1400000, 1500000,
	1600000, 1800000, 2500000, 2750000,
	2800000, 3000000, 3100000, 3300000,
};

static int tps65217_vsel_to_uv1(unsigned int vsel)
{
	int uV = 0;

	if (vsel > 63)
		return -EINVAL;

	if (vsel <= 24)
		uV = vsel * 25000 + 900000;
	else if (vsel <= 52)
		uV = (vsel - 24) * 50000 + 1500000;
	else if (vsel < 56)
		uV = (vsel - 52) * 100000 + 2900000;
	else
		uV = 3300000;

	return uV;
}

static int tps65217_uv_to_vsel1(int uV, unsigned int *vsel)
{
	if ((uV < 0) && (uV > 3300000))
		return -EINVAL;

	if (uV <= 1500000)
		*vsel = DIV_ROUND_UP(uV - 900000, 25000);
	else if (uV <= 2900000)
		*vsel = 24 + DIV_ROUND_UP(uV - 1500000, 50000);
	else if (uV < 3300000)
		*vsel = 52 + DIV_ROUND_UP(uV - 2900000, 100000);
	else
		*vsel = 56;

	return 0;
}

static int tps65217_vsel_to_uv2(unsigned int vsel)
{
	int uV = 0;

	if (vsel > 31)
		return -EINVAL;

	if (vsel <= 8)
		uV = vsel * 50000 + 1500000;
	else if (vsel <= 13)
		uV = (vsel - 8) * 100000 + 1900000;
	else
		uV = (vsel - 13) * 50000 + 2400000;

	return uV;
}

static int tps65217_uv_to_vsel2(int uV, unsigned int *vsel)
{
	if ((uV < 0) && (uV > 3300000))
		return -EINVAL;

	if (uV <= 1900000)
		*vsel = DIV_ROUND_UP(uV - 1500000, 50000);
	else if (uV <= 2400000)
		*vsel = 8 + DIV_ROUND_UP(uV - 1900000, 100000);
	else
		*vsel = 13 + DIV_ROUND_UP(uV - 2400000, 50000);

	return 0;
}

static struct tps_info tps65217_pmic_regs[] = {
	TPS65217_INFO("DCDC1", 900000, 1800000, tps65217_vsel_to_uv1,
			tps65217_uv_to_vsel1, NULL, 64, TPS65217_ENABLE_DC1_EN,
			TPS65217_REG_DEFDCDC1, TPS65217_DEFDCDCX_DCDC_MASK),
	TPS65217_INFO("DCDC2", 900000, 3300000, tps65217_vsel_to_uv1,
			tps65217_uv_to_vsel1, NULL, 64, TPS65217_ENABLE_DC2_EN,
			TPS65217_REG_DEFDCDC2, TPS65217_DEFDCDCX_DCDC_MASK),
	TPS65217_INFO("DCDC3", 900000, 1500000, tps65217_vsel_to_uv1,
			tps65217_uv_to_vsel1, NULL, 64, TPS65217_ENABLE_DC3_EN,
			TPS65217_REG_DEFDCDC3, TPS65217_DEFDCDCX_DCDC_MASK),
	TPS65217_INFO("LDO1", 1000000, 3300000, NULL, NULL, LDO1_VSEL_table,
			16, TPS65217_ENABLE_LDO1_EN, TPS65217_REG_DEFLDO1,
			TPS65217_DEFLDO1_LDO1_MASK),
	TPS65217_INFO("LDO2", 900000, 3300000, tps65217_vsel_to_uv1,
			tps65217_uv_to_vsel1, NULL, 64, TPS65217_ENABLE_LDO2_EN,
			TPS65217_REG_DEFLDO2, TPS65217_DEFLDO2_LDO2_MASK),
	TPS65217_INFO("LDO3", 1800000, 3300000, tps65217_vsel_to_uv2,
			tps65217_uv_to_vsel2, NULL, 32,
			TPS65217_ENABLE_LS1_EN | TPS65217_DEFLDO3_LDO3_EN,
			TPS65217_REG_DEFLS1, TPS65217_DEFLDO3_LDO3_MASK),
	TPS65217_INFO("LDO4", 1800000, 3300000, tps65217_vsel_to_uv2,
			tps65217_uv_to_vsel2, NULL, 32,
			TPS65217_ENABLE_LS2_EN | TPS65217_DEFLDO4_LDO4_EN,
			TPS65217_REG_DEFLS2, TPS65217_DEFLDO4_LDO4_MASK),
};

static int tps65217_pmic_is_enabled(struct regulator_dev *dev)
{
	int ret;
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int data, rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	ret = tps65217_reg_read(tps, TPS65217_REG_ENABLE, &data);
	if (ret)
		return ret;

	return (data & tps->info[rid]->enable_mask) ? 1 : 0;
}

static int tps65217_pmic_enable(struct regulator_dev *dev)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	/* Enable the regulator and password protection is level 1 */
	return tps65217_set_bits(tps, TPS65217_REG_ENABLE,
				tps->info[rid]->enable_mask,
				tps->info[rid]->enable_mask,
				TPS65217_PROTECT_L1);
}

static int tps65217_pmic_disable(struct regulator_dev *dev)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	/* Disable the regulator and password protection is level 1 */
	return tps65217_clear_bits(tps, TPS65217_REG_ENABLE,
			tps->info[rid]->enable_mask, TPS65217_PROTECT_L1);
}

static int tps65217_pmic_get_voltage_sel(struct regulator_dev *dev)
{
	int ret;
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int selector, rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	ret = tps65217_reg_read(tps, tps->info[rid]->set_vout_reg, &selector);
	if (ret)
		return ret;

	selector &= tps->info[rid]->set_vout_mask;

	return selector;
}

static int tps65217_pmic_set_voltage_sel(struct regulator_dev *dev,
					 unsigned selector)
{
	int ret;
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	/* Set the voltage based on vsel value and write protect level is 2 */
	ret = tps65217_set_bits(tps, tps->info[rid]->set_vout_reg,
				tps->info[rid]->set_vout_mask,
				selector, TPS65217_PROTECT_L2);

	/* Set GO bit for DCDCx to initiate voltage transistion */
	switch (rid) {
	case TPS65217_DCDC_1 ... TPS65217_DCDC_3:
		ret = tps65217_set_bits(tps, TPS65217_REG_DEFSLEW,
				       TPS65217_DEFSLEW_GO, TPS65217_DEFSLEW_GO,
				       TPS65217_PROTECT_L2);
		break;
	}

	return ret;
}

static int tps65217_pmic_map_voltage(struct regulator_dev *dev,
				     int min_uV, int max_uV)
{

	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int sel, rid = rdev_get_id(dev);
	int ret;

	/* LDO1 uses regulator_map_voltage_iterate() */
	if (rid == TPS65217_LDO_1)
		return -EINVAL;

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	if (min_uV < tps->info[rid]->min_uV || min_uV > tps->info[rid]->max_uV)
		return -EINVAL;

	if (max_uV < tps->info[rid]->min_uV || max_uV > tps->info[rid]->max_uV)
		return -EINVAL;

	ret = tps->info[rid]->uv_to_vsel(min_uV, &sel);
	if (ret)
		return ret;

	return sel;
}

static int tps65217_pmic_list_voltage(struct regulator_dev *dev,
					unsigned selector)
{
	struct tps65217 *tps = rdev_get_drvdata(dev);
	unsigned int rid = rdev_get_id(dev);

	if (rid < TPS65217_DCDC_1 || rid > TPS65217_LDO_4)
		return -EINVAL;

	if (selector >= tps->info[rid]->table_len)
		return -EINVAL;

	if (tps->info[rid]->table)
		return tps->info[rid]->table[selector];

	return tps->info[rid]->vsel_to_uv(selector);
}

/* Operations permitted on DCDCx, LDO2, LDO3 and LDO4 */
static struct regulator_ops tps65217_pmic_ops = {
	.is_enabled		= tps65217_pmic_is_enabled,
	.enable			= tps65217_pmic_enable,
	.disable		= tps65217_pmic_disable,
	.get_voltage_sel	= tps65217_pmic_get_voltage_sel,
	.set_voltage_sel	= tps65217_pmic_set_voltage_sel,
	.list_voltage		= tps65217_pmic_list_voltage,
	.map_voltage		= tps65217_pmic_map_voltage,
};

/* Operations permitted on LDO1 */
static struct regulator_ops tps65217_pmic_ldo1_ops = {
	.is_enabled		= tps65217_pmic_is_enabled,
	.enable			= tps65217_pmic_enable,
	.disable		= tps65217_pmic_disable,
	.get_voltage_sel	= tps65217_pmic_get_voltage_sel,
	.set_voltage_sel	= tps65217_pmic_set_voltage_sel,
	.list_voltage		= tps65217_pmic_list_voltage,
};

static const struct regulator_desc regulators[] = {
	TPS65217_REGULATOR("DCDC1", TPS65217_DCDC_1, tps65217_pmic_ops, 64),
	TPS65217_REGULATOR("DCDC2", TPS65217_DCDC_2, tps65217_pmic_ops, 64),
	TPS65217_REGULATOR("DCDC3", TPS65217_DCDC_3, tps65217_pmic_ops, 64),
	TPS65217_REGULATOR("LDO1", TPS65217_LDO_1, tps65217_pmic_ldo1_ops, 16),
	TPS65217_REGULATOR("LDO2", TPS65217_LDO_2, tps65217_pmic_ops, 64),
	TPS65217_REGULATOR("LDO3", TPS65217_LDO_3, tps65217_pmic_ops, 32),
	TPS65217_REGULATOR("LDO4", TPS65217_LDO_4, tps65217_pmic_ops, 32),
};

static int __devinit tps65217_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct tps65217 *tps;
	struct tps_info *info = &tps65217_pmic_regs[pdev->id];
	struct regulator_config config = { };

	/* Already set by core driver */
	tps = dev_to_tps65217(pdev->dev.parent);
	tps->info[pdev->id] = info;

	config.dev = &pdev->dev;
	config.init_data = pdev->dev.platform_data;
	config.driver_data = tps;

	rdev = regulator_register(&regulators[pdev->id], &config);
	if (IS_ERR(rdev))
		return PTR_ERR(rdev);

	platform_set_drvdata(pdev, rdev);

	return 0;
}

static int __devexit tps65217_regulator_remove(struct platform_device *pdev)
{
	struct regulator_dev *rdev = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	regulator_unregister(rdev);

	return 0;
}

static struct platform_driver tps65217_regulator_driver = {
	.driver = {
		.name = "tps65217-pmic",
	},
	.probe = tps65217_regulator_probe,
	.remove = __devexit_p(tps65217_regulator_remove),
};

static int __init tps65217_regulator_init(void)
{
	return platform_driver_register(&tps65217_regulator_driver);
}
subsys_initcall(tps65217_regulator_init);

static void __exit tps65217_regulator_exit(void)
{
	platform_driver_unregister(&tps65217_regulator_driver);
}
module_exit(tps65217_regulator_exit);

MODULE_AUTHOR("AnilKumar Ch <anilkumar@ti.com>");
MODULE_DESCRIPTION("TPS65217 voltage regulator driver");
MODULE_ALIAS("platform:tps65217-pmic");
MODULE_LICENSE("GPL v2");
