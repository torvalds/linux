// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for ST Microelectronics TSC1641 I2C power monitor
 *
 * 60 V, 16-bit high-precision power monitor with I2C and MIPI I3C interface
 * Datasheet: https://www.st.com/resource/en/datasheet/tsc1641.pdf
 *
 * Copyright (C) 2025 Igor Reznichenko <igor@reznichenko.net>
 */

#include <linux/bitfield.h>
#include <linux/bits.h>
#include <linux/device.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/sysfs.h>
#include <linux/util_macros.h>

/* I2C registers */
#define TSC1641_CONFIG		0x00
#define TSC1641_SHUNT_VOLTAGE	0x01
#define TSC1641_LOAD_VOLTAGE	0x02
#define TSC1641_POWER		0x03
#define TSC1641_CURRENT		0x04
#define TSC1641_TEMP		0x05
#define TSC1641_MASK		0x06
#define TSC1641_FLAG		0x07
#define TSC1641_RSHUNT		0x08 /* Shunt resistance */
#define TSC1641_SOL		0x09
#define TSC1641_SUL		0x0A
#define TSC1641_LOL		0x0B
#define TSC1641_LUL		0x0C
#define TSC1641_POL		0x0D
#define TSC1641_TOL		0x0E
#define TSC1641_MANUF_ID	0xFE /* 0x0006 */
#define TSC1641_DIE_ID		0xFF /* 0x1000 */
#define TSC1641_MAX_REG		0xFF

#define TSC1641_RSHUNT_DEFAULT	1000   /* 1mOhm */
#define TSC1641_CONFIG_DEFAULT	0x003F /* Default mode and temperature sensor */
#define TSC1641_MASK_DEFAULT	0xFC00 /* Unmask all alerts */

/* Bit mask for conversion time in the configuration register */
#define TSC1641_CONV_TIME_MASK	GENMASK(7, 4)

#define TSC1641_CONV_TIME_DEFAULT	1024
#define TSC1641_MIN_UPDATE_INTERVAL	1024

/* LSB value of different registers */
#define TSC1641_VLOAD_LSB_MVOLT		2
#define TSC1641_POWER_LSB_UWATT		25000
#define TSC1641_VSHUNT_LSB_NVOLT	2500 /* Use nanovolts to make it integer */
#define TSC1641_RSHUNT_LSB_UOHM		10
#define TSC1641_TEMP_LSB_MDEGC		500

/* Limits based on datasheet */
#define TSC1641_RSHUNT_MIN_UOHM		100
#define TSC1641_RSHUNT_MAX_UOHM		655350
#define TSC1641_CURR_ABS_MAX_MAMP	819200 /* Max current at 100uOhm*/

#define TSC1641_ALERT_POL_MASK		BIT(1)
#define TSC1641_ALERT_LATCH_EN_MASK	BIT(0)

/* Flags indicating alerts in TSC1641_FLAG register*/
#define TSC1641_SAT_FLAG		BIT(13)
#define TSC1641_SHUNT_OV_FLAG		BIT(6)
#define TSC1641_SHUNT_UV_FLAG		BIT(5)
#define TSC1641_LOAD_OV_FLAG		BIT(4)
#define TSC1641_LOAD_UV_FLAG		BIT(3)
#define TSC1641_POWER_OVER_FLAG		BIT(2)
#define TSC1641_TEMP_OVER_FLAG		BIT(1)

static bool tsc1641_writeable_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TSC1641_CONFIG:
	case TSC1641_MASK:
	case TSC1641_RSHUNT:
	case TSC1641_SOL:
	case TSC1641_SUL:
	case TSC1641_LOL:
	case TSC1641_LUL:
	case TSC1641_POL:
	case TSC1641_TOL:
		return true;
	default:
		return false;
	}
}

static bool tsc1641_volatile_reg(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case TSC1641_SHUNT_VOLTAGE:
	case TSC1641_LOAD_VOLTAGE:
	case TSC1641_POWER:
	case TSC1641_CURRENT:
	case TSC1641_TEMP:
	case TSC1641_FLAG:
	case TSC1641_MANUF_ID:
	case TSC1641_DIE_ID:
		return true;
	default:
		return false;
	}
}

