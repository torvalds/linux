// SPDX-License-Identifier: GPL-2.0-only
//
// DA9121 Single-channel dual-phase 10A buck converter
//
// Copyright (C) 2020 Axis Communications AB
//
// DA9130 Single-channel dual-phase 10A buck converter (Automotive)
// DA9217 Single-channel dual-phase  6A buck converter
// DA9122 Dual-channel single-phase  5A buck converter
// DA9131 Dual-channel single-phase  5A buck converter (Automotive)
// DA9220 Dual-channel single-phase  3A buck converter
// DA9132 Dual-channel single-phase  3A buck converter (Automotive)
//
// Copyright (C) 2020 Dialog Semiconductor

#include <linux/of_device.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/regulator/of_regulator.h>
#include <linux/regulator/machine.h>
#include <linux/regulator/driver.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/regulator/da9121.h>
#include <linux/interrupt.h>
#include <linux/workqueue.h>

#include "da9121-regulator.h"

/* Chip data */
struct da9121 {
	struct device *dev;
	struct delayed_work work;
	struct da9121_pdata *pdata;
	struct regmap *regmap;
	struct regulator_dev *rdev[DA9121_IDX_MAX];
	unsigned int persistent[2];
	unsigned int passive_delay;
	int chip_irq;
	int variant_id;
	int subvariant_id;
};

/* Define ranges for different variants, enabling translation to/from
 * registers. Maximums give scope to allow for transients.
 */
struct da9121_range {
	int val_min;
	int val_max;
	int val_stp;
	int reg_min;
	int reg_max;
};

static struct da9121_range da9121_10A_2phase_current = {
	.val_min =  7000000,
	.val_max = 20000000,
	.val_stp =  1000000,
	.reg_min = 1,
	.reg_max = 14,
};

static struct da9121_range da9121_6A_2phase_current = {
	.val_min =  7000000,
	.val_max = 12000000,
	.val_stp =  1000000,
	.reg_min = 1,
	.reg_max = 6,
};

static struct da9121_range da9121_5A_1phase_current = {
	.val_min =  3500000,
	.val_max = 10000000,
	.val_stp =   500000,
	.reg_min = 1,
	.reg_max = 14,
};

static struct da9121_range da9121_3A_1phase_current = {
	.val_min = 3500000,
	.val_max = 6000000,
	.val_stp =  500000,
	.reg_min = 1,
	.reg_max = 6,
};

static struct da9121_range da914x_40A_4phase_current = {
	.val_min = 14000000,
	.val_max = 80000000,
	.val_stp =  2000000,
	.reg_min = 1,
	.reg_max = 14,
};

static struct da9121_range da914x_20A_2phase_current = {
	.val_min =  7000000,
	.val_max = 40000000,
	.val_stp =  2000000,
	.reg_min = 1,
	.reg_max = 14,
};

struct da9121_variant_info {
	int num_bucks;
	int num_phases;
	struct da9121_range *current_range;
};

static const struct da9121_variant_info variant_parameters[] = {
	{ 1, 2, &da9121_10A_2phase_current },	//DA9121_TYPE_DA9121_DA9130
	{ 2, 1, &da9121_3A_1phase_current  },	//DA9121_TYPE_DA9220_DA9132
	{ 2, 1, &da9121_5A_1phase_current  },	//DA9121_TYPE_DA9122_DA9131
	{ 1, 2, &da9121_6A_2phase_current  },	//DA9121_TYPE_DA9217
	{ 1, 4, &da914x_40A_4phase_current },   //DA9121_TYPE_DA9141
	{ 1, 2, &da914x_20A_2phase_current },   //DA9121_TYPE_DA9142
};

struct da9121_field {
	unsigned int reg;
	unsigned int msk;
};

static const struct da9121_field da9121_current_field[2] = {
	{ DA9121_REG_BUCK_BUCK1_2, DA9121_MASK_BUCK_BUCKx_2_CHx_ILIM },
	{ DA9xxx_REG_BUCK_BUCK2_2, DA9121_MASK_BUCK_BUCKx_2_CHx_ILIM },
};

static const struct da9121_field da9121_mode_field[2] = {
	{ DA9121_REG_BUCK_BUCK1_4, DA9121_MASK_BUCK_BUCKx_4_CHx_A_MODE },
	{ DA9xxx_REG_BUCK_BUCK2_4, DA9121_MASK_BUCK_BUCKx_4_CHx_A_MODE },
};

struct status_event_data {
	int buck_id; /* 0=core, 1/2-buck */
	int reg_index;  /* index for status/event/mask register selection */
	int status_bit; /* bit masks... */
	int event_bit;
	int mask_bit;
	unsigned long notification; /* Notification for status inception */
	char *warn; /* if NULL, notify - otherwise dev_warn this string */
};

#define DA9121_STATUS(id, bank, name, notification, warning) \
	{ id, bank, \
	DA9121_MASK_SYS_STATUS_##bank##_##name, \
	DA9121_MASK_SYS_EVENT_##bank##_E_##name, \
	DA9121_MASK_SYS_MASK_##bank##_M_##name, \
	notification, warning }

/* For second buck related event bits that are specific to DA9122, DA9220 variants */
#define DA9xxx_STATUS(id, bank, name, notification, warning) \
	{ id, bank, \
	DA9xxx_MASK_SYS_STATUS_##bank##_##name, \
	DA9xxx_MASK_SYS_EVENT_##bank##_E_##name, \
	DA9xxx_MASK_SYS_MASK_##bank##_M_##name, \
	notification, warning }

