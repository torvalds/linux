// SPDX-License-Identifier: GPL-2.0
//
// Copyright (c) 2019 five technologies GmbH
// Author: Markus Reichl <m.reichl@fivetechno.de>

#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/of.h>
#include <linux/regulator/driver.h>
#include <linux/regmap.h>


#define VOL_MIN_IDX			0x00
#define VOL_MAX_IDX			0x7ff

/* Register definitions */
#define MP8859_VOUT_L_REG		0    //3 lo Bits
#define MP8859_VOUT_H_REG		1    //8 hi Bits
#define MP8859_VOUT_GO_REG		2
#define MP8859_IOUT_LIM_REG		3
#define MP8859_CTL1_REG			4
#define MP8859_CTL2_REG			5
#define MP8859_RESERVED1_REG		6
#define MP8859_RESERVED2_REG		7
#define MP8859_RESERVED3_REG		8
#define MP8859_STATUS_REG		9
#define MP8859_INTERRUPT_REG		0x0A
#define MP8859_MASK_REG			0x0B
#define MP8859_ID1_REG			0x0C
#define MP8859_MFR_ID_REG		0x27
#define MP8859_DEV_ID_REG		0x28
#define MP8859_IC_REV_REG		0x29

#define MP8859_MAX_REG			0x29

#define MP8859_GO_BIT			0x01

#define MP8859_IOUT_LIM_MASK		0x7f

#define MP8859_ENABLE_MASK		0x80
#define MP8859_DISCHG_EN_MASK		0x10
#define MP8859_MODE_MASK		0x08

#define MP8859_PG_MASK			0x80
#define MP8859_OTP_MASK			0x40
#define MP8859_OTW_MASK			0x20
#define MP8859_CC_CV_MASK		0x10

static int mp8859_set_voltage_sel(struct regulator_dev *rdev, unsigned int sel)
{
	int ret;

	ret = regmap_write(rdev->regmap, MP8859_VOUT_L_REG, sel & 0x7);

	if (ret)
		return ret;
	ret = regmap_write(rdev->regmap, MP8859_VOUT_H_REG, sel >> 3);

	if (ret)
		return ret;
	ret = regmap_update_bits(rdev->regmap, MP8859_VOUT_GO_REG,
					MP8859_GO_BIT, 1);
	return ret;
}

static int mp8859_get_voltage_sel(struct regulator_dev *rdev)
{
	unsigned int val_tmp;
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, MP8859_VOUT_H_REG, &val_tmp);

	if (ret)
		return ret;
	val = val_tmp << 3;

	ret = regmap_read(rdev->regmap, MP8859_VOUT_L_REG, &val_tmp);

	if (ret)
		return ret;
	val |= val_tmp & 0x07;
	return val;
}

static int mp8859_set_voltage_time_sel(struct regulator_dev *rdev,
				       unsigned int from, unsigned int to)
{
	int change;

	/* The voltage ramps at 1mV/uS, selectors are 10mV */
	if (from > to)
		change = from - to;
	else
		change = to - from;

	return change * 10 * 1000;
}

static unsigned int mp8859_get_mode(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	ret = regmap_read(rdev->regmap, MP8859_CTL1_REG, &val);
	if (ret != 0) {
		dev_err(&rdev->dev, "Failed to read mode: %d\n", ret);
		return 0;
	}

	if (val & MP8859_MODE_MASK)
		return REGULATOR_MODE_FAST;
	else
		return REGULATOR_MODE_NORMAL;
}

static int mp8859_set_mode(struct regulator_dev *rdev, unsigned int mode)
{
	unsigned int val;

	switch (mode) {
	case REGULATOR_MODE_FAST:
		val = MP8859_MODE_MASK;
		break;
	case REGULATOR_MODE_NORMAL:
		val = 0;
		break;
	default:
		return -EINVAL;
	}

	return regmap_update_bits(rdev->regmap, MP8859_CTL1_REG,
				  MP8859_MODE_MASK, val);
}