static const struct regmap_config tsc1641_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,
	.use_single_write = true,
	.use_single_read = true,
	.max_register = TSC1641_MAX_REG,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = tsc1641_volatile_reg,
	.writeable_reg = tsc1641_writeable_reg,
};

struct tsc1641_data {
	long rshunt_uohm;
	long current_lsb_ua;
	struct regmap *regmap;
};

/*
 * Upper limit due to chip 16-bit shunt register, lower limit to
 * prevent current and power registers overflow
 */
static inline int tsc1641_validate_shunt(u32 val)
{
	if (val < TSC1641_RSHUNT_MIN_UOHM || val > TSC1641_RSHUNT_MAX_UOHM)
		return -EINVAL;
	return 0;
}

static int tsc1641_set_shunt(struct tsc1641_data *data, u32 val)
{
	struct regmap *regmap = data->regmap;
	long rshunt_reg;

	/* RSHUNT register LSB is 10uOhm so need to divide further */
	rshunt_reg = DIV_ROUND_CLOSEST(val, TSC1641_RSHUNT_LSB_UOHM);
	/*
	 * Clamp value to the nearest multiple of TSC1641_RSHUNT_LSB_UOHM
	 * in case shunt value provided was not a multiple
	 */
	data->rshunt_uohm = rshunt_reg * TSC1641_RSHUNT_LSB_UOHM;
	data->current_lsb_ua = DIV_ROUND_CLOSEST(TSC1641_VSHUNT_LSB_NVOLT * 1000,
						 data->rshunt_uohm);

	return regmap_write(regmap, TSC1641_RSHUNT, rshunt_reg);
}

/*
 * Conversion times in uS, value in CONFIG[CT3:CT0] corresponds to index in this array
 * See "Table 14. CT3 to CT0: conversion time" in:
 * https://www.st.com/resource/en/datasheet/tsc1641.pdf
 */
static const int tsc1641_conv_times[] = { 128, 256, 512, 1024, 2048, 4096, 8192, 16384, 32768 };

static int tsc1641_reg_to_upd_interval(u16 config)
{
	int idx = FIELD_GET(TSC1641_CONV_TIME_MASK, config);

	idx = clamp_val(idx, 0, ARRAY_SIZE(tsc1641_conv_times) - 1);
	int conv_time = tsc1641_conv_times[idx];

	/* Don't support sub-millisecond update interval as it's not supported in hwmon */
	conv_time = max(conv_time, TSC1641_MIN_UPDATE_INTERVAL);
	/* Return nearest value in milliseconds */
	return DIV_ROUND_CLOSEST(conv_time, 1000);
}

static u16 tsc1641_upd_interval_to_reg(long interval)
{
	/* Supported interval is 1ms - 33ms */
	interval = clamp_val(interval, 1, 33);

	int conv = interval * 1000;
	int conv_bits = find_closest(conv, tsc1641_conv_times,
				     ARRAY_SIZE(tsc1641_conv_times));

	return FIELD_PREP(TSC1641_CONV_TIME_MASK, conv_bits);
}

static int tsc1641_chip_write(struct device *dev, u32 attr, long val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_chip_update_interval:
		return regmap_update_bits(data->regmap, TSC1641_CONFIG,
					  TSC1641_CONV_TIME_MASK,
					  tsc1641_upd_interval_to_reg(val));
	default:
		return -EOPNOTSUPP;
	}
}

