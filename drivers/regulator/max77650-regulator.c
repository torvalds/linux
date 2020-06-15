// SPDX-License-Identifier: GPL-2.0
//
// Copyright (C) 2018 BayLibre SAS
// Author: Bartosz Golaszewski <bgolaszewski@baylibre.com>
//
// Regulator driver for MAXIM 77650/77651 charger/power-supply.

#include <linux/of.h>
#include <linux/mfd/max77650.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/regulator/driver.h>

#define MAX77650_REGULATOR_EN_CTRL_MASK		GENMASK(3, 0)
#define MAX77650_REGULATOR_EN_CTRL_BITS(_reg) \
		((_reg) & MAX77650_REGULATOR_EN_CTRL_MASK)
#define MAX77650_REGULATOR_ENABLED		GENMASK(2, 1)
#define MAX77650_REGULATOR_DISABLED		BIT(2)

#define MAX77650_REGULATOR_V_LDO_MASK		GENMASK(6, 0)
#define MAX77650_REGULATOR_V_SBB_MASK		GENMASK(5, 0)
#define MAX77651_REGULATOR_V_SBB1_MASK		GENMASK(5, 2)
#define MAX77651_REGULATOR_V_SBB1_RANGE_MASK	GENMASK(1, 0)

#define MAX77650_REGULATOR_AD_MASK		BIT(3)
#define MAX77650_REGULATOR_AD_DISABLED		0x00
#define MAX77650_REGULATOR_AD_ENABLED		BIT(3)

#define MAX77650_REGULATOR_CURR_LIM_MASK	GENMASK(7, 6)

enum {
	MAX77650_REGULATOR_ID_LDO = 0,
	MAX77650_REGULATOR_ID_SBB0,
	MAX77650_REGULATOR_ID_SBB1,
	MAX77650_REGULATOR_ID_SBB2,
	MAX77650_REGULATOR_NUM_REGULATORS,
};

struct max77650_regulator_desc {
	struct regulator_desc desc;
	unsigned int regA;
	unsigned int regB;
};

static struct max77650_regulator_desc max77651_SBB1_desc;

static const unsigned int max77651_sbb1_volt_range_sel[] = {
	0x0, 0x1, 0x2, 0x3
};

static const struct linear_range max77651_sbb1_volt_ranges[] = {
	/* range index 0 */
	REGULATOR_LINEAR_RANGE(2400000, 0x00, 0x0f, 50000),
	/* range index 1 */
	REGULATOR_LINEAR_RANGE(3200000, 0x00, 0x0f, 50000),
	/* range index 2 */
	REGULATOR_LINEAR_RANGE(4000000, 0x00, 0x0f, 50000),
	/* range index 3 */
	REGULATOR_LINEAR_RANGE(4800000, 0x00, 0x09, 50000),
};

static const unsigned int max77650_current_limit_table[] = {
	1000000, 866000, 707000, 500000,
};

static int max77650_regulator_is_enabled(struct regulator_dev *rdev)
{
	struct max77650_regulator_desc *rdesc;
	struct regmap *map;
	int val, rv, en;

	rdesc = rdev_get_drvdata(rdev);
	map = rdev_get_regmap(rdev);

	rv = regmap_read(map, rdesc->regB, &val);
	if (rv)
		return rv;

	en = MAX77650_REGULATOR_EN_CTRL_BITS(val);

	return en != MAX77650_REGULATOR_DISABLED;
}

static int max77650_regulator_enable(struct regulator_dev *rdev)
{
	struct max77650_regulator_desc *rdesc;
	struct regmap *map;

	rdesc = rdev_get_drvdata(rdev);
	map = rdev_get_regmap(rdev);

	return regmap_update_bits(map, rdesc->regB,
				  MAX77650_REGULATOR_EN_CTRL_MASK,
				  MAX77650_REGULATOR_ENABLED);
}

static int max77650_regulator_disable(struct regulator_dev *rdev)
{
	struct max77650_regulator_desc *rdesc;
	struct regmap *map;

	rdesc = rdev_get_drvdata(rdev);
	map = rdev_get_regmap(rdev);

	return regmap_update_bits(map, rdesc->regB,
				  MAX77650_REGULATOR_EN_CTRL_MASK,
				  MAX77650_REGULATOR_DISABLED);
}