/* The status signals that may need servicing, depending on device variant.
 * After assertion, they persist; so event is notified, the IRQ disabled,
 * and status polled until clear again and IRQ is reenabled.
 *
 * SG/PG1/PG2 should be set when device first powers up and should never
 * re-occur. When this driver starts, it is expected that these will have
 * self-cleared for when the IRQs are enabled, so these should never be seen.
 * If seen, the implication is that the device has reset.
 *
 * GPIO0/1/2 are not configured for use by default, so should not be seen.
 */
static const struct status_event_data status_event_handling[] = {
	DA9xxx_STATUS(0, 0, SG, 0, "Handled E_SG\n"),
	DA9121_STATUS(0, 0, TEMP_CRIT, (REGULATOR_EVENT_OVER_TEMP|REGULATOR_EVENT_DISABLE), NULL),
	DA9121_STATUS(0, 0, TEMP_WARN, REGULATOR_EVENT_OVER_TEMP, NULL),
	DA9121_STATUS(1, 1, PG1, 0, "Handled E_PG1\n"),
	DA9121_STATUS(1, 1, OV1, REGULATOR_EVENT_REGULATION_OUT, NULL),
	DA9121_STATUS(1, 1, UV1, REGULATOR_EVENT_UNDER_VOLTAGE, NULL),
	DA9121_STATUS(1, 1, OC1, REGULATOR_EVENT_OVER_CURRENT, NULL),
	DA9xxx_STATUS(2, 1, PG2, 0, "Handled E_PG2\n"),
	DA9xxx_STATUS(2, 1, OV2, REGULATOR_EVENT_REGULATION_OUT, NULL),
	DA9xxx_STATUS(2, 1, UV2, REGULATOR_EVENT_UNDER_VOLTAGE, NULL),
	DA9xxx_STATUS(2, 1, OC2, REGULATOR_EVENT_OVER_CURRENT, NULL),
	DA9121_STATUS(0, 2, GPIO0, 0, "Handled E_GPIO0\n"),
	DA9121_STATUS(0, 2, GPIO1, 0, "Handled E_GPIO1\n"),
	DA9121_STATUS(0, 2, GPIO2, 0, "Handled E_GPIO2\n"),
};

static int da9121_get_current_limit(struct regulator_dev *rdev)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct da9121_range *range =
		variant_parameters[chip->variant_id].current_range;
	unsigned int val = 0;
	int ret = 0;

	ret = regmap_read(chip->regmap, da9121_current_field[id].reg, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read BUCK register: %d\n", ret);
		goto error;
	}

	if (val < range->reg_min) {
		ret = -EACCES;
		goto error;
	}

	if (val > range->reg_max) {
		ret = -EINVAL;
		goto error;
	}

	return range->val_min + (range->val_stp * (val - range->reg_min));
error:
	return ret;
}

static int da9121_ceiling_selector(struct regulator_dev *rdev,
		int min, int max,
		unsigned int *selector)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	struct da9121_range *range =
		variant_parameters[chip->variant_id].current_range;
	unsigned int level;
	unsigned int i = 0;
	unsigned int sel = 0;
	int ret = 0;

	if (range->val_min > max || range->val_max < min) {
		dev_err(chip->dev,
			"Requested current out of regulator capability\n");
		ret = -EINVAL;
		goto error;
	}

	level = range->val_max;
	for (i = range->reg_max; i >= range->reg_min; i--) {
		if (level <= max) {
			sel = i;
			break;
		}
		level -= range->val_stp;
	}

	if (level < min) {
		dev_err(chip->dev,
			"Best match falls below minimum requested current\n");
		ret = -EINVAL;
		goto error;
	}

	*selector = sel;
error:
	return ret;
}

static int da9121_set_current_limit(struct regulator_dev *rdev,
				int min_ua, int max_ua)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	struct da9121_range *range =
		variant_parameters[chip->variant_id].current_range;
	unsigned int sel = 0;
	int ret = 0;

	if (min_ua < range->val_min ||
	    max_ua > range->val_max) {
		ret = -EINVAL;
		goto error;
	}

	if (rdev->desc->ops->is_enabled(rdev)) {
		ret = -EBUSY;
		goto error;
	}

	ret = da9121_ceiling_selector(rdev, min_ua, max_ua, &sel);
	if (ret < 0)
		goto error;

	ret = regmap_update_bits(chip->regmap,
				da9121_current_field[id].reg,
				da9121_current_field[id].msk,
				(unsigned int)sel);
	if (ret < 0)
		dev_err(chip->dev, "Cannot update BUCK current limit, err: %d\n", ret);

error:
	return ret;
}

static unsigned int da9121_map_mode(unsigned int mode)
{
	switch (mode) {
	case DA9121_BUCK_MODE_FORCE_PWM:
		return REGULATOR_MODE_FAST;
	case DA9121_BUCK_MODE_FORCE_PWM_SHEDDING:
		return REGULATOR_MODE_NORMAL;
	case DA9121_BUCK_MODE_AUTO:
		return REGULATOR_MODE_IDLE;
	case DA9121_BUCK_MODE_FORCE_PFM:
		return REGULATOR_MODE_STANDBY;
	default:
		return REGULATOR_MODE_INVALID;
	}
}