static int tsc1641_chip_read(struct device *dev, u32 attr, long *val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	u32 regval;
	int ret;

	switch (attr) {
	case hwmon_chip_update_interval:
		ret = regmap_read(data->regmap, TSC1641_CONFIG, &regval);
		if (ret)
			return ret;

		*val = tsc1641_reg_to_upd_interval(regval);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int tsc1641_flag_read(struct regmap *regmap, u32 flag, long *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read_bypassed(regmap, TSC1641_FLAG, &regval);
	if (ret)
		return ret;

	*val = !!(regval & flag);
	return 0;
}

static int tsc1641_in_read(struct device *dev, u32 attr, long *val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret, reg;
	long sat_flag;

	switch (attr) {
	case hwmon_in_input:
		reg = TSC1641_LOAD_VOLTAGE;
		break;
	case hwmon_in_min:
		reg = TSC1641_LUL;
		break;
	case hwmon_in_max:
		reg = TSC1641_LOL;
		break;
	case hwmon_in_min_alarm:
		return tsc1641_flag_read(regmap, TSC1641_LOAD_UV_FLAG, val);
	case hwmon_in_max_alarm:
		return tsc1641_flag_read(regmap, TSC1641_LOAD_OV_FLAG, val);
	default:
		return -EOPNOTSUPP;
	}

	ret = regmap_read(regmap, reg, &regval);
	if (ret)
		return ret;

	/* Check if load voltage is out of range */
	if (reg == TSC1641_LOAD_VOLTAGE) {
		/* Register is 15-bit max */
		if (regval & 0x8000)
			return -ENODATA;

		ret  = tsc1641_flag_read(regmap, TSC1641_SAT_FLAG, &sat_flag);
		if (ret)
			return ret;
		/* Out of range conditions per datasheet */
		if (sat_flag && (regval == 0x7FFF || !regval))
			return -ENODATA;
	}

	*val = regval * TSC1641_VLOAD_LSB_MVOLT;
	return 0;
}

/* Chip supports bidirectional (positive or negative) current */
static int tsc1641_curr_read(struct device *dev, u32 attr, long *val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int regval;
	int ret, reg;
	long sat_flag;

	/* Current limits are the shunt under/over voltage limits */
	switch (attr) {
	case hwmon_curr_input:
		reg = TSC1641_CURRENT;
		break;
	case hwmon_curr_min:
		reg = TSC1641_SUL;
		break;
	case hwmon_curr_max:
		reg = TSC1641_SOL;
		break;
	case hwmon_curr_min_alarm:
		return tsc1641_flag_read(regmap, TSC1641_SHUNT_UV_FLAG, val);
	case hwmon_curr_max_alarm:
		return tsc1641_flag_read(regmap, TSC1641_SHUNT_OV_FLAG, val);
	default:
		return -EOPNOTSUPP;
	}
	/*
	 * Current uses shunt voltage, so check if it's out of range.
	 * We report current register in sysfs to stay consistent with internal
	 * power calculations which use current register values
	 */
	if (reg == TSC1641_CURRENT) {
		ret = regmap_read(regmap, TSC1641_SHUNT_VOLTAGE, &regval);
		if (ret)
			return ret;

		ret = tsc1641_flag_read(regmap, TSC1641_SAT_FLAG, &sat_flag);
		if (ret)
			return ret;

		if (sat_flag && (regval == 0x7FFF || regval == 0x8000))
			return -ENODATA;
	}

	ret = regmap_read(regmap, reg, &regval);
	if (ret)
		return ret;

	/* Current in milliamps, signed */
	*val = DIV_ROUND_CLOSEST((s16)regval * data->current_lsb_ua, 1000);
	return 0;
}

static int tsc1641_power_read(struct device *dev, u32 attr, long *val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret, reg;

	switch (attr) {
	case hwmon_power_input:
		reg = TSC1641_POWER;
		break;
	case hwmon_power_max:
		reg = TSC1641_POL;
		break;
	case hwmon_power_max_alarm:
		return tsc1641_flag_read(regmap, TSC1641_POWER_OVER_FLAG, val);
	default:
		return -EOPNOTSUPP;
	}

	ret = regmap_read(regmap, reg, &regval);
	if (ret)
		return ret;

	*val = regval * TSC1641_POWER_LSB_UWATT;
	return 0;
}

static int tsc1641_temp_read(struct device *dev, u32 attr, long *val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int ret, reg;

	switch (attr) {
	case hwmon_temp_input:
		reg = TSC1641_TEMP;
		break;
	case hwmon_temp_max:
		reg = TSC1641_TOL;
		break;
	case hwmon_temp_max_alarm:
		return tsc1641_flag_read(regmap, TSC1641_TEMP_OVER_FLAG, val);
	default:
		return -EOPNOTSUPP;
	}

	ret = regmap_read(regmap, reg, &regval);
	if (ret)
		return ret;

	/* 0x8000 means that TEMP measurement not enabled */
	if (reg == TSC1641_TEMP && regval == 0x8000)
		return -ENODATA;

	/* Both temperature and limit registers are signed */
	*val = (s16)regval * TSC1641_TEMP_LSB_MDEGC;
	return 0;
}

static int tsc1641_in_write(struct device *dev, u32 attr, long val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;
	int reg;

	switch (attr) {
	case hwmon_in_min:
		reg = TSC1641_LUL;
		break;
	case hwmon_in_max:
		reg = TSC1641_LOL;
		break;
	default:
		return -EOPNOTSUPP;
	}
	/* Clamp to full register range */
	val = clamp_val(val, 0, TSC1641_VLOAD_LSB_MVOLT * USHRT_MAX);
	regval = DIV_ROUND_CLOSEST(val, TSC1641_VLOAD_LSB_MVOLT);

	return regmap_write(regmap, reg, regval);
}

static int tsc1641_curr_write(struct device *dev, u32 attr, long val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int reg, regval;

	switch (attr) {
	case hwmon_curr_min:
		reg = TSC1641_SUL;
		break;
	case hwmon_curr_max:
		reg = TSC1641_SOL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Clamp to prevent over/underflow below */
	val = clamp_val(val, -TSC1641_CURR_ABS_MAX_MAMP, TSC1641_CURR_ABS_MAX_MAMP);
	/* Convert val in milliamps to register */
	regval = DIV_ROUND_CLOSEST(val * 1000, data->current_lsb_ua);
	/*
	 * Prevent signed 16-bit overflow.
	 * Integer arithmetic and shunt scaling can quantize values near 0x7FFF/0x8000,
	 * so reading and writing back may not preserve the exact original register value.
	 */
	regval = clamp_val(regval, SHRT_MIN, SHRT_MAX);
	/* SUL and SOL registers are signed */
	return regmap_write(regmap, reg, regval & 0xFFFF);
}

static int tsc1641_power_write(struct device *dev, u32 attr, long val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	unsigned int regval;

	switch (attr) {
	case hwmon_power_max:
		/* Clamp to full register range */
		val = clamp_val(val, 0, TSC1641_POWER_LSB_UWATT * USHRT_MAX);
		regval = DIV_ROUND_CLOSEST(val, TSC1641_POWER_LSB_UWATT);
		return regmap_write(regmap, TSC1641_POL, regval);
	default:
		return -EOPNOTSUPP;
	}
}

static int tsc1641_temp_write(struct device *dev, u32 attr, long val)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	struct regmap *regmap = data->regmap;
	int regval;

	switch (attr) {
	case hwmon_temp_max:
		/* Clamp to full register range */
		val = clamp_val(val, TSC1641_TEMP_LSB_MDEGC * SHRT_MIN,
				TSC1641_TEMP_LSB_MDEGC * SHRT_MAX);
		regval = DIV_ROUND_CLOSEST(val, TSC1641_TEMP_LSB_MDEGC);
		/* TOL register is signed */
		return regmap_write(regmap, TSC1641_TOL, regval & 0xFFFF);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t tsc1641_is_visible(const void *data, enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_update_interval:
			return 0644;
		default:
			break;
		}
		break;
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
			return 0444;
		case hwmon_in_min:
		case hwmon_in_max:
			return 0644;
		case hwmon_in_min_alarm:
		case hwmon_in_max_alarm:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
			return 0444;
		case hwmon_curr_min:
		case hwmon_curr_max:
			return 0644;
		case hwmon_curr_min_alarm:
		case hwmon_curr_max_alarm:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
			return 0444;
		case hwmon_power_max:
			return 0644;
		case hwmon_power_max_alarm:
			return 0444;
		default:
			break;
		}
		break;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
			return 0444;
		case hwmon_temp_max:
			return 0644;
		case hwmon_temp_max_alarm:
			return 0444;
		default:
			break;
		}
		break;
	default:
		break;
	}
	return 0;
}

