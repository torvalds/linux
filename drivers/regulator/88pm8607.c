/*
 * Regulators driver for Marvell 88PM8607
 *
 * Copyright (C) 2009 Marvell International Ltd.
 * 	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/88pm860x.h>
#include <linux/module.h>

struct pm8607_regulator_info {
	struct regulator_desc	desc;
	struct pm860x_chip	*chip;
	struct regulator_dev	*regulator;
	struct i2c_client	*i2c;

	unsigned int	*vol_table;
	unsigned int	*vol_suspend;

	int	vol_reg;
	int	vol_shift;
	int	vol_nbits;
	int	update_reg;
	int	update_bit;
	int	enable_reg;
	int	enable_bit;
	int	slope_double;
};

static const unsigned int BUCK1_table[] = {
	 725000,  750000,  775000,  800000,  825000,  850000,  875000,  900000,
	 925000,  950000,  975000, 1000000, 1025000, 1050000, 1075000, 1100000,
	1125000, 1150000, 1175000, 1200000, 1225000, 1250000, 1275000, 1300000,
	1325000, 1350000, 1375000, 1400000, 1425000, 1450000, 1475000, 1500000,
	      0,   25000,   50000,   75000,  100000,  125000,  150000,  175000,
	 200000,  225000,  250000,  275000,  300000,  325000,  350000,  375000,
	 400000,  425000,  450000,  475000,  500000,  525000,  550000,  575000,
	 600000,  625000,  650000,  675000,  700000,  725000,  750000,  775000,
};

static const unsigned int BUCK1_suspend_table[] = {
	      0,   25000,   50000,   75000,  100000,  125000,  150000,  175000,
	 200000,  225000,  250000,  275000,  300000,  325000,  350000,  375000,
	 400000,  425000,  450000,  475000,  500000,  525000,  550000,  575000,
	 600000,  625000,  650000,  675000,  700000,  725000,  750000,  775000,
	 800000,  825000,  850000,  875000,  900000,  925000,  950000,  975000,
	1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000,
	1400000, 1425000, 1450000, 1475000, 1500000, 1500000, 1500000, 1500000,
};

static const unsigned int BUCK2_table[] = {
	      0,   50000,  100000,  150000,  200000,  250000,  300000,  350000,
	 400000,  450000,  500000,  550000,  600000,  650000,  700000,  750000,
	 800000,  850000,  900000,  950000, 1000000, 1050000, 1100000, 1150000,
	1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000, 1550000,
	1600000, 1650000, 1700000, 1750000, 1800000, 1850000, 1900000, 1950000,
	2000000, 2050000, 2100000, 2150000, 2200000, 2250000, 2300000, 2350000,
	2400000, 2450000, 2500000, 2550000, 2600000, 2650000, 2700000, 2750000,
	2800000, 2850000, 2900000, 2950000, 3000000, 3000000, 3000000, 3000000,
};

static const unsigned int BUCK2_suspend_table[] = {
	      0,   50000,  100000,  150000,  200000,  250000,  300000,  350000,
	 400000,  450000,  500000,  550000,  600000,  650000,  700000,  750000,
	 800000,  850000,  900000,  950000, 1000000, 1050000, 1100000, 1150000,
	1200000, 1250000, 1300000, 1350000, 1400000, 1450000, 1500000, 1550000,
	1600000, 1650000, 1700000, 1750000, 1800000, 1850000, 1900000, 1950000,
	2000000, 2050000, 2100000, 2150000, 2200000, 2250000, 2300000, 2350000,
	2400000, 2450000, 2500000, 2550000, 2600000, 2650000, 2700000, 2750000,
	2800000, 2850000, 2900000, 2950000, 3000000, 3000000, 3000000, 3000000,
};

static const unsigned int BUCK3_table[] = {
              0,   25000,   50000,   75000,  100000,  125000,  150000,  175000,
	 200000,  225000,  250000,  275000,  300000,  325000,  350000,  375000,
	 400000,  425000,  450000,  475000,  500000,  525000,  550000,  575000,
	 600000,  625000,  650000,  675000,  700000,  725000,  750000,  775000,
	 800000,  825000,  850000,  875000,  900000,  925000,  950000,  975000,
	1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000,
	1400000, 1425000, 1450000, 1475000, 1500000, 1500000, 1500000, 1500000,
};

static const unsigned int BUCK3_suspend_table[] = {
              0,   25000,   50000,   75000,  100000,  125000,  150000,  175000,
	 200000,  225000,  250000,  275000,  300000,  325000,  350000,  375000,
	 400000,  425000,  450000,  475000,  500000,  525000,  550000,  575000,
	 600000,  625000,  650000,  675000,  700000,  725000,  750000,  775000,
	 800000,  825000,  850000,  875000,  900000,  925000,  950000,  975000,
	1000000, 1025000, 1050000, 1075000, 1100000, 1125000, 1150000, 1175000,
	1200000, 1225000, 1250000, 1275000, 1300000, 1325000, 1350000, 1375000,
	1400000, 1425000, 1450000, 1475000, 1500000, 1500000, 1500000, 1500000,
};

static const unsigned int LDO1_table[] = {
	1800000, 1200000, 2800000, 0,
};

static const unsigned int LDO1_suspend_table[] = {
	1800000, 1200000, 0, 0,
};

static const unsigned int LDO2_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 3300000,
};

static const unsigned int LDO2_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO3_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 3300000,
};

static const unsigned int LDO3_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO4_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2900000, 3300000,
};

static const unsigned int LDO4_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2900000, 2900000,
};

static const unsigned int LDO5_table[] = {
	2900000, 3000000, 3100000, 3300000,
};

static const unsigned int LDO5_suspend_table[] = {
	2900000, 0, 0, 0,
};

static const unsigned int LDO6_table[] = {
	1800000, 1850000, 2600000, 2650000, 2700000, 2750000, 2800000, 3300000,
};

static const unsigned int LDO6_suspend_table[] = {
	1800000, 1850000, 2600000, 2650000, 2700000, 2750000, 2800000, 2900000,
};

static const unsigned int LDO7_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO7_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO8_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO8_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO9_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 3300000,
};

static const unsigned int LDO9_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
};

static const unsigned int LDO10_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 3300000,
	1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000,
};

static const unsigned int LDO10_suspend_table[] = {
	1800000, 1850000, 1900000, 2700000, 2750000, 2800000, 2850000, 2900000,
	1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000,
};

static const unsigned int LDO12_table[] = {
	1800000, 1900000, 2700000, 2800000, 2900000, 3000000, 3100000, 3300000,
	1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000,
};

static const unsigned int LDO12_suspend_table[] = {
	1800000, 1900000, 2700000, 2800000, 2900000, 2900000, 2900000, 2900000,
	1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000, 1200000,
};

static const unsigned int LDO13_table[] = {
	1300000, 1800000, 2000000, 2500000, 2800000, 3000000, 0, 0,
};

static const unsigned int LDO13_suspend_table[] = {
	0,
};

static const unsigned int LDO14_table[] = {
	1800000, 1850000, 2700000, 2750000, 2800000, 2850000, 2900000, 3300000,
};

static const unsigned int LDO14_suspend_table[] = {
	1800000, 1850000, 2700000, 2750000, 2800000, 2850000, 2900000, 2900000,
};

static int pm8607_list_voltage(struct regulator_dev *rdev, unsigned index)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	int ret = -EINVAL;

	if (info->vol_table && (index < (1 << info->vol_nbits))) {
		ret = info->vol_table[index];
		if (info->slope_double)
			ret <<= 1;
	}
	return ret;
}

static int choose_voltage(struct regulator_dev *rdev, int min_uV, int max_uV)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	int i, ret = -ENOENT;

	if (info->slope_double) {
		min_uV = min_uV >> 1;
		max_uV = max_uV >> 1;
	}
	if (info->vol_table) {
		for (i = 0; i < (1 << info->vol_nbits); i++) {
			if (!info->vol_table[i])
				break;
			if ((min_uV <= info->vol_table[i])
				&& (max_uV >= info->vol_table[i])) {
				ret = i;
				break;
			}
		}
	}
	if (ret < 0)
		pr_err("invalid voltage range (%d %d) uV\n", min_uV, max_uV);
	return ret;
}

static int pm8607_set_voltage(struct regulator_dev *rdev,
			      int min_uV, int max_uV, unsigned *selector)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	uint8_t val, mask;
	int ret;

	if (min_uV > max_uV) {
		pr_err("invalid voltage range (%d, %d) uV\n", min_uV, max_uV);
		return -EINVAL;
	}

	ret = choose_voltage(rdev, min_uV, max_uV);
	if (ret < 0)
		return -EINVAL;
	*selector = ret;
	val = (uint8_t)(ret << info->vol_shift);
	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;

	ret = pm860x_set_bits(info->i2c, info->vol_reg, mask, val);
	if (ret)
		return ret;
	switch (info->desc.id) {
	case PM8607_ID_BUCK1:
	case PM8607_ID_BUCK3:
		ret = pm860x_set_bits(info->i2c, info->update_reg,
				      1 << info->update_bit,
				      1 << info->update_bit);
		break;
	}
	return ret;
}

static int pm8607_get_voltage(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	uint8_t val, mask;
	int ret;

	ret = pm860x_reg_read(info->i2c, info->vol_reg);
	if (ret < 0)
		return ret;

	mask = ((1 << info->vol_nbits) - 1)  << info->vol_shift;
	val = ((unsigned char)ret & mask) >> info->vol_shift;

	return pm8607_list_voltage(rdev, val);
}

static int pm8607_enable(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);

	return pm860x_set_bits(info->i2c, info->enable_reg,
			       1 << info->enable_bit,
			       1 << info->enable_bit);
}

static int pm8607_disable(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);

	return pm860x_set_bits(info->i2c, info->enable_reg,
			       1 << info->enable_bit, 0);
}

static int pm8607_is_enabled(struct regulator_dev *rdev)
{
	struct pm8607_regulator_info *info = rdev_get_drvdata(rdev);
	int ret;

	ret = pm860x_reg_read(info->i2c, info->enable_reg);
	if (ret < 0)
		return ret;

	return !!((unsigned char)ret & (1 << info->enable_bit));
}

static struct regulator_ops pm8607_regulator_ops = {
	.set_voltage	= pm8607_set_voltage,
	.get_voltage	= pm8607_get_voltage,
	.enable		= pm8607_enable,
	.disable	= pm8607_disable,
	.is_enabled	= pm8607_is_enabled,
};

#define PM8607_DVC(vreg, nbits, ureg, ubit, ereg, ebit)			\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm8607_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM8607_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
	},								\
	.vol_reg	= PM8607_##vreg,				\
	.vol_shift	= (0),						\
	.vol_nbits	= (nbits),					\
	.update_reg	= PM8607_##ureg,				\
	.update_bit	= (ubit),					\
	.enable_reg	= PM8607_##ereg,				\
	.enable_bit	= (ebit),					\
	.slope_double	= (0),						\
	.vol_table	= (unsigned int *)&vreg##_table,		\
	.vol_suspend	= (unsigned int *)&vreg##_suspend_table,	\
}

#define PM8607_LDO(_id, vreg, shift, nbits, ereg, ebit)			\
{									\
	.desc	= {							\
		.name	= "LDO" #_id,					\
		.ops	= &pm8607_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM8607_ID_LDO##_id,				\
		.owner	= THIS_MODULE,					\
	},								\
	.vol_reg	= PM8607_##vreg,				\
	.vol_shift	= (shift),					\
	.vol_nbits	= (nbits),					\
	.enable_reg	= PM8607_##ereg,				\
	.enable_bit	= (ebit),					\
	.slope_double	= (0),						\
	.vol_table	= (unsigned int *)&LDO##_id##_table,		\
	.vol_suspend	= (unsigned int *)&LDO##_id##_suspend_table,	\
}

static struct pm8607_regulator_info pm8607_regulator_info[] = {
	PM8607_DVC(BUCK1, 6, GO, 0, SUPPLIES_EN11, 0),
	PM8607_DVC(BUCK2, 6, GO, 1, SUPPLIES_EN11, 1),
	PM8607_DVC(BUCK3, 6, GO, 2, SUPPLIES_EN11, 2),

	PM8607_LDO( 1,         LDO1, 0, 2, SUPPLIES_EN11, 3),
	PM8607_LDO( 2,         LDO2, 0, 3, SUPPLIES_EN11, 4),
	PM8607_LDO( 3,         LDO3, 0, 3, SUPPLIES_EN11, 5),
	PM8607_LDO( 4,         LDO4, 0, 3, SUPPLIES_EN11, 6),
	PM8607_LDO( 5,         LDO5, 0, 2, SUPPLIES_EN11, 7),
	PM8607_LDO( 6,         LDO6, 0, 3, SUPPLIES_EN12, 0),
	PM8607_LDO( 7,         LDO7, 0, 3, SUPPLIES_EN12, 1),
	PM8607_LDO( 8,         LDO8, 0, 3, SUPPLIES_EN12, 2),
	PM8607_LDO( 9,         LDO9, 0, 3, SUPPLIES_EN12, 3),
	PM8607_LDO(10,        LDO10, 0, 3, SUPPLIES_EN12, 4),
	PM8607_LDO(12,        LDO12, 0, 4, SUPPLIES_EN12, 5),
	PM8607_LDO(13, VIBRATOR_SET, 1, 3,  VIBRATOR_SET, 0),
	PM8607_LDO(14,        LDO14, 0, 4, SUPPLIES_EN12, 6),
};

static int __devinit pm8607_regulator_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm8607_regulator_info *info = NULL;
	struct regulator_init_data *pdata = pdev->dev.platform_data;
	struct resource *res;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (res == NULL) {
		dev_err(&pdev->dev, "No I/O resource!\n");
		return -EINVAL;
	}
	for (i = 0; i < ARRAY_SIZE(pm8607_regulator_info); i++) {
		info = &pm8607_regulator_info[i];
		if (info->desc.id == res->start)
			break;
	}
	if (i == ARRAY_SIZE(pm8607_regulator_info)) {
		dev_err(&pdev->dev, "Failed to find regulator %llu\n",
			(unsigned long long)res->start);
		return -EINVAL;
	}
	info->i2c = (chip->id == CHIP_PM8607) ? chip->client : chip->companion;
	info->chip = chip;

	/* check DVC ramp slope double */
	if ((i == PM8607_ID_BUCK3) && info->chip->buck3_double)
		info->slope_double = 1;

	/* replace driver_data with info */
	info->regulator = regulator_register(&info->desc, &pdev->dev,
					     pdata, info);
	if (IS_ERR(info->regulator)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			info->desc.name);
		return PTR_ERR(info->regulator);
	}

	platform_set_drvdata(pdev, info);
	return 0;
}

static int __devexit pm8607_regulator_remove(struct platform_device *pdev)
{
	struct pm8607_regulator_info *info = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);
	regulator_unregister(info->regulator);
	return 0;
}

static struct platform_driver pm8607_regulator_driver = {
	.driver		= {
		.name	= "88pm860x-regulator",
		.owner	= THIS_MODULE,
	},
	.probe		= pm8607_regulator_probe,
	.remove		= __devexit_p(pm8607_regulator_remove),
};

static int __init pm8607_regulator_init(void)
{
	return platform_driver_register(&pm8607_regulator_driver);
}
subsys_initcall(pm8607_regulator_init);

static void __exit pm8607_regulator_exit(void)
{
	platform_driver_unregister(&pm8607_regulator_driver);
}
module_exit(pm8607_regulator_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Haojian Zhuang <haojian.zhuang@marvell.com>");
MODULE_DESCRIPTION("Regulator Driver for Marvell 88PM8607 PMIC");
MODULE_ALIAS("platform:88pm8607-regulator");