static int da9121_buck_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = DA9121_BUCK_MODE_FORCE_PWM;
		break;
	case REGULATOR_MODE_NORMAL:
		val = DA9121_BUCK_MODE_FORCE_PWM_SHEDDING;
		break;
	case REGULATOR_MODE_IDLE:
		val = DA9121_BUCK_MODE_AUTO;
		break;
	case REGULATOR_MODE_STANDBY:
		val = DA9121_BUCK_MODE_FORCE_PFM;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(chip->regmap,
				  da9121_mode_field[id].reg,
				  da9121_mode_field[id].msk,
				  val);
}

static unsigned int da9121_buck_get_mode(struct regulator_dev *rdev)
{
	struct da9121 *chip = rdev_get_drvdata(rdev);
	int id = rdev_get_id(rdev);
	unsigned int val, mode;
	int ret = 0;

	ret = regmap_read(chip->regmap, da9121_mode_field[id].reg, &val);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read BUCK register: %d\n", ret);
		return -EINVAL;
	}

	mode = da9121_map_mode(val & da9121_mode_field[id].msk);
	if (mode == REGULATOR_MODE_INVALID)
		return -EINVAL;

	return mode;
}

static const struct regulator_ops da9121_buck_ops = {
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_voltage_sel = regulator_set_voltage_sel_regmap,
	.get_voltage_sel = regulator_get_voltage_sel_regmap,
	.list_voltage = regulator_list_voltage_linear,
	.get_current_limit = da9121_get_current_limit,
	.set_current_limit = da9121_set_current_limit,
	.set_mode = da9121_buck_set_mode,
	.get_mode = da9121_buck_get_mode,
};

static struct of_regulator_match da9121_matches[] = {
	[DA9121_IDX_BUCK1] = { .name = "buck1" },
	[DA9121_IDX_BUCK2] = { .name = "buck2" },
};

static int da9121_of_parse_cb(struct device_node *np,
				const struct regulator_desc *desc,
				struct regulator_config *config)
{
	struct da9121 *chip = config->driver_data;
	struct da9121_pdata *pdata;
	struct gpio_desc *ena_gpiod;

	if (chip->pdata == NULL) {
		pdata = devm_kzalloc(chip->dev, sizeof(*pdata), GFP_KERNEL);
		if (!pdata)
			return -ENOMEM;
	} else {
		pdata = chip->pdata;
	}

	pdata->num_buck++;

	if (pdata->num_buck > variant_parameters[chip->variant_id].num_bucks) {
		dev_err(chip->dev, "Error: excessive regulators for device\n");
		return -ENODEV;
	}

	ena_gpiod = fwnode_gpiod_get_index(of_fwnode_handle(np), "enable", 0,
						GPIOD_OUT_HIGH |
						GPIOD_FLAGS_BIT_NONEXCLUSIVE,
						"da9121-enable");
	if (!IS_ERR(ena_gpiod))
		config->ena_gpiod = ena_gpiod;

	if (variant_parameters[chip->variant_id].num_bucks == 2) {
		uint32_t ripple_cancel;
		uint32_t ripple_reg;
		int ret;

		if (of_property_read_u32(da9121_matches[pdata->num_buck-1].of_node,
				"dlg,ripple-cancel", &ripple_cancel)) {
			if (pdata->num_buck > 1)
				ripple_reg = DA9xxx_REG_BUCK_BUCK2_7;
			else
				ripple_reg = DA9121_REG_BUCK_BUCK1_7;

			ret = regmap_update_bits(chip->regmap, ripple_reg,
				DA9xxx_MASK_BUCK_BUCKx_7_CHx_RIPPLE_CANCEL,
				ripple_cancel);
			if (ret < 0)
				dev_err(chip->dev, "Cannot set ripple mode, err: %d\n", ret);
		}
	}

	return 0;
}

#define DA9121_MIN_MV		300
#define DA9121_MAX_MV		1900
#define DA9121_STEP_MV		10
#define DA9121_MIN_SEL		(DA9121_MIN_MV / DA9121_STEP_MV)
#define DA9121_N_VOLTAGES	(((DA9121_MAX_MV - DA9121_MIN_MV) / DA9121_STEP_MV) \
				 + 1 + DA9121_MIN_SEL)

static const struct regulator_desc da9121_reg = {
	.id = DA9121_IDX_BUCK1,
	.name = "da9121",
	.of_match = "buck1",
	.of_parse_cb = da9121_of_parse_cb,
	.owner = THIS_MODULE,
	.regulators_node = of_match_ptr("regulators"),
	.of_map_mode = da9121_map_mode,
	.ops = &da9121_buck_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = DA9121_N_VOLTAGES,
	.min_uV = DA9121_MIN_MV * 1000,
	.uV_step = DA9121_STEP_MV * 1000,
	.linear_min_sel = DA9121_MIN_SEL,
	.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
	.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	.enable_reg = DA9121_REG_BUCK_BUCK1_0,
	.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
	/* Default value of BUCK_BUCK1_0.CH1_SRC_DVC_UP */
	.ramp_delay = 20000,
	/* tBUCK_EN */
	.enable_time = 20,
};