static int tsc1641_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_chip:
		return tsc1641_chip_read(dev, attr, val);
	case hwmon_in:
		return tsc1641_in_read(dev, attr, val);
	case hwmon_curr:
		return tsc1641_curr_read(dev, attr, val);
	case hwmon_power:
		return tsc1641_power_read(dev, attr, val);
	case hwmon_temp:
		return tsc1641_temp_read(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int tsc1641_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_chip:
		return tsc1641_chip_write(dev, attr, val);
	case hwmon_in:
		return tsc1641_in_write(dev, attr, val);
	case hwmon_curr:
		return tsc1641_curr_write(dev, attr, val);
	case hwmon_power:
		return tsc1641_power_write(dev, attr, val);
	case hwmon_temp:
		return tsc1641_temp_write(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
}

static const struct hwmon_channel_info * const tsc1641_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(in,
			   HWMON_I_INPUT | HWMON_I_MAX | HWMON_I_MAX_ALARM |
			   HWMON_I_MIN | HWMON_I_MIN_ALARM),
	HWMON_CHANNEL_INFO(curr,
			   HWMON_C_INPUT | HWMON_C_MAX | HWMON_C_MAX_ALARM |
			   HWMON_C_MIN | HWMON_C_MIN_ALARM),
	HWMON_CHANNEL_INFO(power,
			   HWMON_P_INPUT | HWMON_P_MAX | HWMON_P_MAX_ALARM),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_ALARM),
	NULL
};

