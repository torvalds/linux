// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2015 Texas Instruments Incorporated - https://www.ti.com/
 *
 * Author: Andrew F. Davis <afd@ti.com>
 *
 * Based on the TPS65912 driver
 */

#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regulator/driver.h>

#include <linux/mfd/tps65086.h>

enum tps65086_regulators { BUCK1, BUCK2, BUCK3, BUCK4, BUCK5, BUCK6, LDOA1,
	LDOA2, LDOA3, VTT, SWA1, SWB1, SWB2 };

/* Selector for regulator configuration regarding PMIC chip ID. */
enum tps65086_ids {
	TPS6508640 = 0,
	TPS65086401,
	TPS6508641,
	TPS65086470,
};

#define TPS65086_REGULATOR(_name, _of, _id, _nv, _vr, _vm, _er, _em, _lr, _dr, _dm)	\
	[_id] = {							\
		.desc = {						\
			.name			= _name,		\
			.of_match		= of_match_ptr(_of),	\
			.regulators_node	= "regulators",		\
			.of_parse_cb		= tps65086_of_parse_cb,	\
			.id			= _id,			\
			.ops			= &reg_ops,		\
			.n_voltages		= _nv,			\
			.type			= REGULATOR_VOLTAGE,	\
			.owner			= THIS_MODULE,		\
			.vsel_reg		= _vr,			\
			.vsel_mask		= _vm,			\
			.enable_reg		= _er,			\
			.enable_mask		= _em,			\
			.volt_table		= NULL,			\
			.linear_ranges		= _lr,			\
			.n_linear_ranges	= ARRAY_SIZE(_lr),	\
		},							\
		.decay_reg = _dr,					\
		.decay_mask = _dm,					\
	}

#define TPS65086_SWITCH(_name, _of, _id, _er, _em)			\
	[_id] = {							\
		.desc = {						\
			.name			= _name,		\
			.of_match		= of_match_ptr(_of),	\
			.regulators_node	= "regulators",		\
			.of_parse_cb		= tps65086_of_parse_cb,	\
			.id			= _id,			\
			.ops			= &switch_ops,		\
			.type			= REGULATOR_VOLTAGE,	\
			.owner			= THIS_MODULE,		\
			.enable_reg		= _er,			\
			.enable_mask		= _em,			\
		},							\
	}


#define TPS65086_REGULATOR_CONFIG(_chip_id, _config)			\
	[_chip_id] = {							\
		.config = _config,					\
		.num_elems = ARRAY_SIZE(_config),			\
	}

struct tps65086_regulator {
	struct regulator_desc desc;
	unsigned int decay_reg;
	unsigned int decay_mask;
};

struct tps65086_regulator_config {
	struct tps65086_regulator * const config;
	const unsigned int num_elems;
};

static const struct linear_range tps65086_10mv_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(410000, 0x1, 0x7F, 10000),
};

static const struct linear_range tps65086_buck126_25mv_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(1000000, 0x1, 0x18, 0),
	REGULATOR_LINEAR_RANGE(1025000, 0x19, 0x7F, 25000),
};

static const struct linear_range tps65086_buck345_25mv_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(425000, 0x1, 0x7F, 25000),
};

static const struct linear_range tps65086_ldoa1_ranges[] = {
	REGULATOR_LINEAR_RANGE(1350000, 0x0, 0x0, 0),
	REGULATOR_LINEAR_RANGE(1500000, 0x1, 0x7, 100000),
	REGULATOR_LINEAR_RANGE(2300000, 0x8, 0xB, 100000),
	REGULATOR_LINEAR_RANGE(2850000, 0xC, 0xD, 150000),
	REGULATOR_LINEAR_RANGE(3300000, 0xE, 0xE, 0),
};

static const struct linear_range tps65086_ldoa23_ranges[] = {
	REGULATOR_LINEAR_RANGE(700000, 0x0, 0xD, 50000),
	REGULATOR_LINEAR_RANGE(1400000, 0xE, 0xF, 100000),
};

/* Operations permitted on regulators */
static const struct regulator_ops reg_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
	.set_voltage_sel	= regulator_set_voltage_sel_regmap,
	.map_voltage		= regulator_map_voltage_linear_range,
	.get_voltage_sel	= regulator_get_voltage_sel_regmap,
	.list_voltage		= regulator_list_voltage_linear_range,
};