static const struct regulator_desc da9220_reg[2] = {
	{
		.id = DA9121_IDX_BUCK1,
		.name = "DA9220/DA9132 BUCK1",
		.of_match = "buck1",
		.of_parse_cb = da9121_of_parse_cb,
		.owner = THIS_MODULE,
		.regulators_node = of_match_ptr("regulators"),
		.of_map_mode = da9121_map_mode,
		.ops = &da9121_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = DA9121_N_VOLTAGES,
		.min_uV = DA9121_MIN_MV * 1000,
		.uV_step = DA9121_STEP_MV * 1000,
		.linear_min_sel = DA9121_MIN_SEL,
		.enable_reg = DA9121_REG_BUCK_BUCK1_0,
		.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
		.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
		.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	},
	{
		.id = DA9121_IDX_BUCK2,
		.name = "DA9220/DA9132 BUCK2",
		.of_match = "buck2",
		.of_parse_cb = da9121_of_parse_cb,
		.owner = THIS_MODULE,
		.regulators_node = of_match_ptr("regulators"),
		.of_map_mode = da9121_map_mode,
		.ops = &da9121_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = DA9121_N_VOLTAGES,
		.min_uV = DA9121_MIN_MV * 1000,
		.uV_step = DA9121_STEP_MV * 1000,
		.linear_min_sel = DA9121_MIN_SEL,
		.enable_reg = DA9xxx_REG_BUCK_BUCK2_0,
		.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
		.vsel_reg = DA9xxx_REG_BUCK_BUCK2_5,
		.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	}
};

static const struct regulator_desc da9122_reg[2] = {
	{
		.id = DA9121_IDX_BUCK1,
		.name = "DA9122/DA9131 BUCK1",
		.of_match = "buck1",
		.of_parse_cb = da9121_of_parse_cb,
		.owner = THIS_MODULE,
		.regulators_node = of_match_ptr("regulators"),
		.of_map_mode = da9121_map_mode,
		.ops = &da9121_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = DA9121_N_VOLTAGES,
		.min_uV = DA9121_MIN_MV * 1000,
		.uV_step = DA9121_STEP_MV * 1000,
		.linear_min_sel = DA9121_MIN_SEL,
		.enable_reg = DA9121_REG_BUCK_BUCK1_0,
		.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
		.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
		.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	},
	{
		.id = DA9121_IDX_BUCK2,
		.name = "DA9122/DA9131 BUCK2",
		.of_match = "buck2",
		.of_parse_cb = da9121_of_parse_cb,
		.owner = THIS_MODULE,
		.regulators_node = of_match_ptr("regulators"),
		.of_map_mode = da9121_map_mode,
		.ops = &da9121_buck_ops,
		.type = REGULATOR_VOLTAGE,
		.n_voltages = DA9121_N_VOLTAGES,
		.min_uV = DA9121_MIN_MV * 1000,
		.uV_step = DA9121_STEP_MV * 1000,
		.linear_min_sel = DA9121_MIN_SEL,
		.enable_reg = DA9xxx_REG_BUCK_BUCK2_0,
		.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
		.vsel_reg = DA9xxx_REG_BUCK_BUCK2_5,
		.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	}
};

static const struct regulator_desc da9217_reg = {
	.id = DA9121_IDX_BUCK1,
	.name = "DA9217 BUCK1",
	.of_match = "buck1",
	.of_parse_cb = da9121_of_parse_cb,
	.owner = THIS_MODULE,
	.regulators_node = of_match_ptr("regulators"),
	.of_map_mode = da9121_map_mode,
	.ops = &da9121_buck_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = DA9121_N_VOLTAGES,
	.min_uV = DA9121_MIN_MV * 1000,
	.uV_step = DA9121_STEP_MV * 1000,
	.linear_min_sel = DA9121_MIN_SEL,
	.enable_reg = DA9121_REG_BUCK_BUCK1_0,
	.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
	.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
	.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
};

#define DA914X_MIN_MV		500
#define DA914X_MAX_MV		1000
#define DA914X_STEP_MV		10
#define DA914X_MIN_SEL		(DA914X_MIN_MV / DA914X_STEP_MV)
#define DA914X_N_VOLTAGES	(((DA914X_MAX_MV - DA914X_MIN_MV) / DA914X_STEP_MV) \
				 + 1 + DA914X_MIN_SEL)

static const struct regulator_desc da9141_reg = {
	.id = DA9121_IDX_BUCK1,
	.name = "DA9141",
	.of_match = "buck1",
	.of_parse_cb = da9121_of_parse_cb,
	.owner = THIS_MODULE,
	.regulators_node = of_match_ptr("regulators"),
	.of_map_mode = da9121_map_mode,
	.ops = &da9121_buck_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = DA914X_N_VOLTAGES,
	.min_uV = DA914X_MIN_MV * 1000,
	.uV_step = DA914X_STEP_MV * 1000,
	.linear_min_sel = DA914X_MIN_SEL,
	.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
	.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
	.enable_reg = DA9121_REG_BUCK_BUCK1_0,
	.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
	/* Default value of BUCK_BUCK1_0.CH1_SRC_DVC_UP */
	.ramp_delay = 20000,
	/* tBUCK_EN */
	.enable_time = 20,
};

static const struct regulator_desc da9142_reg = {
	.id = DA9121_IDX_BUCK1,
	.name = "DA9142 BUCK1",
	.of_match = "buck1",
	.of_parse_cb = da9121_of_parse_cb,
	.owner = THIS_MODULE,
	.regulators_node = of_match_ptr("regulators"),
	.of_map_mode = da9121_map_mode,
	.ops = &da9121_buck_ops,
	.type = REGULATOR_VOLTAGE,
	.n_voltages = DA914X_N_VOLTAGES,
	.min_uV = DA914X_MIN_MV * 1000,
	.uV_step = DA914X_STEP_MV * 1000,
	.linear_min_sel = DA914X_MIN_SEL,
	.enable_reg = DA9121_REG_BUCK_BUCK1_0,
	.enable_mask = DA9121_MASK_BUCK_BUCKx_0_CHx_EN,
	.vsel_reg = DA9121_REG_BUCK_BUCK1_5,
	.vsel_mask = DA9121_MASK_BUCK_BUCKx_5_CHx_A_VOUT,
};


