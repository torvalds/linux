/*
 * max77693.c - Regulator driver for the Maxim 77693 and 77843
 *
 * Copyright (C) 2013-2015 Samsung Electronics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
 * Krzysztof Kozlowski <krzk@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 * This driver is based on max77686.c
 */

#include <linux/err.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include <linux/module.h>
#include <linux/export.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/machine.h>
#include <linux/mfd/max77693.h>
#include <linux/mfd/max77693-common.h>
#include <linux/mfd/max77693-private.h>
#include <linux/mfd/max77843-private.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regmap.h>

/*
 * ID for MAX77843 regulators.
 * There is no need for such for MAX77693.
 */
enum max77843_regulator_type {
	MAX77843_SAFEOUT1 = 0,
	MAX77843_SAFEOUT2,
	MAX77843_CHARGER,

	MAX77843_NUM,
};

/* Register differences between chargers: MAX77693 and MAX77843 */
struct chg_reg_data {
	unsigned int linear_reg;
	unsigned int linear_mask;
	unsigned int uA_step;
	unsigned int min_sel;
};

/*
 * MAX77693 CHARGER regulator - Min : 20mA, Max : 2580mA, step : 20mA
 * 0x00, 0x01, 0x2, 0x03	= 60 mA
 * 0x04 ~ 0x7E			= (60 + (X - 3) * 20) mA
 * Actually for MAX77693 the driver manipulates the maximum input current,
 * not the fast charge current (output). This should be fixed.
 *
 * On MAX77843 the calculation formula is the same (except values).
 * Fortunately it properly manipulates the fast charge current.
 */
static int max77693_chg_get_current_limit(struct regulator_dev *rdev)
{
	const struct chg_reg_data *reg_data = rdev_get_drvdata(rdev);
	unsigned int chg_min_uA = rdev->constraints->min_uA;
	unsigned int chg_max_uA = rdev->constraints->max_uA;
	unsigned int reg, sel;
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, reg_data->linear_reg, &reg);
	if (ret < 0)
		return ret;

	sel = reg & reg_data->linear_mask;

	/* the first four codes for charger current are all 60mA */
	if (sel <= reg_data->min_sel)
		sel = 0;
	else
		sel -= reg_data->min_sel;

	val = chg_min_uA + reg_data->uA_step * sel;
	if (val > chg_max_uA)
		return -EINVAL;

	return val;
}

static int max77693_chg_set_current_limit(struct regulator_dev *rdev,
						int min_uA, int max_uA)
{
	const struct chg_reg_data *reg_data = rdev_get_drvdata(rdev);
	unsigned int chg_min_uA = rdev->constraints->min_uA;
	int sel = 0;

	while (chg_min_uA + reg_data->uA_step * sel < min_uA)
		sel++;

	if (chg_min_uA + reg_data->uA_step * sel > max_uA)
		return -EINVAL;

	/* the first four codes for charger current are all 60mA */
	sel += reg_data->min_sel;

	return regmap_write(rdev->regmap, reg_data->linear_reg, sel);
}
/* end of CHARGER regulator ops */

/* Returns regmap suitable for given regulator on chosen device */
static struct regmap *max77693_get_regmap(enum max77693_types type,
					  struct max77693_dev *max77693,
					  int reg_id)
{
	if (type == TYPE_MAX77693)
		return max77693->regmap;

	/* Else: TYPE_MAX77843 */
	switch (reg_id) {
	case MAX77843_SAFEOUT1:
	case MAX77843_SAFEOUT2:
		return max77693->regmap;
	case MAX77843_CHARGER:
		return max77693->regmap_chg;
	default:
		return max77693->regmap;
	}
}

static const unsigned int max77693_safeout_table[] = {
	4850000,
	4900000,
	4950000,
	3300000,
};

static struct regulator_ops max77693_safeout_ops = {
	.list_voltage		= regulator_list_voltage_table,
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
};

static struct regulator_ops max77693_charger_ops = {
	.is_enabled		= regulator_is_enabled_regmap,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_current_limit	= max77693_chg_get_current_limit,
	.set_current_limit	= max77693_chg_set_current_limit,
};

#define max77693_regulator_desc_esafeout(_num)	{		\
	.name		= "ESAFEOUT"#_num,			\
	.id		= MAX77693_ESAFEOUT##_num,		\
	.of_match	= of_match_ptr("ESAFEOUT"#_num),	\
	.regulators_node	= of_match_ptr("regulators"),	\
	.n_voltages	= 4,					\
	.ops		= &max77693_safeout_ops,		\
	.type		= REGULATOR_VOLTAGE,			\
	.owner		= THIS_MODULE,				\
	.volt_table	= max77693_safeout_table,		\
	.vsel_reg	= MAX77693_CHG_REG_SAFEOUT_CTRL,	\
	.vsel_mask	= SAFEOUT_CTRL_SAFEOUT##_num##_MASK,	\
	.enable_reg	= MAX77693_CHG_REG_SAFEOUT_CTRL,	\
	.enable_mask	= SAFEOUT_CTRL_ENSAFEOUT##_num##_MASK ,	\
}

