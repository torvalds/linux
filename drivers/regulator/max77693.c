/*
 * max77693.c - Regulator driver for the Maxim 77693
 *
 * Copyright (C) 2013 Samsung Electronics
 * Jonghwa Lee <jonghwa3.lee@samsung.com>
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
#include <linux/mfd/max77693-private.h>
#include <linux/regulator/of_regulator.h>

#define CHGIN_ILIM_STEP_20mA			20000

/* CHARGER regulator ops */
/* CHARGER regulator uses two bits for enabling */
static int max77693_chg_is_enabled(struct regulator_dev *rdev)
{
	int ret;
	u8 val;

	ret = max77693_read_reg(rdev->regmap, rdev->desc->enable_reg, &val);
	if (ret)
		return ret;

	return (val & rdev->desc->enable_mask) == rdev->desc->enable_mask;
}

/*
 * CHARGER regulator - Min : 20mA, Max : 2580mA, step : 20mA
 * 0x00, 0x01, 0x2, 0x03	= 60 mA
 * 0x04 ~ 0x7E			= (60 + (X - 3) * 20) mA
 */
static int max77693_chg_get_current_limit(struct regulator_dev *rdev)
{
	unsigned int chg_min_uA = rdev->constraints->min_uA;
	unsigned int chg_max_uA = rdev->constraints->max_uA;
	u8 reg, sel;
	unsigned int val;
	int ret;

	ret = max77693_read_reg(rdev->regmap,
				MAX77693_CHG_REG_CHG_CNFG_09, &reg);
	if (ret < 0)
		return ret;

	sel = reg & CHG_CNFG_09_CHGIN_ILIM_MASK;

	/* the first four codes for charger current are all 60mA */
	if (sel <= 3)
		sel = 0;
	else
		sel -= 3;

	val = chg_min_uA + CHGIN_ILIM_STEP_20mA * sel;
	if (val > chg_max_uA)
		return -EINVAL;

	return val;
}

static int max77693_chg_set_current_limit(struct regulator_dev *rdev,
						int min_uA, int max_uA)
{
	unsigned int chg_min_uA = rdev->constraints->min_uA;
	int sel = 0;

	while (chg_min_uA + CHGIN_ILIM_STEP_20mA * sel < min_uA)
		sel++;

	if (chg_min_uA + CHGIN_ILIM_STEP_20mA * sel > max_uA)
		return -EINVAL;

	/* the first four codes for charger current are all 60mA */
	sel += 3;

	return max77693_write_reg(rdev->regmap,
				MAX77693_CHG_REG_CHG_CNFG_09, sel);
}
/* end of CHARGER regulator ops */

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
	.is_enabled		= max77693_chg_is_enabled,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.get_current_limit	= max77693_chg_get_current_limit,
	.set_current_limit	= max77693_chg_set_current_limit,
};

#define regulator_desc_esafeout(_num)	{			\
	.name		= "ESAFEOUT"#_num,			\
	.id		= MAX77693_ESAFEOUT##_num,		\
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

static struct regulator_desc regulators[] = {
	regulator_desc_esafeout(1),
	regulator_desc_esafeout(2),
	{
		.name = "CHARGER",
		.id = MAX77693_CHARGER,
		.ops = &max77693_charger_ops,
		.type = REGULATOR_CURRENT,
		.owner = THIS_MODULE,
		.enable_reg = MAX77693_CHG_REG_CHG_CNFG_00,
		.enable_mask = CHG_CNFG_00_CHG_MASK |
				CHG_CNFG_00_BUCK_MASK,
	},
};

#ifdef CONFIG_OF
static int max77693_pmic_dt_parse_rdata(struct device *dev,
					struct max77693_regulator_data **rdata)
{
	struct device_node *np;
	struct of_regulator_match *rmatch;
	struct max77693_regulator_data *tmp;
	int i, matched = 0;

	np = of_get_child_by_name(dev->parent->of_node, "regulators");
	if (!np)
		return -EINVAL;

	rmatch = devm_kzalloc(dev,
		 sizeof(*rmatch) * ARRAY_SIZE(regulators), GFP_KERNEL);
	if (!rmatch) {
		of_node_put(np);
		return -ENOMEM;
	}

	for (i = 0; i < ARRAY_SIZE(regulators); i++)
		rmatch[i].name = regulators[i].name;

	matched = of_regulator_match(dev, np, rmatch, ARRAY_SIZE(regulators));
	of_node_put(np);
	if (matched <= 0)
		return matched;
	*rdata = devm_kzalloc(dev, sizeof(**rdata) * matched, GFP_KERNEL);
	if (!(*rdata))
		return -ENOMEM;

	tmp = *rdata;

	for (i = 0; i < matched; i++) {
		tmp->initdata = rmatch[i].init_data;
		tmp->of_node = rmatch[i].of_node;
		tmp->id = regulators[i].id;
		tmp++;
	}

	return matched;
}
#else
static int max77693_pmic_dt_parse_rdata(struct device *dev,
					struct max77693_regulator_data **rdata)
{
	return 0;
}
#endif /* CONFIG_OF */

static int max77693_pmic_init_rdata(struct device *dev,
				    struct max77693_regulator_data **rdata)
{
	struct max77693_platform_data *pdata;
	int num_regulators = 0;

	pdata = dev_get_platdata(dev->parent);
	if (pdata) {
		*rdata = pdata->regulators;
		num_regulators = pdata->num_regulators;
	}

	if (!(*rdata) && dev->parent->of_node)
		num_regulators = max77693_pmic_dt_parse_rdata(dev, rdata);

	return num_regulators;
}

static int max77693_pmic_probe(struct platform_device *pdev)
{
	struct max77693_dev *iodev = dev_get_drvdata(pdev->dev.parent);
	struct max77693_regulator_data *rdata = NULL;
	int num_rdata, i;
	struct regulator_config config;

	num_rdata = max77693_pmic_init_rdata(&pdev->dev, &rdata);
	if (!rdata || num_rdata <= 0) {
		dev_err(&pdev->dev, "No init data supplied.\n");
		return -ENODEV;
	}

	config.dev = &pdev->dev;
	config.regmap = iodev->regmap;

	for (i = 0; i < num_rdata; i++) {
		int id = rdata[i].id;
		struct regulator_dev *rdev;

		config.init_data = rdata[i].initdata;
		config.of_node = rdata[i].of_node;

		rdev = devm_regulator_register(&pdev->dev,
						&regulators[id], &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev,
				"Failed to initialize regulator-%d\n", id);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id max77693_pmic_id[] = {
	{"max77693-pmic", 0},
	{},
};

MODULE_DEVICE_TABLE(platform, max77693_pmic_id);

static struct platform_driver max77693_pmic_driver = {
	.driver = {
		   .name = "max77693-pmic",
		   .owner = THIS_MODULE,
		   },
	.probe = max77693_pmic_probe,
	.id_table = max77693_pmic_id,
};

module_platform_driver(max77693_pmic_driver);

MODULE_DESCRIPTION("MAXIM MAX77693 regulator driver");
MODULE_AUTHOR("Jonghwa Lee <jonghwa3.lee@samsung.com>");
MODULE_LICENSE("GPL");