static const struct regulator_desc *local_da9121_regulators[][DA9121_IDX_MAX] = {
	[DA9121_TYPE_DA9121_DA9130] = { &da9121_reg, NULL },
	[DA9121_TYPE_DA9220_DA9132] = { &da9220_reg[0], &da9220_reg[1] },
	[DA9121_TYPE_DA9122_DA9131] = { &da9122_reg[0], &da9122_reg[1] },
	[DA9121_TYPE_DA9217] = { &da9217_reg, NULL },
	[DA9121_TYPE_DA9141] = { &da9141_reg, NULL },
	[DA9121_TYPE_DA9142] = { &da9142_reg, NULL },
};

static void da9121_status_poll_on(struct work_struct *work)
{
	struct da9121 *chip = container_of(work, struct da9121, work.work);
	int status[3] = {0};
	int clear[3] = {0};
	unsigned long delay;
	int i;
	int ret;

	ret = regmap_bulk_read(chip->regmap, DA9121_REG_SYS_STATUS_0, status, 2);
	if (ret < 0) {
		dev_err(chip->dev,
			"Failed to read STATUS registers: %d\n", ret);
		goto error;
	}

	/* Possible events are tested to be within range for the variant, potentially
	 * masked by the IRQ handler (not just warned about), as having been masked,
	 * and the respective state cleared - then flagged to unmask for next IRQ.
	 */
	for (i = 0; i < ARRAY_SIZE(status_event_handling); i++) {
		const struct status_event_data *item = &status_event_handling[i];
		int reg_idx = item->reg_index;
		bool relevant = (item->buck_id <= variant_parameters[chip->variant_id].num_bucks);
		bool supported = (item->warn == NULL);
		bool persisting = (chip->persistent[reg_idx] & item->event_bit);
		bool now_cleared = !(status[reg_idx] & item->status_bit);

		if (relevant && supported && persisting && now_cleared) {
			clear[reg_idx] |= item->mask_bit;
			chip->persistent[reg_idx] &= ~item->event_bit;
		}
	}

	for (i = 0; i < 2; i++) {
		if (clear[i]) {
			unsigned int reg = DA9121_REG_SYS_MASK_0 + i;
			unsigned int mbit = clear[i];

			ret = regmap_update_bits(chip->regmap, reg, mbit, 0);
			if (ret < 0) {
				dev_err(chip->dev,
					"Failed to unmask 0x%02x %d\n",
					reg, ret);
				goto error;
			}
		}
	}

	if (chip->persistent[0] | chip->persistent[1]) {
		delay = msecs_to_jiffies(chip->passive_delay);
		queue_delayed_work(system_freezable_wq, &chip->work, delay);
	}

error:
	return;
}

static irqreturn_t da9121_irq_handler(int irq, void *data)
{
	struct da9121 *chip = data;
	struct regulator_dev *rdev;
	int event[3] = {0};
	int handled[3] = {0};
	int mask[3] = {0};
	int ret = IRQ_NONE;
	int i;
	int err;

	err = regmap_bulk_read(chip->regmap, DA9121_REG_SYS_EVENT_0, event, 3);
	if (err < 0) {
		dev_err(chip->dev, "Failed to read EVENT registers %d\n", err);
		ret = IRQ_NONE;
		goto error;
	}

	err = regmap_bulk_read(chip->regmap, DA9121_REG_SYS_MASK_0, mask, 3);
	if (err < 0) {
		dev_err(chip->dev,
			"Failed to read MASK registers: %d\n", ret);
		ret = IRQ_NONE;
		goto error;
	}

	rdev = chip->rdev[DA9121_IDX_BUCK1];

	/* Possible events are tested to be within range for the variant, currently
	 * enabled, and having triggered this IRQ. The event may then be notified,
	 * or a warning given for unexpected events - those from device POR, and
	 * currently unsupported GPIO configurations.
	 */
	for (i = 0; i < ARRAY_SIZE(status_event_handling); i++) {
		const struct status_event_data *item = &status_event_handling[i];
		int reg_idx = item->reg_index;
		bool relevant = (item->buck_id <= variant_parameters[chip->variant_id].num_bucks);
		bool enabled = !(mask[reg_idx] & item->mask_bit);
		bool active = (event[reg_idx] & item->event_bit);
		bool notify = (item->warn == NULL);

		if (relevant && enabled && active) {
			if (notify) {
				chip->persistent[reg_idx] |= item->event_bit;
				regulator_notifier_call_chain(rdev, item->notification, NULL);
			} else {
				dev_warn(chip->dev, item->warn);
				handled[reg_idx] |= item->event_bit;
				ret = IRQ_HANDLED;
			}
		}
	}

	for (i = 0; i < 3; i++) {
		if (event[i] != handled[i]) {
			dev_warn(chip->dev,
				"Unhandled event(s) in bank%d 0x%02x\n", i,
				event[i] ^ handled[i]);
		}
	}

	/* Mask the interrupts for persistent events OV, OC, UV, WARN, CRIT */
	for (i = 0; i < 2; i++) {
		if (handled[i]) {
			unsigned int reg = DA9121_REG_SYS_MASK_0 + i;
			unsigned int mbit = handled[i];

			err = regmap_update_bits(chip->regmap, reg, mbit, mbit);
			if (err < 0) {
				dev_err(chip->dev,
					"Failed to mask 0x%02x interrupt %d\n",
					reg, err);
				ret = IRQ_NONE;
				goto error;
			}
		}
	}

	/* clear the events */
	if (handled[0] | handled[1] | handled[2]) {
		err = regmap_bulk_write(chip->regmap, DA9121_REG_SYS_EVENT_0, handled, 3);
		if (err < 0) {
			dev_err(chip->dev, "Fail to write EVENTs %d\n", err);
			ret = IRQ_NONE;
			goto error;
		}
	}

	queue_delayed_work(system_freezable_wq, &chip->work, 0);
error:
	return ret;
}

