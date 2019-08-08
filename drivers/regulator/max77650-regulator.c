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

static const unsigned int max77651_sbb1_regulator_volt_table[] = {
	2400000, 3200000, 4000000, 4800000,
	2450000, 3250000, 4050000, 4850000,
	2500000, 3300000, 4100000, 4900000,
	2550000, 3350000, 4150000, 4950000,
	2600000, 3400000, 4200000, 5000000,
	2650000, 3450000, 4250000, 5050000,
	2700000, 3500000, 4300000, 5100000,
	2750000, 3550000, 4350000, 5150000,
	2800000, 3600000, 4400000, 5200000,
	2850000, 3650000, 4450000, 5250000,
	2900000, 3700000, 4500000,       0,
	2950000, 3750000, 4550000,       0,
	3000000, 3800000, 4600000,       0,
	3050000, 3850000, 4650000,       0,
	3100000, 3900000, 4700000,       0,
	3150000, 3950000, 4750000,       0,
};

#define MAX77651_REGULATOR_SBB1_SEL_DEC(_val) \
		(((_val & 0x3c) >> 2) | ((_val & 0x03) << 4))
#define MAX77651_REGULATOR_SBB1_SEL_ENC(_val) \
		(((_val & 0x30) >> 4) | ((_val & 0x0f) << 2))

#define MAX77650_REGULATOR_SBB1_SEL_DECR(_val)				\
	do {								\
		_val = MAX77651_REGULATOR_SBB1_SEL_DEC(_val);		\
		_val--;							\
		_val = MAX77651_REGULATOR_SBB1_SEL_ENC(_val);		\
	} while (0)

#define MAX77650_REGULATOR_SBB1_SEL_INCR(_val)				\
	do {								\
		_val = MAX77651_REGULATOR_SBB1_SEL_DEC(_val);		\
		_val++;							\
		_val = MAX77651_REGULATOR_SBB1_SEL_ENC(_val);		\
	} while (0)

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

static int max77650_regulator_set_voltage_sel(struct regulator_dev *rdev,
					      unsigned int sel)
{
	int rv = 0, curr, diff;
	bool ascending;

	/*
	 * If the regulator is disabled, we can program the desired
	 * voltage right away.
	 */
	if (!max77650_regulator_is_enabled(rdev))
		return regulator_set_voltage_sel_regmap(rdev, sel);

	/*
	 * Otherwise we need to manually ramp the output voltage up/down
	 * one step at a time.
	 */

	curr = regulator_get_voltage_sel_regmap(rdev);
	if (curr < 0)
		return curr;

	diff = curr - sel;
	if (diff == 0)
		return 0; /* Already there. */
	else if (diff > 0)
		ascending = false;
	else
		ascending = true;

	/*
	 * Make sure we'll get to the right voltage and break the loop even if
	 * the selector equals 0.
	 */
	for (ascending ? curr++ : curr--;; ascending ? curr++ : curr--) {
		rv = regulator_set_voltage_sel_regmap(rdev, curr);
		if (rv)
			return rv;

		if (curr == sel)
			break;
	}

	return 0;
}

/*
 * Special case: non-linear voltage table for max77651 SBB1 - software
 * must ensure the voltage is ramped in 50mV increments.
 */
static int max77651_regulator_sbb1_set_voltage_sel(struct regulator_dev *rdev,
						   unsigned int sel)
{
	int rv = 0, curr, vcurr, vdest, vdiff;

	/*
	 * If the regulator is disabled, we can program the desired
	 * voltage right away.
	 */
	if (!max77650_regulator_is_enabled(rdev))
		return regulator_set_voltage_sel_regmap(rdev, sel);

	curr = regulator_get_voltage_sel_regmap(rdev);
	if (curr < 0)
		return curr;

	if (curr == sel)
		return 0; /* Already there. */

	vcurr = max77651_sbb1_regulator_volt_table[curr];
	vdest = max77651_sbb1_regulator_volt_table[sel];
	vdiff = vcurr - vdest;

	for (;;) {
		if (vdiff > 0)
			MAX77650_REGULATOR_SBB1_SEL_DECR(curr);
		else
			MAX77650_REGULATOR_SBB1_SEL_INCR(curr);

		rv = regulator_set_voltage_sel_regmap(rdev, curr);
		if (rv)
			return rv;

		if (curr == sel)
			break;
	};

	return 0;
}

static const struct regulator_ops max77650_regulator_LDO_ops = {
	.is_enabled		= max77650_regulator_is_enabled,
	.enable			= max77650_regulator_enable,
	.disable		= max77650_regulator_disable,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= max77650_regulator_set_voltage_sel,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

static const struct regulator_ops max77650_regulator_SBB_ops = {
	.is_enabled		= max77650_regulator_is_enabled,
	.enable			= max77650_regulator_enable,
	.disable		= max77650_regulator_disable,
	.list_voltage		= regulator_list_voltage_linear,
	.map_voltage		= regulator_map_voltage_linear,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= max77650_regulator_set_voltage_sel,
	.get_current_limit	= regulator_get_current_limit_regmap,
	.set_current_limit	= regulator_set_current_limit_regmap,
	.set_active_discharge	= regulator_set_active_discharge_regmap,
};

/* Special case for max77651 SBB1 - non-linear voltage mapping. */
static const struct regulator_ops max77651_SBB1_regulator_ops = {
	.is_enabled		= max77650_regulator_is_enabled,
	.enable			= max77650_regulator_enable,
	.disable		= max77650_regulator_disable,
	.list_voltage		= regulator_list_voltage_table,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.set_voltage_sel	= max77651_regulator_sbb1_set_voltage_sel,
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
		.volt_table		= max77651_sbb1_regulator_volt_table,
		.n_voltages = ARRAY_SIZE(max77651_sbb1_regulator_volt_table),
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

static struct platform_driver max77650_regulator_driver = {
	.driver = {
		.name = "max77650-regulator",
	},
	.probe = max77650_regulator_probe,
};
module_platform_driver(max77650_regulator_driver);

MODULE_DESCRIPTION("MAXIM 77650/77651 regulator driver");
MODULE_AUTHOR("Bartosz Golaszewski <bgolaszewski@baylibre.com>");
MODULE_LICENSE("GPL v2");