static const struct regulator_ops max77650_regulator_LDO_ops = {
	.is_enabled		= max77650_regulator_is_enabled,
	.enable			= max77650_regulator_enable,
	.disable		= max77650_regulator_disable,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

static const struct regulator_ops max77650_regulator_SBB_ops = {
	.is_enabled		= max77650_regulator_is_enabled,
	.enable			= max77650_regulator_enable,
	.disable		= max77650_regulator_disable,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

/* Special case for max77651 SBB1 - pickable linear-range voltage mapping. */
static const struct regulator_ops max77651_SBB1_regulator_ops = {
	.is_enabled		= max77650_regulator_is_enabled,
	.enable			= max77650_regulator_enable,
	.disable		= max77650_regulator_disable,
	.list_voltage		= regulator_list_voltage_pickable_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_pickable_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_pickable_regmap,
	.get_current_limit	= regulator_get_current_limit_regmap,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

static struct max77650_regulator_desc max77650_LDO_desc = {
	.desc = {
		.name			= "ldo",
		.of_match		= of_match_ptr("ldo"),
		.regulators_node	= of_match_ptr("regulators"),
		.supply_name		= "in-ldo",
		.id			= MAX77650_REGULATOR_ID_LDO,
		.ops			= &max77650_regulator_LDO_ops,
		.min_uV			= 1350000,
		.uV_step		= 12500,
		.n_voltages		= 128,
		.vsel_step		= 1,
		.vsel_mask		= MAX77650_REGULATOR_V_LDO_MASK,
		.vsel_reg		= MAX77650_REG_CNFG_LDO_A,
		.active_discharge_off	= MAX77650_REGULATOR_AD_DISABLED,
		.active_discharge_on	= MAX77650_REGULATOR_AD_ENABLED,
		.active_discharge_mask	= MAX77650_REGULATOR_AD_MASK,
		.active_discharge_reg	= MAX77650_REG_CNFG_LDO_B,
		.enable_time		= 100,
		.type			= REGULATOR_VOLTAGE,
		.owner			= THIS_MODULE,
	},
	.regA		= MAX77650_REG_CNFG_LDO_A,
	.regB		= MAX77650_REG_CNFG_LDO_B,
};

static struct max77650_regulator_desc max77650_SBB0_desc = {
	.desc = {
		.name			= "sbb0",
		.of_match		= of_match_ptr("sbb0"),
		.regulators_node	= of_match_ptr("regulators"),
		.supply_name		= "in-sbb0",
		.id			= MAX77650_REGULATOR_ID_SBB0,
		.ops			= &max77650_regulator_SBB_ops,
		.min_uV			= 800000,
		.uV_step		= 25000,
		.n_voltages		= 64,
		.vsel_step		= 1,
		.vsel_mask		= MAX77650_REGULATOR_V_SBB_MASK,
		.vsel_reg		= MAX77650_REG_CNFG_SBB0_A,
		.active_discharge_off	= MAX77650_REGULATOR_AD_DISABLED,
		.active_discharge_on	= MAX77650_REGULATOR_AD_ENABLED,
		.active_discharge_mask	= MAX77650_REGULATOR_AD_MASK,
		.active_discharge_reg	= MAX77650_REG_CNFG_SBB0_B,
		.enable_time		= 100,
		.type			= REGULATOR_VOLTAGE,
		.owner			= THIS_MODULE,
		.csel_reg		= MAX77650_REG_CNFG_SBB0_A,
		.csel_mask		= MAX77650_REGULATOR_CURR_LIM_MASK,
		.curr_table		= max77650_current_limit_table,
		.n_current_limits = ARRAY_SIZE(max77650_current_limit_table),
	},
	.regA		= MAX77650_REG_CNFG_SBB0_A,
	.regB		= MAX77650_REG_CNFG_SBB0_B,
};

static struct max77650_regulator_desc max77650_SBB1_desc = {
	.desc = {
		.name			= "sbb1",
		.of_match		= of_match_ptr("sbb1"),
		.regulators_node	= of_match_ptr("regulators"),
		.supply_name		= "in-sbb1",
		.id			= MAX77650_REGULATOR_ID_SBB1,
		.ops			= &max77650_regulator_SBB_ops,
		.min_uV			= 800000,
		.uV_step		= 12500,
		.n_voltages		= 64,
		.vsel_step		= 1,
		.vsel_mask		= MAX77650_REGULATOR_V_SBB_MASK,
		.vsel_reg		= MAX77650_REG_CNFG_SBB1_A,
		.active_discharge_off	= MAX77650_REGULATOR_AD_DISABLED,
		.active_discharge_on	= MAX77650_REGULATOR_AD_ENABLED,
		.active_discharge_mask	= MAX77650_REGULATOR_AD_MASK,
		.active_discharge_reg	= MAX77650_REG_CNFG_SBB1_B,
		.enable_time		= 100,
		.type			= REGULATOR_VOLTAGE,
		.owner			= THIS_MODULE,
		.csel_reg		= MAX77650_REG_CNFG_SBB1_A,
		.csel_mask		= MAX77650_REGULATOR_CURR_LIM_MASK,
		.curr_table		= max77650_current_limit_table,
		.n_current_limits = ARRAY_SIZE(max77650_current_limit_table),
	},
	.regA		= MAX77650_REG_CNFG_SBB1_A,
	.regB		= MAX77650_REG_CNFG_SBB1_B,
};

static struct max77650_regulator_desc max77651_SBB1_desc = {
	.desc = {
		.name			= "sbb1",
		.of_match		= of_match_ptr("sbb1"),
		.regulators_node	= of_match_ptr("regulators"),
		.supply_name		= "in-sbb1",
		.id			= MAX77650_REGULATOR_ID_SBB1,
		.ops			= &max77651_SBB1_regulator_ops,
		.linear_range_selectors	= max77651_sbb1_volt_range_sel,
		.linear_ranges		= max77651_sbb1_volt_ranges,
		.n_linear_ranges	= ARRAY_SIZE(max77651_sbb1_volt_ranges),
		.n_voltages		= 58,
		.vsel_step		= 1,
		.vsel_range_mask	= MAX77651_REGULATOR_V_SBB1_RANGE_MASK,
		.vsel_range_reg		= MAX77650_REG_CNFG_SBB1_A,
		.vsel_mask		= MAX77651_REGULATOR_V_SBB1_MASK,
		.vsel_reg		= MAX77650_REG_CNFG_SBB1_A,
		.active_discharge_off	= MAX77650_REGULATOR_AD_DISABLED,
		.active_discharge_on	= MAX77650_REGULATOR_AD_ENABLED,
		.active_discharge_mask	= MAX77650_REGULATOR_AD_MASK,
		.active_discharge_reg	= MAX77650_REG_CNFG_SBB1_B,
		.enable_time		= 100,
		.type			= REGULATOR_VOLTAGE,
		.owner			= THIS_MODULE,
		.csel_reg		= MAX77650_REG_CNFG_SBB1_A,
		.csel_mask		= MAX77650_REGULATOR_CURR_LIM_MASK,
		.curr_table		= max77650_current_limit_table,
		.n_current_limits = ARRAY_SIZE(max77650_current_limit_table),
	},
	.regA		= MAX77650_REG_CNFG_SBB1_A,
	.regB		= MAX77650_REG_CNFG_SBB1_B,
};

static struct max77650_regulator_desc max77650_SBB2_desc = {
	.desc = {
		.name			= "sbb2",
		.of_match		= of_match_ptr("sbb2"),
		.regulators_node	= of_match_ptr("regulators"),
		.supply_name		= "in-sbb0",
		.id			= MAX77650_REGULATOR_ID_SBB2,
		.ops			= &max77650_regulator_SBB_ops,
		.min_uV			= 800000,
		.uV_step		= 50000,
		.n_voltages		= 64,
		.vsel_step		= 1,
		.vsel_mask		= MAX77650_REGULATOR_V_SBB_MASK,
		.vsel_reg		= MAX77650_REG_CNFG_SBB2_A,
		.active_discharge_off	= MAX77650_REGULATOR_AD_DISABLED,
		.active_discharge_on	= MAX77650_REGULATOR_AD_ENABLED,
		.active_discharge_mask	= MAX77650_REGULATOR_AD_MASK,
		.active_discharge_reg	= MAX77650_REG_CNFG_SBB2_B,
		.enable_time		= 100,
		.type			= REGULATOR_VOLTAGE,
		.owner			= THIS_MODULE,
		.csel_reg		= MAX77650_REG_CNFG_SBB2_A,
		.csel_mask		= MAX77650_REGULATOR_CURR_LIM_MASK,
		.curr_table		= max77650_current_limit_table,
		.n_current_limits = ARRAY_SIZE(max77650_current_limit_table),
	},
	.regA		= MAX77650_REG_CNFG_SBB2_A,
	.regB		= MAX77650_REG_CNFG_SBB2_B,
};

static struct max77650_regulator_desc max77651_SBB2_desc = {
	.desc = {
		.name			= "sbb2",
		.of_match		= of_match_ptr("sbb2"),
		.regulators_node	= of_match_ptr("regulators"),
		.supply_name		= "in-sbb0",
		.id			= MAX77650_REGULATOR_ID_SBB2,
		.ops			= &max77650_regulator_SBB_ops,
		.min_uV			= 2400000,
		.uV_step		= 50000,
		.n_voltages		= 64,
		.vsel_step		= 1,
		.vsel_mask		= MAX77650_REGULATOR_V_SBB_MASK,
		.vsel_reg		= MAX77650_REG_CNFG_SBB2_A,
		.active_discharge_off	= MAX77650_REGULATOR_AD_DISABLED,
		.active_discharge_on	= MAX77650_REGULATOR_AD_ENABLED,
		.active_discharge_mask	= MAX77650_REGULATOR_AD_MASK,
		.active_discharge_reg	= MAX77650_REG_CNFG_SBB2_B,
		.enable_time		= 100,
		.type			= REGULATOR_VOLTAGE,
		.owner			= THIS_MODULE,
		.csel_reg		= MAX77650_REG_CNFG_SBB2_A,
		.csel_mask		= MAX77650_REGULATOR_CURR_LIM_MASK,
		.curr_table		= max77650_current_limit_table,
		.n_current_limits = ARRAY_SIZE(max77650_current_limit_table),
	},
	.regA		= MAX77650_REG_CNFG_SBB2_A,
	.regB		= MAX77650_REG_CNFG_SBB2_B,
};

static int max77650_regulator_probe(struct platform_device *pdev)
{
	struct max77650_regulator_desc **rdescs;
	struct max77650_regulator_desc *rdesc;
	struct regulator_config config = { };
	struct device *dev, *parent;
	struct regulator_dev *rdev;
	struct regmap *map;
	unsigned int val;
	int i, rv;

	dev = &pdev->dev;
	parent = dev->parent;

	if (!dev->of_node)
		dev->of_node = parent->of_node;

	rdescs = devm_kcalloc(dev, MAX77650_REGULATOR_NUM_REGULATORS,
			      sizeof(*rdescs), GFP_KERNEL);
	if (!rdescs)
		return -ENOMEM;

	map = dev_get_regmap(parent, NULL);
	if (!map)
		return -ENODEV;

	rv = regmap_read(map, MAX77650_REG_CID, &val);
	if (rv)
		return rv;

	rdescs[MAX77650_REGULATOR_ID_LDO] = &max77650_LDO_desc;
	rdescs[MAX77650_REGULATOR_ID_SBB0] = &max77650_SBB0_desc;

	switch (MAX77650_CID_BITS(val)) {
	case MAX77650_CID_77650A:
	case MAX77650_CID_77650C:
		rdescs[MAX77650_REGULATOR_ID_SBB1] = &max77650_SBB1_desc;
		rdescs[MAX77650_REGULATOR_ID_SBB2] = &max77650_SBB2_desc;
		break;
	case MAX77650_CID_77651A:
	case MAX77650_CID_77651B:
		rdescs[MAX77650_REGULATOR_ID_SBB1] = &max77651_SBB1_desc;
		rdescs[MAX77650_REGULATOR_ID_SBB2] = &max77651_SBB2_desc;
		break;
	default:
		return -ENODEV;
	}

	config.dev = parent;

	for (i = 0; i < MAX77650_REGULATOR_NUM_REGULATORS; i++) {
		rdesc = rdescs[i];
		config.driver_data = rdesc;

		rdev = devm_regulator_register(dev, &rdesc->desc, &config);
		if (IS_ERR(rdev))
			return PTR_ERR(rdev);
	}

	return 0;
}

static const struct of_device_id max77650_regulator_of_match[] = {
	{ .compatible = "maxim,max77650-regulator" },
	{ }
};
MODULE_DEVICE_TABLE(of, max77650_regulator_of_match);

static struct platform_driver max77650_regulator_driver = {
	.driver = {
		.name = "max77650-regulator",
		.of_match_table = max77650_regulator_of_match,
	},
	.probe = max77650_regulator_probe,
};
module_platform_driver(max77650_regulator_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 regulator driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
MODULE_ALIAS("platform:max77650-regulator");