static ssize_t shunt_resistor_show(struct device *dev,
				   struct device_attribute *da, char *buf)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);

	return sysfs_emit(buf, "%li\n", data->rshunt_uohm);
}

static ssize_t shunt_resistor_store(struct device *dev,
				    struct device_attribute *da,
				    const char *buf, size_t count)
{
	struct tsc1641_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int ret;

	ret = kstrtouint(buf, 10, &val);
	if (ret < 0)
		return ret;

	ret = tsc1641_validate_shunt(val);
	if (ret < 0)
		return ret;

	ret = tsc1641_set_shunt(data, val);
	if (ret < 0)
		return ret;
	return count;
}

static const struct hwmon_ops tsc1641_hwmon_ops = {
	.is_visible = tsc1641_is_visible,
	.read = tsc1641_read,
	.write = tsc1641_write,
};

static const struct hwmon_chip_info tsc1641_chip_info = {
	.ops = &tsc1641_hwmon_ops,
	.info = tsc1641_info,
};

static DEVICE_ATTR_RW(shunt_resistor);

/* Shunt resistor value is exposed via sysfs attribute */
static struct attribute *tsc1641_attrs[] = {
	&dev_attr_shunt_resistor.attr,
	NULL,
};
ATTRIBUTE_GROUPS(tsc1641);

static int tsc1641_init(struct device *dev, struct tsc1641_data *data)
{
	struct regmap *regmap = data->regmap;
	bool active_high;
	u32 shunt;
	int ret;

	if (device_property_read_u32(dev, "shunt-resistor-micro-ohms", &shunt) < 0)
		shunt = TSC1641_RSHUNT_DEFAULT;

	if (tsc1641_validate_shunt(shunt) < 0) {
		dev_err(dev, "invalid shunt resistor value %u\n", shunt);
		return -EINVAL;
	}

	ret = tsc1641_set_shunt(data, shunt);
	if (ret < 0)
		return ret;

	ret = regmap_write(regmap, TSC1641_CONFIG, TSC1641_CONFIG_DEFAULT);
	if (ret < 0)
		return ret;

	active_high = device_property_read_bool(dev, "st,alert-polarity-active-high");

	return regmap_write(regmap, TSC1641_MASK, TSC1641_MASK_DEFAULT |
			    FIELD_PREP(TSC1641_ALERT_POL_MASK, active_high));
}

static int tsc1641_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct tsc1641_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &tsc1641_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev, PTR_ERR(data->regmap),
				     "failed to allocate register map\n");

	ret = tsc1641_init(dev, data);
	if (ret < 0)
		return dev_err_probe(dev, ret, "failed to configure device\n");

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &tsc1641_chip_info, tsc1641_groups);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	dev_info(dev, "power monitor %s (Rshunt = %li uOhm)\n",
		 client->name, data->rshunt_uohm);

	return 0;
}

static const struct i2c_device_id tsc1641_id[] = {
	{ "tsc1641", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, tsc1641_id);

static const struct of_device_id __maybe_unused tsc1641_of_match[] = {
	{ .compatible = "st,tsc1641" },
	{ },
};
MODULE_DEVICE_TABLE(of, tsc1641_of_match);

static struct i2c_driver tsc1641_driver = {
	.driver = {
		.name = "tsc1641",
		.of_match_table = of_match_ptr(tsc1641_of_match),
	},
	.probe = tsc1641_probe,
	.id_table = tsc1641_id,
};

module_i2c_driver(tsc1641_driver);

MODULE_AUTHOR("Igor Reznichenko <igor@reznichenko.net>");
MODULE_DESCRIPTION("tsc1641 driver");
MODULE_LICENSE("GPL");