/* Operations permitted on load switches */
static const struct regulator_ops switch_ops = {
	.enable			= regulator_enable_regmap,
	.disable		= regulator_disable_regmap,
	.is_enabled		= regulator_is_enabled_regmap,
};

static int tps65086_of_parse_cb(struct device_node *dev,
				const struct regulator_desc *desc,
				struct regulator_config *config);

static struct tps65086_regulator tps6508640_regulator_config[] = {
	TPS65086_REGULATOR("BUCK1", "buck1", BUCK1, 0x80, TPS65086_BUCK1CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK1CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK2", "buck2", BUCK2, 0x80, TPS65086_BUCK2CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(1),
			   tps65086_10mv_ranges, TPS65086_BUCK2CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK3", "buck3", BUCK3, 0x80, TPS65086_BUCK3VID,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(2),
			   tps65086_10mv_ranges, TPS65086_BUCK3DECAY,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK4", "buck4", BUCK4, 0x80, TPS65086_BUCK4VID,
			   BUCK_VID_MASK, TPS65086_BUCK4CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK4VID,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK5", "buck5", BUCK5, 0x80, TPS65086_BUCK5VID,
			   BUCK_VID_MASK, TPS65086_BUCK5CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK5CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK6", "buck6", BUCK6, 0x80, TPS65086_BUCK6VID,
			   BUCK_VID_MASK, TPS65086_BUCK6CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK6CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("LDOA1", "ldoa1", LDOA1, 0xF, TPS65086_LDOA1CTRL,
			   VDOA1_VID_MASK, TPS65086_SWVTT_EN, BIT(7),
			   tps65086_ldoa1_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA2", "ldoa2", LDOA2, 0x10, TPS65086_LDOA2VID,
			   VDOA23_VID_MASK, TPS65086_LDOA2CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA3", "ldoa3", LDOA3, 0x10, TPS65086_LDOA3VID,
			   VDOA23_VID_MASK, TPS65086_LDOA3CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_SWITCH("VTT", "vtt", VTT, TPS65086_SWVTT_EN, BIT(4)),
	TPS65086_SWITCH("SWA1", "swa1", SWA1, TPS65086_SWVTT_EN, BIT(5)),
	TPS65086_SWITCH("SWB1", "swb1", SWB1, TPS65086_SWVTT_EN, BIT(6)),
	TPS65086_SWITCH("SWB2", "swb2", SWB2, TPS65086_LDOA1CTRL, BIT(0)),
};

static struct tps65086_regulator tps65086401_regulator_config[] = {
	TPS65086_REGULATOR("BUCK1", "buck1", BUCK1, 0x80, TPS65086_BUCK1CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK1CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK2", "buck2", BUCK2, 0x80, TPS65086_BUCK2CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(1),
			   tps65086_10mv_ranges, TPS65086_BUCK2CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK3", "buck3", BUCK3, 0x80, TPS65086_BUCK3VID,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(2),
			   tps65086_10mv_ranges, TPS65086_BUCK3DECAY,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK4", "buck4", BUCK4, 0x80, TPS65086_BUCK4VID,
			   BUCK_VID_MASK, TPS65086_BUCK4CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK4VID,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK5", "buck5", BUCK5, 0x80, TPS65086_BUCK5VID,
			   BUCK_VID_MASK, TPS65086_BUCK5CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK5CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK6", "buck6", BUCK6, 0x80, TPS65086_BUCK6VID,
			   BUCK_VID_MASK, TPS65086_BUCK6CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK6CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("LDOA1", "ldoa1", LDOA1, 0xF, TPS65086_LDOA1CTRL,
			   VDOA1_VID_MASK, TPS65086_SWVTT_EN, BIT(7),
			   tps65086_ldoa1_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA2", "ldoa2", LDOA2, 0x10, TPS65086_LDOA2VID,
			   VDOA23_VID_MASK, TPS65086_LDOA2CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA3", "ldoa3", LDOA3, 0x10, TPS65086_LDOA3VID,
			   VDOA23_VID_MASK, TPS65086_LDOA3CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_SWITCH("VTT", "vtt", VTT, TPS65086_SWVTT_EN, BIT(4)),
	TPS65086_SWITCH("SWA1", "swa1", SWA1, TPS65086_SWVTT_EN, BIT(5)),
	TPS65086_SWITCH("SWB1", "swb1", SWB1, TPS65086_SWVTT_EN, BIT(6)),
};

static struct tps65086_regulator tps6508641_regulator_config[] = {
	TPS65086_REGULATOR("BUCK1", "buck1", BUCK1, 0x80, TPS65086_BUCK1CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK1CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK2", "buck2", BUCK2, 0x80, TPS65086_BUCK2CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(1),
			   tps65086_10mv_ranges, TPS65086_BUCK2CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK3", "buck3", BUCK3, 0x80, TPS65086_BUCK3VID,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(2),
			   tps65086_10mv_ranges, TPS65086_BUCK3DECAY,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK4", "buck4", BUCK4, 0x80, TPS65086_BUCK4VID,
			   BUCK_VID_MASK, TPS65086_BUCK4CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK4VID,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK5", "buck5", BUCK5, 0x80, TPS65086_BUCK5VID,
			   BUCK_VID_MASK, TPS65086_BUCK5CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK5CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK6", "buck6", BUCK6, 0x80, TPS65086_BUCK6VID,
			   BUCK_VID_MASK, TPS65086_BUCK6CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK6CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("LDOA1", "ldoa1", LDOA1, 0xF, TPS65086_LDOA1CTRL,
			   VDOA1_VID_MASK, TPS65086_SWVTT_EN, BIT(7),
			   tps65086_ldoa1_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA2", "ldoa2", LDOA2, 0x10, TPS65086_LDOA2VID,
			   VDOA23_VID_MASK, TPS65086_LDOA2CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA3", "ldoa3", LDOA3, 0x10, TPS65086_LDOA3VID,
			   VDOA23_VID_MASK, TPS65086_LDOA3CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_SWITCH("VTT", "vtt", VTT, TPS65086_SWVTT_EN, BIT(4)),
	TPS65086_SWITCH("SWA1", "swa1", SWA1, TPS65086_SWVTT_EN, BIT(5)),
	TPS65086_SWITCH("SWB1", "swb1", SWB1, TPS65086_SWVTT_EN, BIT(6)),
};

static struct tps65086_regulator tps65086470_regulator_config[] = {
	TPS65086_REGULATOR("BUCK1", "buck1", BUCK1, 0x80, TPS65086_BUCK1CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK1CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK2", "buck2", BUCK2, 0x80, TPS65086_BUCK2CTRL,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(1),
			   tps65086_10mv_ranges, TPS65086_BUCK2CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK3", "buck3", BUCK3, 0x80, TPS65086_BUCK3VID,
			   BUCK_VID_MASK, TPS65086_BUCK123CTRL, BIT(2),
			   tps65086_10mv_ranges, TPS65086_BUCK3DECAY,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK4", "buck4", BUCK4, 0x80, TPS65086_BUCK4VID,
			   BUCK_VID_MASK, TPS65086_BUCK4CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK4VID,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK5", "buck5", BUCK5, 0x80, TPS65086_BUCK5VID,
			   BUCK_VID_MASK, TPS65086_BUCK5CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK5CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("BUCK6", "buck6", BUCK6, 0x80, TPS65086_BUCK6VID,
			   BUCK_VID_MASK, TPS65086_BUCK6CTRL, BIT(0),
			   tps65086_10mv_ranges, TPS65086_BUCK6CTRL,
			   BIT(0)),
	TPS65086_REGULATOR("LDOA1", "ldoa1", LDOA1, 0xF, TPS65086_LDOA1CTRL,
			   VDOA1_VID_MASK, TPS65086_LDOA1CTRL, BIT(0),
			   tps65086_ldoa1_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA2", "ldoa2", LDOA2, 0x10, TPS65086_LDOA2VID,
			   VDOA23_VID_MASK, TPS65086_LDOA2CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_REGULATOR("LDOA3", "ldoa3", LDOA3, 0x10, TPS65086_LDOA3VID,
			   VDOA23_VID_MASK, TPS65086_LDOA3CTRL, BIT(0),
			   tps65086_ldoa23_ranges, 0, 0),
	TPS65086_SWITCH("VTT", "vtt", VTT, TPS65086_SWVTT_EN, BIT(4)),
	TPS65086_SWITCH("SWA1", "swa1", SWA1, TPS65086_SWVTT_EN, BIT(5)),
	TPS65086_SWITCH("SWB1", "swb1", SWB1, TPS65086_SWVTT_EN, BIT(6)),
	TPS65086_SWITCH("SWB2", "swb2", SWB2, TPS65086_SWVTT_EN, BIT(7)),
};

static const struct tps65086_regulator_config regulator_configs[] = {
	TPS65086_REGULATOR_CONFIG(TPS6508640, tps6508640_regulator_config),
	TPS65086_REGULATOR_CONFIG(TPS65086401, tps65086401_regulator_config),
	TPS65086_REGULATOR_CONFIG(TPS6508641, tps6508641_regulator_config),
	TPS65086_REGULATOR_CONFIG(TPS65086470, tps65086470_regulator_config)
};

static int tps65086_of_parse_cb(struct device_node *node,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct tps65086 * const tps = dev_get_drvdata(config->dev);
	struct tps65086_regulator *regulators = tps->reg_config->config;
	int ret;

	/* Check for 25mV step mode */
	if (of_property_read_bool(node, "ti,regulator-step-size-25mv")) {
		switch (desc->id) {
		case BUCK1:
		case BUCK2:
		case BUCK6:
			regulators[desc->id].desc.linear_ranges =
				tps65086_buck126_25mv_ranges;
			regulators[desc->id].desc.n_linear_ranges =
				ARRAY_SIZE(tps65086_buck126_25mv_ranges);
			break;
		case BUCK3:
		case BUCK4:
		case BUCK5:
			regulators[desc->id].desc.linear_ranges =
				tps65086_buck345_25mv_ranges;
			regulators[desc->id].desc.n_linear_ranges =
				ARRAY_SIZE(tps65086_buck345_25mv_ranges);
			break;
		default:
			dev_warn(config->dev, "25mV step mode only valid for BUCK regulators\n");
		}
	}

	/* Check for decay mode */
	if (desc->id <= BUCK6 && of_property_read_bool(node, "ti,regulator-decay")) {
		ret = regmap_write_bits(config->regmap,
					regulators[desc->id].decay_reg,
					regulators[desc->id].decay_mask,
					regulators[desc->id].decay_mask);
		if (ret) {
			dev_err(config->dev, "Error setting decay\n");
			return ret;
		}
	}

	return 0;
}

static int tps65086_regulator_probe(struct platform_device *pdev)
{
	struct tps65086 *tps = dev_get_drvdata(pdev->dev.parent);
	struct regulator_config config = { };
	unsigned int selector_reg_config;
	struct regulator_dev *rdev;
	int i;

	/* Select regulator configuration for used PMIC device */
	switch (tps->chip_id) {
	case TPS6508640_ID:
		selector_reg_config = TPS6508640;
		break;
	case TPS65086401_ID:
		selector_reg_config = TPS65086401;
		break;
	case TPS6508641_ID:
		selector_reg_config = TPS6508641;
		break;
	case TPS65086470_ID:
		selector_reg_config = TPS65086470;
		break;
	default:
		dev_err(tps->dev, "Unknown device ID. Cannot determine regulator config.\n");
		return -ENODEV;
	}
	tps->reg_config = &regulator_configs[selector_reg_config];

	platform_set_drvdata(pdev, tps);

	config.dev = &pdev->dev;
	config.dev->of_node = tps->dev->of_node;
	config.driver_data = tps;
	config.regmap = tps->regmap;

	for (i = 0; i < tps->reg_config->num_elems; ++i) {
		struct regulator_desc * const desc_ptr = &tps->reg_config->config[i].desc;

		dev_dbg(tps->dev, "Index: %u; Regulator name: \"%s\"; Regulator ID: %d\n",
			i, desc_ptr->name, desc_ptr->id);

		rdev = devm_regulator_register(&pdev->dev, desc_ptr, &config);
		if (IS_ERR(rdev)) {
			dev_err(tps->dev, "failed to register %d \"%s\" regulator\n",
				i, desc_ptr->name);
			return PTR_ERR(rdev);
		}
	}

	return 0;
}

static const struct platform_device_id tps65086_regulator_id_table[] = {
	{ "tps65086-regulator", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(platform, tps65086_regulator_id_table);

static struct platform_driver tps65086_regulator_driver = {
	.driver = {
		.name = "tps65086-regulator",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = tps65086_regulator_probe,
	.id_table = tps65086_regulator_id_table,
};
module_platform_driver(tps65086_regulator_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("TPS65086 Regulator driver");
MODULE_LICENSE("GPL v2");