static int mp8859_set_current_limit(struct regulator_dev *rdev,
				    int min_uA, int max_uA)
{
	unsigned int cur_val, new_val;
	int ret, i;

	/* Steps of 50mA */
	new_val = max_uA / 50000;
	if (new_val > MP8859_IOUT_LIM_MASK)
		return -EINVAL;
	if (new_val == 0)
		return -EINVAL;

	/*
	 * If the regulator is limiting then ramp gradually as per
	 * datasheet, otherwise just set the value directly.
	 */
	ret = regmap_read(rdev->regmap, MP8859_STATUS_REG, &cur_val);
	if (ret != 0)
		return ret;
	if (!(cur_val & MP8859_CC_CV_MASK)) {
		return regmap_update_bits(rdev->regmap, MP8859_IOUT_LIM_REG,
					  MP8859_IOUT_LIM_MASK, new_val);
	}

	ret = regmap_read(rdev->regmap, MP8859_IOUT_LIM_REG, &cur_val);
	if (ret != 0)
		return ret;

	if (cur_val >= new_val) {
		for (i = cur_val; i >= new_val; i--) {
			ret = regmap_update_bits(rdev->regmap,
						 MP8859_IOUT_LIM_REG,
						 MP8859_IOUT_LIM_MASK,
						 cur_val - i);
			if (ret != 0)
				return ret;
		}
	} else {
		for (i = cur_val; i <= new_val; i++) {
			ret = regmap_update_bits(rdev->regmap,
						 MP8859_IOUT_LIM_REG,
						 MP8859_IOUT_LIM_MASK,
						 cur_val + i);
			if (ret != 0)
				return ret;
		}
	}

	return 0;
}

static int mp8859_get_status(struct regulator_dev *rdev)
{
	unsigned int val;
	int ret;

	/* Output status is only meaingful when enabled */
	ret = regmap_read(rdev->regmap, MP8859_CTL1_REG, &val);
	if (ret != 0)
		return ret;
	if (!(val & MP8859_ENABLE_MASK))
		return REGULATOR_STATUS_UNDEFINED;

	ret = regmap_read(rdev->regmap, MP8859_STATUS_REG, &val);
	if (ret != 0)
		return ret;

	if (val & MP8859_PG_MASK)
		return REGULATOR_STATUS_ON;
	else
		return REGULATOR_STATUS_ERROR;
}

static int mp8859_get_error_flags(struct regulator_dev *rdev,
				  unsigned int *flags)
{
	unsigned int status, enabled;
	int ret;

	*flags = 0;

	/* Output status is only meaingful when enabled */
	ret = regmap_read(rdev->regmap, MP8859_CTL1_REG, &enabled);
	if (ret != 0)
		return ret;
	enabled &= MP8859_ENABLE_MASK;

	ret = regmap_read(rdev->regmap, MP8859_STATUS_REG, &status);
	if (ret != 0)
		return ret;

	if (enabled && !(status & MP8859_PG_MASK))
		status |= REGULATOR_ERROR_FAIL;
	if (status & MP8859_OTP_MASK)
		status |= REGULATOR_ERROR_OVER_TEMP;
	if (status & MP8859_OTW_MASK)
		status |= REGULATOR_ERROR_OVER_TEMP_WARN;
	if (status & MP8859_CC_CV_MASK)
		status |= REGULATOR_ERROR_OVER_CURRENT;

	return 0;
}

static const struct linear_range mp8859_dcdc_ranges[] = {
	REGULATOR_LINEAR_RANGE(0, VOL_MIN_IDX, VOL_MAX_IDX, 10000),
};

static bool mp8859_readable(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MP8859_VOUT_L_REG:
	case MP8859_VOUT_H_REG:
	case MP8859_VOUT_GO_REG:
	case MP8859_IOUT_LIM_REG:
	case MP8859_CTL1_REG:
	case MP8859_CTL2_REG:
	case MP8859_STATUS_REG:
	case MP8859_INTERRUPT_REG:
	case MP8859_MASK_REG:
	case MP8859_ID1_REG:
	case MP8859_MFR_ID_REG:
	case MP8859_DEV_ID_REG:
	case MP8859_IC_REV_REG:
		return true;
	default:
		return false;
	}
}

static bool mp8859_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MP8859_VOUT_GO_REG:
	case MP8859_STATUS_REG:
	case MP8859_INTERRUPT_REG:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config mp8859_regmap = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MP8859_MAX_REG,
	.cache_type = REGCACHE_MAPLE,
	.readable_reg = mp8859_readable,
	.volatile_reg = mp8859_volatile,
};