static int da9121_set_regulator_config(struct da9121 *chip)
{
	struct regulator_config config = { };
	unsigned int max_matches = variant_parameters[chip->variant_id].num_bucks;
	int ret = 0;
	int i;

	for (i = 0; i < max_matches; i++) {
		const struct regulator_desc *regl_desc =
			local_da9121_regulators[chip->variant_id][i];

		config.dev = chip->dev;
		config.driver_data = chip;
		config.regmap = chip->regmap;

		chip->rdev[i] = devm_regulator_register(chip->dev,
					regl_desc, &config);
		if (IS_ERR(chip->rdev[i])) {
			dev_err(chip->dev, "Failed to register regulator %s, %d/%d\n",
				regl_desc->name, (i+1), max_matches);
			ret = PTR_ERR(chip->rdev[i]);
			goto error;
		}
	}

error:
	return ret;
}

/* DA9121 chip register model */
static const struct regmap_range da9121_1ch_readable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_6),
	regmap_reg_range(DA9121_REG_OTP_DEVICE_ID, DA9121_REG_OTP_CONFIG_ID),
};

static const struct regmap_access_table da9121_1ch_readable_table = {
	.yes_ranges = da9121_1ch_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_1ch_readable_ranges),
};

static const struct regmap_range da9121_2ch_readable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_7),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_0, DA9xxx_REG_BUCK_BUCK2_7),
	regmap_reg_range(DA9121_REG_OTP_DEVICE_ID, DA9121_REG_OTP_CONFIG_ID),
};

static const struct regmap_access_table da9121_2ch_readable_table = {
	.yes_ranges = da9121_2ch_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_2ch_readable_ranges),
};

static const struct regmap_range da9121_1ch_writeable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_EVENT_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_2),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_4, DA9121_REG_BUCK_BUCK1_6),
};

static const struct regmap_access_table da9121_1ch_writeable_table = {
	.yes_ranges = da9121_1ch_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_1ch_writeable_ranges),
};

static const struct regmap_range da9121_2ch_writeable_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_EVENT_0, DA9121_REG_SYS_MASK_3),
	regmap_reg_range(DA9121_REG_SYS_CONFIG_2, DA9121_REG_SYS_CONFIG_3),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_2),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_4, DA9121_REG_BUCK_BUCK1_7),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_0, DA9xxx_REG_BUCK_BUCK2_2),
	regmap_reg_range(DA9xxx_REG_BUCK_BUCK2_4, DA9xxx_REG_BUCK_BUCK2_7),
};

static const struct regmap_access_table da9121_2ch_writeable_table = {
	.yes_ranges = da9121_2ch_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_2ch_writeable_ranges),
};


static const struct regmap_range da9121_volatile_ranges[] = {
	regmap_reg_range(DA9121_REG_SYS_STATUS_0, DA9121_REG_SYS_EVENT_2),
	regmap_reg_range(DA9121_REG_SYS_GPIO0_0, DA9121_REG_SYS_GPIO2_1),
	regmap_reg_range(DA9121_REG_BUCK_BUCK1_0, DA9121_REG_BUCK_BUCK1_6),
};

static const struct regmap_access_table da9121_volatile_table = {
	.yes_ranges = da9121_volatile_ranges,
	.n_yes_ranges = ARRAY_SIZE(da9121_volatile_ranges),
};

/* DA9121 regmap config for 1 channel variants */
static struct regmap_config da9121_1ch_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA9121_REG_OTP_CONFIG_ID,
	.rd_table = &da9121_1ch_readable_table,
	.wr_table = &da9121_1ch_writeable_table,
	.volatile_table = &da9121_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

/* DA9121 regmap config for 2 channel variants */
static struct regmap_config da9121_2ch_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = DA9121_REG_OTP_CONFIG_ID,
	.rd_table = &da9121_2ch_readable_table,
	.wr_table = &da9121_2ch_writeable_table,
	.volatile_table = &da9121_volatile_table,
	.cache_type = REGCACHE_RBTREE,
};

