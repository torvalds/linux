/*
 * AXP20x regulators driver.
 *
 * Copyright (C) 2013 Carlo Caione <carlo@caione.org>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file "COPYING" in the main directory of this
 * archive for more details.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/mfd/axp20x.h>
#include <linux/regulator/driver.h>
#include <linux/regulator/of_regulator.h>

#define AXP20X_IO_ENABLED		0x03
#define AXP20X_IO_DISABLED		0x07

#define AXP20X_WORKMODE_DCDC2_MASK	BIT(2)
#define AXP20X_WORKMODE_DCDC3_MASK	BIT(1)

#define AXP20X_FREQ_DCDC_MASK		0x0f

#define AXP20X_DESC_IO(_id, _match, _supply, _min, _max, _step, _vreg, _vmask,	\
		       _ereg, _emask, _enable_val, _disable_val)		\
	[AXP20X_##_id] = {							\
		.name		= #_id,						\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP20X_##_id,					\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.enable_val	= (_enable_val),				\
		.disable_val	= (_disable_val),				\
		.ops		= &axp20x_ops,					\
	}

#define AXP20X_DESC(_id, _match, _supply, _min, _max, _step, _vreg, _vmask,	\
		    _ereg, _emask)						\
	[AXP20X_##_id] = {							\
		.name		= #_id,						\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP20X_##_id,					\
		.n_voltages	= (((_max) - (_min)) / (_step) + 1),		\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_min) * 1000,				\
		.uV_step	= (_step) * 1000,				\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.ops		= &axp20x_ops,					\
	}

#define AXP20X_DESC_FIXED(_id, _match, _supply, _volt)				\
	[AXP20X_##_id] = {							\
		.name		= #_id,						\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP20X_##_id,					\
		.n_voltages	= 1,						\
		.owner		= THIS_MODULE,					\
		.min_uV		= (_volt) * 1000,				\
		.ops		= &axp20x_ops_fixed				\
	}

#define AXP20X_DESC_TABLE(_id, _match, _supply, _table, _vreg, _vmask, _ereg,	\
			  _emask)						\
	[AXP20X_##_id] = {							\
		.name		= #_id,						\
		.supply_name	= (_supply),					\
		.of_match	= of_match_ptr(_match),				\
		.regulators_node = of_match_ptr("regulators"),			\
		.type		= REGULATOR_VOLTAGE,				\
		.id		= AXP20X_##_id,					\
		.n_voltages	= ARRAY_SIZE(_table),				\
		.owner		= THIS_MODULE,					\
		.vsel_reg	= (_vreg),					\
		.vsel_mask	= (_vmask),					\
		.enable_reg	= (_ereg),					\
		.enable_mask	= (_emask),					\
		.volt_table	= (_table),					\
		.ops		= &axp20x_ops_table,				\
	}

static const int axp20x_ldo4_data[] = { 1250000, 1300000, 1400000, 1500000, 1600000,
					1700000, 1800000, 1900000, 2000000, 2500000,
					2700000, 2800000, 3000000, 3100000, 3200000,
					3300000 };

static struct regulator_ops axp20x_ops_fixed = {
	.list_voltage		= regulator_list_voltage_linear,
};

static struct regulator_ops axp20x_ops_table = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_table,
	.map_voltage		= regulator_map_voltage_ascend,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static struct regulator_ops axp20x_ops = {
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear,
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static const struct regulator_desc axp20x_regulators[] = {
	AXP20X_DESC(DCDC2, "dcdc2", "vin2", 700, 2275, 25, AXP20X_DCDC2_V_OUT,
		    0x3f, AXP20X_PWR_OUT_CTRL, 0x10),
	AXP20X_DESC(DCDC3, "dcdc3", "vin3", 700, 3500, 25, AXP20X_DCDC3_V_OUT,
		    0x7f, AXP20X_PWR_OUT_CTRL, 0x02),
	AXP20X_DESC_FIXED(LDO1, "ldo1", "acin", 1300),
	AXP20X_DESC(LDO2, "ldo2", "ldo24in", 1800, 3300, 100,
		    AXP20X_LDO24_V_OUT, 0xf0, AXP20X_PWR_OUT_CTRL, 0x04),
	AXP20X_DESC(LDO3, "ldo3", "ldo3in", 700, 3500, 25, AXP20X_LDO3_V_OUT,
		    0x7f, AXP20X_PWR_OUT_CTRL, 0x40),
	AXP20X_DESC_TABLE(LDO4, "ldo4", "ldo24in", axp20x_ldo4_data,
			  AXP20X_LDO24_V_OUT, 0x0f, AXP20X_PWR_OUT_CTRL, 0x08),
	AXP20X_DESC_IO(LDO5, "ldo5", "ldo5in", 1800, 3300, 100,
		       AXP20X_LDO5_V_OUT, 0xf0, AXP20X_GPIO0_CTRL, 0x07,
		       AXP20X_IO_ENABLED, AXP20X_IO_DISABLED),
};

static int axp20x_set_dcdc_freq(struct platform_device *pdev, u32 dcdcfreq)
{
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);

	if (dcdcfreq < 750) {
		dcdcfreq = 750;
		dev_warn(&pdev->dev, "DCDC frequency too low. Set to 750kHz\n");
	}

	if (dcdcfreq > 1875) {
		dcdcfreq = 1875;
		dev_warn(&pdev->dev, "DCDC frequency too high. Set to 1875kHz\n");
	}

	dcdcfreq = (dcdcfreq - 750) / 75;

	return regmap_update_bits(axp20x->regmap, AXP20X_DCDC_FREQ,
				  AXP20X_FREQ_DCDC_MASK, dcdcfreq);
}

static int axp20x_regulator_parse_dt(struct platform_device *pdev)
{
	struct device_node *np, *regulators;
	int ret;
	u32 dcdcfreq;

	np = of_node_get(pdev->dev.parent->of_node);
	if (!np)
		return 0;

	regulators = of_get_child_by_name(np, "regulators");
	if (!regulators) {
		dev_warn(&pdev->dev, "regulators node not found\n");
	} else {
		dcdcfreq = 1500;
		of_property_read_u32(regulators, "x-powers,dcdc-freq", &dcdcfreq);
		ret = axp20x_set_dcdc_freq(pdev, dcdcfreq);
		if (ret < 0) {
			dev_err(&pdev->dev, "Error setting dcdc frequency: %d\n", ret);
			return ret;
		}

		of_node_put(regulators);
	}

	return 0;
}

static int axp20x_set_dcdc_workmode(struct regulator_dev *rdev, int id, u32 workmode)
{
	unsigned int mask = AXP20X_WORKMODE_DCDC2_MASK;

	if ((id != AXP20X_DCDC2) && (id != AXP20X_DCDC3))
		return -EINVAL;

	if (id == AXP20X_DCDC3)
		mask = AXP20X_WORKMODE_DCDC3_MASK;

	workmode <<= ffs(mask) - 1;

	return regmap_update_bits(rdev->regmap, AXP20X_DCDC_MODE, mask, workmode);
}

static int axp20x_regulator_probe(struct platform_device *pdev)
{
	struct regulator_dev *rdev;
	struct axp20x_dev *axp20x = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = {
		.dev = pdev->dev.parent,
		.regmap = axp20x->regmap,
	};
	int ret, i;
	u32 workmode;

	/* This only sets the dcdc freq. Ignore any errors */
	axp20x_regulator_parse_dt(pdev);

	for (i = 0; i < AXP20X_REG_ID_MAX; i++) {
		rdev = devm_regulator_register(&pdev->dev, &axp20x_regulators[i],
					       &config);
		if (IS_ERR(rdev)) {
			dev_err(&pdev->dev, "Failed to register %s\n",
				axp20x_regulators[i].name);

			return PTR_ERR(rdev);
		}

		ret = of_property_read_u32(rdev->dev.of_node,
					   "x-powers,dcdc-workmode",
					   &workmode);
		if (!ret) {
			if (axp20x_set_dcdc_workmode(rdev, i, workmode))
				dev_err(&pdev->dev, "Failed to set workmode on %s\n",
					axp20x_regulators[i].name);
		}
	}

	return 0;
}

static struct platform_driver axp20x_regulator_driver = {
	.probe	= axp20x_regulator_probe,
	.driver	= {
		.name		= "axp20x-regulator",
	},
};

module_platform_driver(axp20x_regulator_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Carlo Caione <carlo@caione.org>");
MODULE_DESCRIPTION("Regulator Driver for AXP20X PMIC");