static const struct regulator_ops mp8859_ops = {
	.set_voltage_sel = mp8859_set_voltage_sel,
	.get_voltage_sel = mp8859_get_voltage_sel,
	.list_voltage = regulator_list_voltage_linear_range,
	.set_voltage_time_sel = mp8859_set_voltage_time_sel,
	.enable = regulator_enable_regmap,
	.disable = regulator_disable_regmap,
	.is_enabled = regulator_is_enabled_regmap,
	.set_mode = mp8859_set_mode,
	.get_mode = mp8859_get_mode,
	.set_active_discharge = regulator_set_active_discharge_regmap,
	.set_current_limit = mp8859_set_current_limit,
	.get_status = mp8859_get_status,
	.get_error_flags = mp8859_get_error_flags,
};

static const struct regulator_desc mp8859_regulators[] = {
	{
		.id = 0,
		.type = REGULATOR_VOLTAGE,
		.name = "mp8859_dcdc",
		.supply_name = "vin",
		.of_match = of_match_ptr("mp8859_dcdc"),
		.n_voltages = VOL_MAX_IDX + 1,
		.linear_ranges = mp8859_dcdc_ranges,
		.n_linear_ranges = 1,
		.enable_reg = MP8859_CTL1_REG,
		.enable_mask = MP8859_ENABLE_MASK,
		.enable_val = MP8859_ENABLE_MASK,
		.active_discharge_reg = MP8859_CTL1_REG,
		.active_discharge_on = MP8859_DISCHG_EN_MASK,
		.active_discharge_mask = MP8859_DISCHG_EN_MASK,
		.ops = &mp8859_ops,
		.owner = THIS_MODULE,
	},
};

static int mp8859_i2c_probe(struct i2c_client *i2c)
{
	int ret;
	struct regulator_config config = {.dev = &i2c->dev};
	struct regmap *regmap = devm_regmap_init_i2c(i2c, &mp8859_regmap);
	struct regulator_dev *rdev;
	unsigned int val, rev;

	if (IS_ERR(regmap)) {
		ret = PTR_ERR(regmap);
		dev_err(&i2c->dev, "regmap init failed: %d\n", ret);
		return ret;
	}

	ret = regmap_read(regmap, MP8859_MFR_ID_REG, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read manufacturer ID: %d\n", ret);
		return ret;
	}
	if (val != 0x9) {
		dev_err(&i2c->dev, "Manufacturer ID %x != 9\n", val);
		return -EINVAL;
	}

	ret = regmap_read(regmap, MP8859_DEV_ID_REG, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read device ID: %d\n", ret);
		return ret;
	}
	if (val != 0x58) {
		dev_err(&i2c->dev, "Manufacturer ID %x != 0x58\n", val);
		return -EINVAL;
	}

	ret = regmap_read(regmap, MP8859_IC_REV_REG, &rev);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read device revision: %d\n", ret);
		return ret;
	}
	ret = regmap_read(regmap, MP8859_ID1_REG, &val);
	if (ret != 0) {
		dev_err(&i2c->dev, "Failed to read device ID1: %d\n", ret);
		return ret;
	}
	dev_info(&i2c->dev, "MP8859-%04d revision %d\n", val, rev);

	rdev = devm_regulator_register(&i2c->dev, &mp8859_regulators[0],
					&config);

	if (IS_ERR(rdev)) {
		ret = PTR_ERR(rdev);
		dev_err(&i2c->dev, "failed to register %s: %d\n",
			mp8859_regulators[0].name, ret);
		return ret;
	}
	return 0;
}

static const struct of_device_id mp8859_dt_id[] __maybe_unused = {
	{.compatible =  "mps,mp8859"},
	{},
};
MODULE_DEVICE_TABLE(of, mp8859_dt_id);

static const struct i2c_device_id mp8859_i2c_id[] = {
	{ "mp8859", },
	{  },
};
MODULE_DEVICE_TABLE(i2c, mp8859_i2c_id);

static struct i2c_driver mp8859_regulator_driver = {
	.driver = {
		.name = "mp8859",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = of_match_ptr(mp8859_dt_id),
	},
	.probe = mp8859_i2c_probe,
	.id_table = mp8859_i2c_id,
};

module_i2c_driver(mp8859_regulator_driver);

MODULE_DESCRIPTION("Monolithic Power Systems MP8859 voltage regulator driver");
MODULE_AUTHOR("Markus Reichl <m.reichl@fivetechno.de>");
MODULE_LICENSE("GPL v2");