static int da9121_check_device_type(struct i2c_client *i2c, struct da9121 *chip)
{
	u32 device_id;
	u32 variant_id;
	u8 variant_mrc, variant_vrc;
	char *type;
	bool config_match = false;
	int ret = 0;

	ret = regmap_read(chip->regmap, DA9121_REG_OTP_DEVICE_ID, &device_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read device ID: %d\n", ret);
		goto error;
	}

	ret = regmap_read(chip->regmap, DA9121_REG_OTP_VARIANT_ID, &variant_id);
	if (ret < 0) {
		dev_err(chip->dev, "Cannot read variant ID: %d\n", ret);
		goto error;
	}

	if ((device_id != DA9121_DEVICE_ID) && (device_id != DA914x_DEVICE_ID)) {
		dev_err(chip->dev, "Invalid device ID: 0x%02x\n", device_id);
		ret = -ENODEV;
		goto error;
	}

	variant_vrc = variant_id & DA9121_MASK_OTP_VARIANT_ID_VRC;

	switch (chip->subvariant_id) {
	case DA9121_SUBTYPE_DA9121:
		type = "DA9121";
		config_match = (variant_vrc == DA9121_VARIANT_VRC);
		break;
	case DA9121_SUBTYPE_DA9130:
		type = "DA9130";
		config_match = (variant_vrc == DA9130_VARIANT_VRC);
		break;
	case DA9121_SUBTYPE_DA9220:
		type = "DA9220";
		config_match = (variant_vrc == DA9220_VARIANT_VRC);
		break;
	case DA9121_SUBTYPE_DA9132:
		type = "DA9132";
		config_match = (variant_vrc == DA9132_VARIANT_VRC);
		break;
	case DA9121_SUBTYPE_DA9122:
		type = "DA9122";
		config_match = (variant_vrc == DA9122_VARIANT_VRC);
		break;
	case DA9121_SUBTYPE_DA9131:
		type = "DA9131";
		config_match = (variant_vrc == DA9131_VARIANT_VRC);
		break;
	case DA9121_SUBTYPE_DA9217:
		type = "DA9217";
		config_match = (variant_vrc == DA9217_VARIANT_VRC);
		break;
	default:
		type = "Unknown";
		break;
	}

	if (device_id == DA914x_DEVICE_ID) {
		switch (chip->subvariant_id) {
		case DA9121_SUBTYPE_DA9141:
			type = "DA9141";
			config_match = (variant_vrc == DA9141_VARIANT_VRC);
			break;
		case DA9121_SUBTYPE_DA9142:
			type = "DA9142";
			config_match = (variant_vrc == DA9142_VARIANT_VRC);
			break;
		default:
			type = "Unknown";
			break;
		}
	}

	dev_info(chip->dev,
		 "Device detected (device-ID: 0x%02X, var-ID: 0x%02X, %s)\n",
		 device_id, variant_id, type);

	if (!config_match) {
		dev_err(chip->dev, "Device tree configuration does not match detected device.\n");
		ret = -EINVAL;
		goto error;
	}

	variant_mrc = (variant_id & DA9121_MASK_OTP_VARIANT_ID_MRC)
			>> DA9121_SHIFT_OTP_VARIANT_ID_MRC;

	if (((device_id == DA9121_DEVICE_ID) &&
	     (variant_mrc < DA9121_VARIANT_MRC_BASE)) ||
	    ((device_id == DA914x_DEVICE_ID) &&
	     (variant_mrc != DA914x_VARIANT_MRC_BASE))) {
		dev_err(chip->dev,
			"Cannot support variant MRC: 0x%02X\n", variant_mrc);
		ret = -EINVAL;
	}
error:
	return ret;
}

static int da9121_assign_chip_model(struct i2c_client *i2c,
			struct da9121 *chip)
{
	struct regmap_config *regmap;
	int ret = 0;

	chip->dev = &i2c->dev;

	/* Use configured subtype to select the regulator descriptor index and
	 * register map, common to both consumer and automotive grade variants
	 */
	switch (chip->subvariant_id) {
	case DA9121_SUBTYPE_DA9121:
	case DA9121_SUBTYPE_DA9130:
		chip->variant_id = DA9121_TYPE_DA9121_DA9130;
		regmap = &da9121_1ch_regmap_config;
		break;
	case DA9121_SUBTYPE_DA9217:
		chip->variant_id = DA9121_TYPE_DA9217;
		regmap = &da9121_1ch_regmap_config;
		break;
	case DA9121_SUBTYPE_DA9122:
	case DA9121_SUBTYPE_DA9131:
		chip->variant_id = DA9121_TYPE_DA9122_DA9131;
		regmap = &da9121_2ch_regmap_config;
		break;
	case DA9121_SUBTYPE_DA9220:
	case DA9121_SUBTYPE_DA9132:
		chip->variant_id = DA9121_TYPE_DA9220_DA9132;
		regmap = &da9121_2ch_regmap_config;
		break;
	case DA9121_SUBTYPE_DA9141:
		chip->variant_id = DA9121_TYPE_DA9141;
		regmap = &da9121_1ch_regmap_config;
		break;
	case DA9121_SUBTYPE_DA9142:
		chip->variant_id = DA9121_TYPE_DA9142;
		regmap = &da9121_2ch_regmap_config;
		break;
	}

	/* Set these up for of_regulator_match call which may want .of_map_modes */
	da9121_matches[0].desc = local_da9121_regulators[chip->variant_id][0];
	da9121_matches[1].desc = local_da9121_regulators[chip->variant_id][1];

	chip->regmap = devm_regmap_init_i2c(i2c, regmap);
	if (IS_ERR(chip->regmap)) {
		ret = PTR_ERR(chip->regmap);
		dev_err(chip->dev, "Failed to configure a register map: %d\n",
			ret);
		return ret;
	}

	ret = da9121_check_device_type(i2c, chip);

	return ret;
}

static int da9121_config_irq(struct i2c_client *i2c,
			struct da9121 *chip)
{
	unsigned int p_delay = DA9121_DEFAULT_POLLING_PERIOD_MS;
	const int mask_all[4] = { 0, 0, 0xFF, 0xFF };
	int ret = 0;

	chip->chip_irq = i2c->irq;

