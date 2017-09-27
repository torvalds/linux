/*
 * Regulators driver for Marvell 88PM8607
 *
 * Copyright (C) 2009 Marvell International Ltd.
 *	Haojian Zhuang <haojian.zhuang@marvell.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regulator/of_regulator.h>
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
	struct i2c_client	*i2c_8606;

	unsigned int	*vol_table;
	unsigned int	*vol_suspend;

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
	1200000, 1300000, 1800000, 2000000, 2500000, 2800000, 3000000, 0,
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

	if (info->vol_table && (index < rdev->desc->n_voltages)) {
		ret = info->vol_table[index];
		if (info->slope_double)
			ret <<= 1;
	}
	return ret;
}

static const struct regulator_ops pm8607_regulator_ops = {
	.list_voltage	= pm8607_list_voltage,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
};

static const struct regulator_ops pm8606_preg_ops = {
	.enable		= regulator_enable_regmap,
	.disable	= regulator_disable_regmap,
	.is_enabled	= regulator_is_enabled_regmap,
};

#define PM8606_PREG(ereg, ebit)						\
{									\
	.desc	= {							\
		.name	= "PREG",					\
		.ops	= &pm8606_preg_ops,				\
		.type	= REGULATOR_CURRENT,				\
		.id	= PM8606_ID_PREG,				\
		.owner	= THIS_MODULE,					\
		.enable_reg = PM8606_##ereg,				\
		.enable_mask = (ebit),					\
		.enable_is_inverted = true,				\
	},								\
}

#define PM8607_DVC(vreg, ureg, ubit, ereg, ebit)			\
{									\
	.desc	= {							\
		.name	= #vreg,					\
		.ops	= &pm8607_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM8607_ID_##vreg,				\
		.owner	= THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(vreg##_table),			\
		.vsel_reg = PM8607_##vreg,				\
		.vsel_mask = ARRAY_SIZE(vreg##_table) - 1,		\
		.apply_reg = PM8607_##ureg,				\
		.apply_bit = (ubit),					\
		.enable_reg = PM8607_##ereg,				\
		.enable_mask = 1 << (ebit),				\
	},								\
	.slope_double	= (0),						\
	.vol_table	= (unsigned int *)&vreg##_table,		\
	.vol_suspend	= (unsigned int *)&vreg##_suspend_table,	\
}

#define PM8607_LDO(_id, vreg, shift, ereg, ebit)			\
{									\
	.desc	= {							\
		.name	= "LDO" #_id,					\
		.ops	= &pm8607_regulator_ops,			\
		.type	= REGULATOR_VOLTAGE,				\
		.id	= PM8607_ID_LDO##_id,				\
		.owner	= THIS_MODULE,					\
		.n_voltages = ARRAY_SIZE(LDO##_id##_table),		\
		.vsel_reg = PM8607_##vreg,				\
		.vsel_mask = (ARRAY_SIZE(LDO##_id##_table) - 1) << (shift), \
		.enable_reg = PM8607_##ereg,				\
		.enable_mask = 1 << (ebit),				\
	},								\
	.slope_double	= (0),						\
	.vol_table	= (unsigned int *)&LDO##_id##_table,		\
	.vol_suspend	= (unsigned int *)&LDO##_id##_suspend_table,	\
}

static struct pm8607_regulator_info pm8607_regulator_info[] = {
	PM8607_DVC(BUCK1, GO, BIT(0), SUPPLIES_EN11, 0),
	PM8607_DVC(BUCK2, GO, BIT(1), SUPPLIES_EN11, 1),
	PM8607_DVC(BUCK3, GO, BIT(2), SUPPLIES_EN11, 2),

	PM8607_LDO(1,         LDO1, 0, SUPPLIES_EN11, 3),
	PM8607_LDO(2,         LDO2, 0, SUPPLIES_EN11, 4),
	PM8607_LDO(3,         LDO3, 0, SUPPLIES_EN11, 5),
	PM8607_LDO(4,         LDO4, 0, SUPPLIES_EN11, 6),
	PM8607_LDO(5,         LDO5, 0, SUPPLIES_EN11, 7),
	PM8607_LDO(6,         LDO6, 0, SUPPLIES_EN12, 0),
	PM8607_LDO(7,         LDO7, 0, SUPPLIES_EN12, 1),
	PM8607_LDO(8,         LDO8, 0, SUPPLIES_EN12, 2),
	PM8607_LDO(9,         LDO9, 0, SUPPLIES_EN12, 3),
	PM8607_LDO(10,        LDO10, 0, SUPPLIES_EN12, 4),
	PM8607_LDO(12,        LDO12, 0, SUPPLIES_EN12, 5),
	PM8607_LDO(13, VIBRATOR_SET, 1, VIBRATOR_SET, 0),
	PM8607_LDO(14,        LDO14, 0, SUPPLIES_EN12, 6),
};

static struct pm8607_regulator_info pm8606_regulator_info[] = {
	PM8606_PREG(PREREGULATORB, 5),
};

#ifdef CONFIG_OF
static int pm8607_regulator_dt_init(struct platform_device *pdev,
				    struct pm8607_regulator_info *info,
				    struct regulator_config *config)
{
	struct device_node *nproot, *np;
	nproot = pdev->dev.parent->of_node;
	if (!nproot)
		return -ENODEV;
	nproot = of_get_child_by_name(nproot, "regulators");
	if (!nproot) {
		dev_err(&pdev->dev, "failed to find regulators node\n");
		return -ENODEV;
	}
	for_each_child_of_node(nproot, np) {
		if (!of_node_cmp(np->name, info->desc.name)) {
			config->init_data =
				of_get_regulator_init_data(&pdev->dev, np,
							   &info->desc);
			config->of_node = np;
			break;
		}
	}
	of_node_put(nproot);
	return 0;
}
#else
#define pm8607_regulator_dt_init(x, y, z)	(-1)
#endif

static int pm8607_regulator_probe(struct platform_device *pdev)
{
	struct pm860x_chip *chip = dev_get_drvdata(pdev->dev.parent);
	struct pm8607_regulator_info *info = NULL;
	struct regulator_init_data *pdata = dev_get_platdata(&pdev->dev);
	struct regulator_config config = { };
	struct resource *res;
	int i;

	res = platform_get_resource(pdev, IORESOURCE_REG, 0);
	if (res) {
		/* There're resources in 88PM8607 regulator driver */
		for (i = 0; i < ARRAY_SIZE(pm8607_regulator_info); i++) {
			info = &pm8607_regulator_info[i];
			if (info->desc.vsel_reg == res->start)
				break;
		}
		if (i == ARRAY_SIZE(pm8607_regulator_info)) {
			dev_err(&pdev->dev, "Failed to find regulator %llu\n",
				(unsigned long long)res->start);
			return -EINVAL;
		}
	} else {
		/* There's no resource in 88PM8606 PREG regulator driver */
		info = &pm8606_regulator_info[0];
		/* i is used to check regulator ID */
		i = -1;
	}
	info->i2c = (chip->id == CHIP_PM8607) ? chip->client : chip->companion;
	info->i2c_8606 = (chip->id == CHIP_PM8607) ? chip->companion :
			chip->client;
	info->chip = chip;

	/* check DVC ramp slope double */
	if ((i == PM8607_ID_BUCK3) && info->chip->buck3_double)
		info->slope_double = 1;

	config.dev = &pdev->dev;
	config.driver_data = info;

	if (pm8607_regulator_dt_init(pdev, info, &config))
		if (pdata)
			config.init_data = pdata;

	if (chip->id == CHIP_PM8607)
		config.regmap = chip->regmap;
	else
		config.regmap = chip->regmap_companion;

	info->regulator = devm_regulator_register(&pdev->dev, &info->desc,
						  &config);
	if (IS_ERR(info->regulator)) {
		dev_err(&pdev->dev, "failed to register regulator %s\n",
			info->desc.name);
		return PTR_ERR(info->regulator);
	}

	platform_set_drvdata(pdev, info);
	return 0;
}

static const struct platform_device_id pm8607_regulator_driver_ids[] = {
	{
		.name	= "88pm860x-regulator",
		.driver_data	= 0,
	}, {
		.name	= "88pm860x-preg",
		.driver_data	= 0,
	},
	{ },
};
MODULE_DEVICE_TABLE(platform, pm8607_regulator_driver_ids);

static struct platform_driver pm8607_regulator_driver = {
	.driver		= {
		.name	= "88pm860x-regulator",
	},
	.probe		= pm8607_regulator_probe,
	.id_table	= pm8607_regulator_driver_ids,
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