static const struct regulator_desc max77693_supported_regulators[] = {
	max77693_regulator_desc_esafeout(1),
	max77693_regulator_desc_esafeout(2),
	{
		.name = "CHARGER",
		.id = MAX77693_CHARGER,
		.of_match = of_match_ptr("CHARGER"),
		.regulators_node = of_match_ptr("regulators"),
		.ops = &max77693_charger_ops,
		.type = REGULATOR_CURRENT,
		.owner = THIS_MODULE,
		.enable_reg = MAX77693_CHG_REG_CHG_CNFG_00,
		.enable_mask = CHG_CNFG_00_CHG_MASK |
				CHG_CNFG_00_BUCK_MASK,
		.enable_val = CHG_CNFG_00_CHG_MASK | CHG_CNFG_00_BUCK_MASK,
	},
};

static const struct chg_reg_data max77693_chg_reg_data = {
	.linear_reg	= MAX77693_CHG_REG_CHG_CNFG_09,
	.linear_mask	= CHG_CNFG_09_CHGIN_ILIM_MASK,
	.uA_step	= 20000,
	.min_sel	= 3,
};

#define	max77843_regulator_desc_esafeout(num)	{			\
	.name		= "SAFEOUT" # num,				\
	.id		= MAX77843_SAFEOUT ## num,			\
	.ops		= &max77693_safeout_ops,			\
	.of_match	= of_match_ptr("SAFEOUT" # num),		\
	.regulators_node = of_match_ptr("regulators"),			\
	.type		= REGULATOR_VOLTAGE,				\
	.owner		= THIS_MODULE,					\
	.n_voltages	= ARRAY_SIZE(max77693_safeout_table),		\
	.volt_table	= max77693_safeout_table,			\
	.enable_reg	= MAX77843_SYS_REG_SAFEOUTCTRL,			\
	.enable_mask	= MAX77843_REG_SAFEOUTCTRL_ENSAFEOUT ## num,	\
	.vsel_reg	= MAX77843_SYS_REG_SAFEOUTCTRL,			\
	.vsel_mask	= MAX77843_REG_SAFEOUTCTRL_SAFEOUT ## num ## _MASK, \
}

static const struct regulator_desc max77843_supported_regulators[] = {
	[MAX77843_SAFEOUT1] = max77843_regulator_desc_esafeout(1),
	[MAX77843_SAFEOUT2] = max77843_regulator_desc_esafeout(2),
	[MAX77843_CHARGER] = {
		.name		= "CHARGER",
		.id		= MAX77843_CHARGER,
		.ops		= &max77693_charger_ops,
		.of_match	= of_match_ptr("CHARGER"),
		.regulators_node = of_match_ptr("regulators"),
		.type		= REGULATOR_CURRENT,
		.owner		= THIS_MODULE,
		.enable_reg	= MAX77843_CHG_REG_CHG_CNFG_00,
		.enable_mask	= MAX77843_CHG_MASK,
		.enable_val	= MAX77843_CHG_MASK,
	},
};

static const struct chg_reg_data max77843_chg_reg_data = {
	.linear_reg	= MAX77843_CHG_REG_CHG_CNFG_02,
	.linear_mask	= MAX77843_CHG_FAST_CHG_CURRENT_MASK,
	.uA_step	= MAX77843_CHG_FAST_CHG_CURRENT_STEP,
	.min_sel	= 2,
};

static int max77693_pmic_probe(struct platform_device *pdev)
{
	enum max77693_types type = platform_get_device_id(pdev)->driver_data;
	struct max77693_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	const struct regulator_desc *regulators;
	unsigned int regulators_size;
	int i;
	struct regulator_config config = { };

	config.dev = iodev->dev;

	switch (type) {
	case TYPE_MAX77693:
		regulators = max77693_supported_regulators;
		regulators_size = ARRAY_SIZE(max77693_supported_regulators);
		config.driver_data = (void *)&max77693_chg_reg_data;
		break;
	case TYPE_MAX77843:
		regulators = max77843_supported_regulators;
		regulators_size = ARRAY_SIZE(max77843_supported_regulators);
		config.driver_data = (void *)&max77843_chg_reg_data;
		break;
	default:
		dev_err(&pdev->dev, "Unsupported device type: %u\n", type);
		return -ENODEV;
	}

	for (i = 0; i < regulators_size; i++) {
		struct regulator_dev *rdev;

		config.regmap = max77693_get_regmap(type, iodev,
						    regulators[i].id);

		rdev = devm_regulator_register(&pdev->dev,
						&regulators[i], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"Failed to initialize regulator-%d\n", i);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id max77693_pmic_id[] = {
	{ "max77693-pmic", TYPE_MAX77693 },
	{ "max77843-regulator", TYPE_MAX77843 },
	{},
};

MODULE_DEVICE_TABLE(platform, max77693_pmic_id);

static struct platform_driver max77693_pmic_driver = {
	.driver = {
		   .name = "max77693-pmic",
		   },
	.probe = max77693_pmic_probe,
	.id_table = max77693_pmic_id,
};

static int __init max77693_pmic_init(void)
{
	return platform_driver_register(&max77693_pmic_driver);
}
subsys_initcall(max77693_pmic_init);

static void __exit max77693_pmic_cleanup(void)
{
	platform_driver_unregister(&max77693_pmic_driver);
}
module_exit(max77693_pmic_cleanup);

MODULE_DESCRIPTION("MAXIM 77693/77843 regulator driver");
MODULE_AUTHOR("Jonghwa Lee <jonghwa3.lee@samsung.com>");
MODULE_AUTHOR("Krzysztof Kozlowski <krzk@kernel.org>");
MODULE_LICENSE("GPL");