	if (chip->chip_irq != 0) {
		if (!of_property_read_u32(chip->dev->of_node,
					  "dlg,irq-polling-delay-passive-ms",
					  &p_delay)) {
			if (p_delay < DA9121_MIN_POLLING_PERIOD_MS ||
			    p_delay > DA9121_MAX_POLLING_PERIOD_MS) {
				dev_warn(chip->dev,
					 "Out-of-range polling period %d ms\n",
					 p_delay);
				p_delay = DA9121_DEFAULT_POLLING_PERIOD_MS;
			}
		}

		chip->passive_delay = p_delay;

		ret = request_threaded_irq(chip->chip_irq, NULL,
					da9121_irq_handler,
					IRQF_TRIGGER_LOW|IRQF_ONESHOT,
					"da9121", chip);
		if (ret != 0) {
			dev_err(chip->dev, "Failed IRQ request: %d\n",
				chip->chip_irq);
			goto error;
		}

		ret = regmap_bulk_write(chip->regmap, DA9121_REG_SYS_MASK_0, mask_all, 4);
		if (ret != 0) {
			dev_err(chip->dev, "Failed to set IRQ masks: %d\n",
				ret);
			goto regmap_error;
		}

		INIT_DELAYED_WORK(&chip->work, da9121_status_poll_on);
		dev_info(chip->dev, "Interrupt polling period set at %d ms\n",
			 chip->passive_delay);
	}
error:
	return ret;
regmap_error:
	free_irq(chip->chip_irq, chip);
	return ret;
}

static const struct of_device_id da9121_dt_ids[] = {
	{ .compatible = "dlg,da9121", .data = (void *) DA9121_SUBTYPE_DA9121 },
	{ .compatible = "dlg,da9130", .data = (void *) DA9121_SUBTYPE_DA9130 },
	{ .compatible = "dlg,da9217", .data = (void *) DA9121_SUBTYPE_DA9217 },
	{ .compatible = "dlg,da9122", .data = (void *) DA9121_SUBTYPE_DA9122 },
	{ .compatible = "dlg,da9131", .data = (void *) DA9121_SUBTYPE_DA9131 },
	{ .compatible = "dlg,da9220", .data = (void *) DA9121_SUBTYPE_DA9220 },
	{ .compatible = "dlg,da9132", .data = (void *) DA9121_SUBTYPE_DA9132 },
	{ .compatible = "dlg,da9141", .data = (void *) DA9121_SUBTYPE_DA9141 },
	{ .compatible = "dlg,da9142", .data = (void *) DA9121_SUBTYPE_DA9142 },
	{ }
};
MODULE_DEVICE_TABLE(of, da9121_dt_ids);

static inline int da9121_of_get_id(struct device *dev)
{
	const struct of_device_id *id = of_match_device(da9121_dt_ids, dev);

	if (!id) {
		dev_err(dev, "%s: Failed\n", __func__);
		return -EINVAL;
	}
	return (uintptr_t)id->data;
}

static int da9121_i2c_probe(struct i2c_client *i2c,
			    const struct i2c_device_id *id)
{
	struct da9121 *chip;
	const int mask_all[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	int ret = 0;

	chip = devm_kzalloc(&i2c->dev, sizeof(struct da9121), GFP_KERNEL);
	if (!chip) {
		ret = -ENOMEM;
		goto error;
	}

	chip->pdata = i2c->dev.platform_data;
	chip->subvariant_id = da9121_of_get_id(&i2c->dev);

	ret = da9121_assign_chip_model(i2c, chip);
	if (ret < 0)
		goto error;

	ret = regmap_bulk_write(chip->regmap, DA9121_REG_SYS_MASK_0, mask_all, 4);
	if (ret != 0) {
		dev_err(chip->dev, "Failed to set IRQ masks: %d\n", ret);
		goto error;
	}

	ret = da9121_set_regulator_config(chip);
	if (ret < 0)
		goto error;

	ret = da9121_config_irq(i2c, chip);

error:
	return ret;
}

static int da9121_i2c_remove(struct i2c_client *i2c)
{
	struct da9121 *chip = i2c_get_clientdata(i2c);
	const int mask_all[4] = { 0xFF, 0xFF, 0xFF, 0xFF };
	int ret;

	free_irq(chip->chip_irq, chip);
	cancel_delayed_work_sync(&chip->work);

	ret = regmap_bulk_write(chip->regmap, DA9121_REG_SYS_MASK_0, mask_all, 4);
	if (ret != 0)
		dev_err(chip->dev, "Failed to set IRQ masks: %d\n", ret);
	return 0;
}

static const struct i2c_device_id da9121_i2c_id[] = {
	{"da9121", DA9121_TYPE_DA9121_DA9130},
	{"da9130", DA9121_TYPE_DA9121_DA9130},
	{"da9217", DA9121_TYPE_DA9217},
	{"da9122", DA9121_TYPE_DA9122_DA9131},
	{"da9131", DA9121_TYPE_DA9122_DA9131},
	{"da9220", DA9121_TYPE_DA9220_DA9132},
	{"da9132", DA9121_TYPE_DA9220_DA9132},
	{"da9141", DA9121_TYPE_DA9141},
	{"da9142", DA9121_TYPE_DA9142},
	{},
};
MODULE_DEVICE_TABLE(i2c, da9121_i2c_id);

static struct i2c_driver da9121_regulator_driver = {
	.driver = {
		.name = "da9121",
		.of_match_table = of_match_ptr(da9121_dt_ids),
	},
	.probe = da9121_i2c_probe,
	.remove = da9121_i2c_remove,
	.id_table = da9121_i2c_id,
};

module_i2c_driver(da9121_regulator_driver);

MODULE_LICENSE("GPL v2");
