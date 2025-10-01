// SPDX-License-Identifier: GPL-2.0-only
/*
 * Driver for Texas Instruments INA238 power monitor chip
 * Datasheet: https://www.ti.com/product/ina238
 *
 * Copyright (C) 2021 Nathan Rossi <nathan.rossi@digi.com>
 */

#include <linux/bitops.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/regmap.h>

/* INA238 register definitions */
#define INA238_CONFIG			0x0
#define INA238_ADC_CONFIG		0x1
#define INA238_SHUNT_CALIBRATION	0x2
#define SQ52206_SHUNT_TEMPCO		0x3
#define INA238_SHUNT_VOLTAGE		0x4
#define INA238_BUS_VOLTAGE		0x5
#define INA238_DIE_TEMP			0x6
#define INA238_CURRENT			0x7
#define INA238_POWER			0x8
#define SQ52206_ENERGY			0x9
#define SQ52206_CHARGE			0xa
#define INA238_DIAG_ALERT		0xb
#define INA238_SHUNT_OVER_VOLTAGE	0xc
#define INA238_SHUNT_UNDER_VOLTAGE	0xd
#define INA238_BUS_OVER_VOLTAGE		0xe
#define INA238_BUS_UNDER_VOLTAGE	0xf
#define INA238_TEMP_LIMIT		0x10
#define INA238_POWER_LIMIT		0x11
#define SQ52206_POWER_PEAK		0x20
#define INA238_DEVICE_ID		0x3f /* not available on INA237 */

#define INA238_CONFIG_ADCRANGE		BIT(4)
#define SQ52206_CONFIG_ADCRANGE_HIGH	BIT(4)
#define SQ52206_CONFIG_ADCRANGE_LOW	BIT(3)

#define INA238_DIAG_ALERT_TMPOL		BIT(7)
#define INA238_DIAG_ALERT_SHNTOL	BIT(6)
#define INA238_DIAG_ALERT_SHNTUL	BIT(5)
#define INA238_DIAG_ALERT_BUSOL		BIT(4)
#define INA238_DIAG_ALERT_BUSUL		BIT(3)
#define INA238_DIAG_ALERT_POL		BIT(2)

#define INA238_REGISTERS		0x20

#define INA238_RSHUNT_DEFAULT		2500	/* uOhm */

/* Default configuration of device on reset. */
#define INA238_CONFIG_DEFAULT		0
#define SQ52206_CONFIG_DEFAULT		0x0005
/* 16 sample averaging, 1052us conversion time, continuous mode */
#define INA238_ADC_CONFIG_DEFAULT	0xfb6a
/* Configure alerts to be based on averaged value (SLOWALERT) */
#define INA238_DIAG_ALERT_DEFAULT	0x2000
#define INA238_DIAG_ALERT_APOL		BIT(12)
/*
 * This driver uses a fixed calibration value in order to scale current/power
 * based on a fixed shunt resistor value. This allows for conversion within the
 * device to avoid integer limits whilst current/power accuracy is scaled
 * relative to the shunt resistor value within the driver. This is similar to
 * how the ina2xx driver handles current/power scaling.
 *
 * To achieve the best possible dynamic range, the value of the shunt voltage
 * register should match the value of the current register. With that, the shunt
 * voltage of 0x7fff = 32,767 uV = 163,785 uV matches the maximum current,
 * and no accuracy is lost. Experiments with a real chip show that this is
 * achieved by setting the SHUNT_CAL register to a value of 0x1000 = 4,096.
 * Per datasheet,
 *  SHUNT_CAL = 819.2 x 10^6 x CURRENT_LSB x Rshunt
 *            = 819,200,000 x CURRENT_LSB x Rshunt
 * With SHUNT_CAL set to 4,096, we get
 *  CURRENT_LSB = 4,096 / (819,200,000 x Rshunt)
 * Assuming an Rshunt value of 5 mOhm, we get
 *  CURRENT_LSB = 4,096 / (819,200,000 x 0.005) = 1mA
 * and thus a dynamic range of 1mA ... 32,767mA, which is sufficient for most
 * applications. The actual dynamic range is of course determined by the actual
 * shunt resistor value.
 *
 * Power and energy values are scaled accordingly.
 */
#define INA238_CALIBRATION_VALUE	4096
#define INA238_FIXED_SHUNT		5000

#define INA238_SHUNT_VOLTAGE_LSB	5000	/* 5 uV/lsb, in nV */
#define INA238_BUS_VOLTAGE_LSB		3125000	/* 3.125 mV/lsb, in nV */
#define SQ52206_BUS_VOLTAGE_LSB		3750000	/* 3.75 mV/lsb, in nV */

#define NUNIT_PER_MUNIT		1000000	/* n[AV] -> m[AV] */

static const struct regmap_config ina238_regmap_config = {
	.max_register = INA238_REGISTERS,
	.reg_bits = 8,
	.val_bits = 16,
};

enum ina238_ids { ina228, ina237, ina238, ina700, ina780, sq52206 };

struct ina238_config {
	bool has_20bit_voltage_current; /* vshunt, vbus and current are 20-bit fields */
	bool has_power_highest;		/* chip detection power peak */
	bool has_energy;		/* chip detection energy */
	u8 temp_resolution;		/* temperature register resolution in bit */
	u16 config_default;		/* Power-on default state */
	u32 power_calculate_factor;	/* fixed parameter for power calculation, from datasheet */
	u32 bus_voltage_lsb;		/* bus voltage LSB, in nV */
	int current_lsb;		/* current LSB, in uA */
};

struct ina238_data {
	const struct ina238_config *config;
	struct i2c_client *client;
	struct mutex config_lock;
	struct regmap *regmap;
	u32 rshunt;
	int gain;
	u32 voltage_lsb[2];		/* shunt, bus voltage LSB, in nV */
	int current_lsb;		/* current LSB, in uA */
	int power_lsb;			/* power LSB, in uW */
	int energy_lsb;			/* energy LSB, in uJ */
};

static const struct ina238_config ina238_config[] = {
	[ina228] = {
		.has_20bit_voltage_current = true,
		.has_energy = true,
		.has_power_highest = false,
		.power_calculate_factor = 20,
		.config_default = INA238_CONFIG_DEFAULT,
		.bus_voltage_lsb = INA238_BUS_VOLTAGE_LSB,
		.temp_resolution = 16,
	},
	[ina237] = {
		.has_20bit_voltage_current = false,
		.has_energy = false,
		.has_power_highest = false,
		.power_calculate_factor = 20,
		.config_default = INA238_CONFIG_DEFAULT,
		.bus_voltage_lsb = INA238_BUS_VOLTAGE_LSB,
		.temp_resolution = 12,
	},
	[ina238] = {
		.has_20bit_voltage_current = false,
		.has_energy = false,
		.has_power_highest = false,
		.power_calculate_factor = 20,
		.config_default = INA238_CONFIG_DEFAULT,
		.bus_voltage_lsb = INA238_BUS_VOLTAGE_LSB,
		.temp_resolution = 12,
	},
	[ina700] = {
		.has_20bit_voltage_current = false,
		.has_energy = true,
		.has_power_highest = false,
		.power_calculate_factor = 20,
		.config_default = INA238_CONFIG_DEFAULT,
		.bus_voltage_lsb = INA238_BUS_VOLTAGE_LSB,
		.temp_resolution = 12,
		.current_lsb = 480,
	},
	[ina780] = {
		.has_20bit_voltage_current = false,
		.has_energy = true,
		.has_power_highest = false,
		.power_calculate_factor = 20,
		.config_default = INA238_CONFIG_DEFAULT,
		.bus_voltage_lsb = INA238_BUS_VOLTAGE_LSB,
		.temp_resolution = 12,
		.current_lsb = 2400,
	},
	[sq52206] = {
		.has_20bit_voltage_current = false,
		.has_energy = true,
		.has_power_highest = true,
		.power_calculate_factor = 24,
		.config_default = SQ52206_CONFIG_DEFAULT,
		.bus_voltage_lsb = SQ52206_BUS_VOLTAGE_LSB,
		.temp_resolution = 16,
	},
};

static int ina238_read_reg24(const struct i2c_client *client, u8 reg, u32 *val)
{
	u8 data[3];
	int err;

	/* 24-bit register read */
	err = i2c_smbus_read_i2c_block_data(client, reg, 3, data);
	if (err < 0)
		return err;
	if (err != 3)
		return -EIO;
	*val = (data[0] << 16) | (data[1] << 8) | data[2];

	return 0;
}

static int ina238_read_reg40(const struct i2c_client *client, u8 reg, u64 *val)
{
	u8 data[5];
	u32 low;
	int err;

	/* 40-bit register read */
	err = i2c_smbus_read_i2c_block_data(client, reg, 5, data);
	if (err < 0)
		return err;
	if (err != 5)
		return -EIO;
	low = (data[1] << 24) | (data[2] << 16) | (data[3] << 8) | data[4];
	*val = ((long long)data[0] << 32) | low;

	return 0;
}

static int ina238_read_field_s20(const struct i2c_client *client, u8 reg, s32 *val)
{
	u32 regval;
	int err;

	err = ina238_read_reg24(client, reg, &regval);
	if (err)
		return err;

	/* bits 3-0 Reserved, always zero */
	regval >>= 4;

	*val = sign_extend32(regval, 19);

	return 0;
}

static int ina228_read_voltage(struct ina238_data *data, int channel, long *val)
{
	int reg = channel ? INA238_BUS_VOLTAGE : INA238_CURRENT;
	u32 lsb = data->voltage_lsb[channel];
	u32 factor = NUNIT_PER_MUNIT;
	int err, regval;

	if (data->config->has_20bit_voltage_current) {
		err = ina238_read_field_s20(data->client, reg, &regval);
		if (err)
			return err;
		/* Adjust accuracy: LSB in units of 500 pV */
		lsb /= 8;
		factor *= 2;
	} else {
		err = regmap_read(data->regmap, reg, &regval);
		if (err)
			return err;
		regval = (s16)regval;
	}

	*val = DIV_S64_ROUND_CLOSEST((s64)regval * lsb, factor);
	return 0;
}

static int ina238_read_in(struct device *dev, u32 attr, int channel,
			  long *val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	int reg, mask = 0;
	int regval;
	int err;

	if (attr == hwmon_in_input)
		return ina228_read_voltage(data, channel, val);

	switch (channel) {
	case 0:
		switch (attr) {
		case hwmon_in_max:
			reg = INA238_SHUNT_OVER_VOLTAGE;
			break;
		case hwmon_in_min:
			reg = INA238_SHUNT_UNDER_VOLTAGE;
			break;
		case hwmon_in_max_alarm:
			reg = INA238_DIAG_ALERT;
			mask = INA238_DIAG_ALERT_SHNTOL;
			break;
		case hwmon_in_min_alarm:
			reg = INA238_DIAG_ALERT;
			mask = INA238_DIAG_ALERT_SHNTUL;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	case 1:
		switch (attr) {
		case hwmon_in_max:
			reg = INA238_BUS_OVER_VOLTAGE;
			break;
		case hwmon_in_min:
			reg = INA238_BUS_UNDER_VOLTAGE;
			break;
		case hwmon_in_max_alarm:
			reg = INA238_DIAG_ALERT;
			mask = INA238_DIAG_ALERT_BUSOL;
			break;
		case hwmon_in_min_alarm:
			reg = INA238_DIAG_ALERT;
			mask = INA238_DIAG_ALERT_BUSUL;
			break;
		default:
			return -EOPNOTSUPP;
		}
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = regmap_read(data->regmap, reg, &regval);
	if (err < 0)
		return err;

	if (mask)
		*val = !!(regval & mask);
	else
		*val = DIV_S64_ROUND_CLOSEST((s64)(s16)regval * data->voltage_lsb[channel],
					     NUNIT_PER_MUNIT);

	return 0;
}

static int ina238_write_in(struct device *dev, u32 attr, int channel, long val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	static const int low_limits[2] = {-164, 0};
	static const int high_limits[2] = {164, 150000};
	static const u8 low_regs[2] = {INA238_SHUNT_UNDER_VOLTAGE, INA238_BUS_UNDER_VOLTAGE};
	static const u8 high_regs[2] = {INA238_SHUNT_OVER_VOLTAGE, INA238_BUS_OVER_VOLTAGE};
	int regval;

	/* Initial clamp to avoid overflows */
	val = clamp_val(val, low_limits[channel], high_limits[channel]);
	val = DIV_S64_ROUND_CLOSEST((s64)val * NUNIT_PER_MUNIT, data->voltage_lsb[channel]);
	/* Final clamp to register limits */
	regval = clamp_val(val, S16_MIN, S16_MAX) & 0xffff;

	switch (attr) {
	case hwmon_in_min:
		return regmap_write(data->regmap, low_regs[channel], regval);
	case hwmon_in_max:
		return regmap_write(data->regmap, high_regs[channel], regval);
	default:
		return -EOPNOTSUPP;
	}
}

static int __ina238_read_curr(struct ina238_data *data, long *val)
{
	u32 lsb = data->current_lsb;
	int err, regval;

	if (data->config->has_20bit_voltage_current) {
		err = ina238_read_field_s20(data->client, INA238_CURRENT, &regval);
		if (err)
			return err;
		lsb /= 16;	/* Adjust accuracy */
	} else {
		err = regmap_read(data->regmap, INA238_CURRENT, &regval);
		if (err)
			return err;
		regval = (s16)regval;
	}

	*val = DIV_S64_ROUND_CLOSEST((s64)regval * lsb, 1000);
	return 0;
}

static int ina238_read_curr(struct device *dev, u32 attr, long *val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	int reg, mask = 0;
	int regval;
	int err;

	if (attr == hwmon_curr_input)
		return __ina238_read_curr(data, val);

	switch (attr) {
	case hwmon_curr_min:
		reg = INA238_SHUNT_UNDER_VOLTAGE;
		break;
	case hwmon_curr_min_alarm:
		reg = INA238_DIAG_ALERT;
		mask = INA238_DIAG_ALERT_SHNTUL;
		break;
	case hwmon_curr_max:
		reg = INA238_SHUNT_OVER_VOLTAGE;
		break;
	case hwmon_curr_max_alarm:
		reg = INA238_DIAG_ALERT;
		mask = INA238_DIAG_ALERT_SHNTOL;
		break;
	default:
		return -EOPNOTSUPP;
	}

	err = regmap_read(data->regmap, reg, &regval);
	if (err < 0)
		return err;

	if (mask)
		*val = !!(regval & mask);
	else
		*val = DIV_S64_ROUND_CLOSEST((s64)(s16)regval * data->current_lsb, 1000);

	return 0;
}

static int ina238_write_curr(struct device *dev, u32 attr, long val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	int regval;

	/* Set baseline range to avoid over/underflows */
	val = clamp_val(val, -1000000, 1000000);
	/* Scale */
	val = DIV_ROUND_CLOSEST(val * 1000, data->current_lsb);
	/* Clamp to register size */
	regval = clamp_val(val, S16_MIN, S16_MAX) & 0xffff;

	switch (attr) {
	case hwmon_curr_min:
		return regmap_write(data->regmap, INA238_SHUNT_UNDER_VOLTAGE,
				    regval);
	case hwmon_curr_max:
		return regmap_write(data->regmap, INA238_SHUNT_OVER_VOLTAGE,
				    regval);
	default:
		return -EOPNOTSUPP;
	}
}

static int ina238_read_power(struct device *dev, u32 attr, long *val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	long long power;
	int regval;
	int err;

	switch (attr) {
	case hwmon_power_input:
		err = ina238_read_reg24(data->client, INA238_POWER, &regval);
		if (err)
			return err;

		power = (long long)regval * data->power_lsb;
		/* Clamp value to maximum value of long */
		*val = clamp_val(power, 0, LONG_MAX);
		break;
	case hwmon_power_input_highest:
		err = ina238_read_reg24(data->client, SQ52206_POWER_PEAK, &regval);
		if (err)
			return err;

		power = (long long)regval * data->power_lsb;
		/* Clamp value to maximum value of long */
		*val = clamp_val(power, 0, LONG_MAX);
		break;
	case hwmon_power_max:
		err = regmap_read(data->regmap, INA238_POWER_LIMIT, &regval);
		if (err)
			return err;

		/*
		 * Truncated 24-bit compare register, lower 8-bits are
		 * truncated. Same conversion to/from uW as POWER register.
		 */
		power = ((long long)regval << 8) * data->power_lsb;
		/* Clamp value to maximum value of long */
		*val = clamp_val(power, 0, LONG_MAX);
		break;
	case hwmon_power_max_alarm:
		err = regmap_read(data->regmap, INA238_DIAG_ALERT, &regval);
		if (err)
			return err;

		*val = !!(regval & INA238_DIAG_ALERT_POL);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static int ina238_write_power_max(struct device *dev, long val)
{
	struct ina238_data *data = dev_get_drvdata(dev);

	/*
	 * Unsigned postive values. Compared against the 24-bit power register,
	 * lower 8-bits are truncated. Same conversion to/from uW as POWER
	 * register.
	 * The first clamp_val() is to establish a baseline to avoid overflows.
	 */
	val = clamp_val(val, 0, LONG_MAX / 2);
	val = DIV_ROUND_CLOSEST(val, data->power_lsb);
	val = clamp_val(val >> 8, 0, U16_MAX);

	return regmap_write(data->regmap, INA238_POWER_LIMIT, val);
}

static int ina238_temp_from_reg(s16 regval, u8 resolution)
{
	return ((regval >> (16 - resolution)) * 1000) >> (resolution - 9);
}

static int ina238_read_temp(struct device *dev, u32 attr, long *val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	int regval;
	int err;

	switch (attr) {
	case hwmon_temp_input:
		err = regmap_read(data->regmap, INA238_DIE_TEMP, &regval);
		if (err)
			return err;
		*val = ina238_temp_from_reg(regval, data->config->temp_resolution);
		break;
	case hwmon_temp_max:
		err = regmap_read(data->regmap, INA238_TEMP_LIMIT, &regval);
		if (err)
			return err;
		/* Signed, result in mC */
		*val = ina238_temp_from_reg(regval, data->config->temp_resolution);
		break;
	case hwmon_temp_max_alarm:
		err = regmap_read(data->regmap, INA238_DIAG_ALERT, &regval);
		if (err)
			return err;

		*val = !!(regval & INA238_DIAG_ALERT_TMPOL);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return 0;
}

static u16 ina238_temp_to_reg(long val, u8 resolution)
{
	int fraction = 1000 - DIV_ROUND_CLOSEST(1000, BIT(resolution - 9));

	val = clamp_val(val, -255000 - fraction, 255000 + fraction);

	return (DIV_ROUND_CLOSEST(val << (resolution - 9), 1000) << (16 - resolution)) & 0xffff;
}

static int ina238_write_temp_max(struct device *dev, long val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	int regval;

	regval = ina238_temp_to_reg(val, data->config->temp_resolution);
	return regmap_write(data->regmap, INA238_TEMP_LIMIT, regval);
}

static int ina238_read_energy(struct device *dev, s64 *energy)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	u64 regval;
	int ret;

	ret = ina238_read_reg40(data->client, SQ52206_ENERGY, &regval);
	if (ret)
		return ret;

	/* result in uJ */
	*energy = regval * data->energy_lsb;
	return 0;
}

static int ina238_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_in:
		return ina238_read_in(dev, attr, channel, val);
	case hwmon_curr:
		return ina238_read_curr(dev, attr, val);
	case hwmon_power:
		return ina238_read_power(dev, attr, val);
	case hwmon_energy64:
		return ina238_read_energy(dev, (s64 *)val);
	case hwmon_temp:
		return ina238_read_temp(dev, attr, val);
	default:
		return -EOPNOTSUPP;
	}
	return 0;
}

static int ina238_write(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long val)
{
	struct ina238_data *data = dev_get_drvdata(dev);
	int err;

	mutex_lock(&data->config_lock);

	switch (type) {
	case hwmon_in:
		err = ina238_write_in(dev, attr, channel, val);
		break;
	case hwmon_curr:
		err = ina238_write_curr(dev, attr, val);
		break;
	case hwmon_power:
		err = ina238_write_power_max(dev, val);
		break;
	case hwmon_temp:
		err = ina238_write_temp_max(dev, val);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&data->config_lock);
	return err;
}

static umode_t ina238_is_visible(const void *drvdata,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	const struct ina238_data *data = drvdata;
	bool has_power_highest = data->config->has_power_highest;
	bool has_energy = data->config->has_energy;

	switch (type) {
	case hwmon_in:
		switch (attr) {
		case hwmon_in_input:
		case hwmon_in_max_alarm:
		case hwmon_in_min_alarm:
			return 0444;
		case hwmon_in_max:
		case hwmon_in_min:
			return 0644;
		default:
			return 0;
		}
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_max_alarm:
		case hwmon_curr_min_alarm:
			return 0444;
		case hwmon_curr_max:
		case hwmon_curr_min:
			return 0644;
		default:
			return 0;
		}
	case hwmon_power:
		switch (attr) {
		case hwmon_power_input:
		case hwmon_power_max_alarm:
			return 0444;
		case hwmon_power_max:
			return 0644;
		case hwmon_power_input_highest:
			if (has_power_highest)
				return 0444;
			return 0;
		default:
			return 0;
		}
	case hwmon_energy64:
		/* hwmon_energy_input */
		if (has_energy)
			return 0444;
		return 0;
	case hwmon_temp:
		switch (attr) {
		case hwmon_temp_input:
		case hwmon_temp_max_alarm:
			return 0444;
		case hwmon_temp_max:
			return 0644;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

#define INA238_HWMON_IN_CONFIG (HWMON_I_INPUT | \
				HWMON_I_MAX | HWMON_I_MAX_ALARM | \
				HWMON_I_MIN | HWMON_I_MIN_ALARM)

static const struct hwmon_channel_info * const ina238_info[] = {
	HWMON_CHANNEL_INFO(in,
			   /* 0: shunt voltage */
			   INA238_HWMON_IN_CONFIG,
			   /* 1: bus voltage */
			   INA238_HWMON_IN_CONFIG),
	HWMON_CHANNEL_INFO(curr,
			   /* 0: current through shunt */
			   HWMON_C_INPUT | HWMON_C_MIN | HWMON_C_MIN_ALARM |
			   HWMON_C_MAX | HWMON_C_MAX_ALARM),
	HWMON_CHANNEL_INFO(power,
			   /* 0: power */
			   HWMON_P_INPUT | HWMON_P_MAX |
			   HWMON_P_MAX_ALARM | HWMON_P_INPUT_HIGHEST),
	HWMON_CHANNEL_INFO(energy64,
			   HWMON_E_INPUT),
	HWMON_CHANNEL_INFO(temp,
			   /* 0: die temperature */
			   HWMON_T_INPUT | HWMON_T_MAX | HWMON_T_MAX_ALARM),
	NULL
};

static const struct hwmon_ops ina238_hwmon_ops = {
	.is_visible = ina238_is_visible,
	.read = ina238_read,
	.write = ina238_write,
};

static const struct hwmon_chip_info ina238_chip_info = {
	.ops = &ina238_hwmon_ops,
	.info = ina238_info,
};

static int ina238_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct device *hwmon_dev;
	struct ina238_data *data;
	enum ina238_ids chip;
	int config;
	int ret;

	chip = (uintptr_t)i2c_get_match_data(client);

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	/* set the device type */
	data->config = &ina238_config[chip];

	mutex_init(&data->config_lock);

	data->regmap = devm_regmap_init_i2c(client, &ina238_regmap_config);
	if (IS_ERR(data->regmap)) {
		dev_err(dev, "failed to allocate register map\n");
		return PTR_ERR(data->regmap);
	}

	/* Setup CONFIG register */
	config = data->config->config_default;
	if (data->config->current_lsb) {
		data->voltage_lsb[0] = INA238_SHUNT_VOLTAGE_LSB;
		data->current_lsb = data->config->current_lsb;
	} else {
		/* load shunt value */
		if (device_property_read_u32(dev, "shunt-resistor", &data->rshunt) < 0)
			data->rshunt = INA238_RSHUNT_DEFAULT;
		if (data->rshunt == 0) {
			dev_err(dev, "invalid shunt resister value %u\n", data->rshunt);
			return -EINVAL;
		}

		/* load shunt gain value */
		if (device_property_read_u32(dev, "ti,shunt-gain", &data->gain) < 0)
			data->gain = 4;	/* Default of ADCRANGE = 0 */
		if (data->gain != 1 && data->gain != 2 && data->gain != 4) {
			dev_err(dev, "invalid shunt gain value %u\n", data->gain);
			return -EINVAL;
		}

		/* Setup SHUNT_CALIBRATION register with fixed value */
		ret = regmap_write(data->regmap, INA238_SHUNT_CALIBRATION,
				   INA238_CALIBRATION_VALUE);
		if (ret < 0) {
			dev_err(dev, "error configuring the device: %d\n", ret);
			return -ENODEV;
		}
		if (chip == sq52206) {
			if (data->gain == 1)		/* ADCRANGE = 10/11 is /1 */
				config |= SQ52206_CONFIG_ADCRANGE_HIGH;
			else if (data->gain == 2)	/* ADCRANGE = 01 is /2 */
				config |= SQ52206_CONFIG_ADCRANGE_LOW;
		} else if (data->gain == 1) {		/* ADCRANGE = 1 is /1 */
			config |= INA238_CONFIG_ADCRANGE;
		}
		data->voltage_lsb[0] = INA238_SHUNT_VOLTAGE_LSB * data->gain / 4;
		data->current_lsb = DIV_U64_ROUND_CLOSEST(250ULL * INA238_FIXED_SHUNT * data->gain,
							  data->rshunt);
	}

	ret = regmap_write(data->regmap, INA238_CONFIG, config);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	/* Setup ADC_CONFIG register */
	ret = regmap_write(data->regmap, INA238_ADC_CONFIG,
			   INA238_ADC_CONFIG_DEFAULT);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	/* Setup alert/alarm configuration */
	config = INA238_DIAG_ALERT_DEFAULT;
	if (device_property_read_bool(dev, "ti,alert-polarity-active-high"))
		config |= INA238_DIAG_ALERT_APOL;

	ret = regmap_write(data->regmap, INA238_DIAG_ALERT, config);
	if (ret < 0) {
		dev_err(dev, "error configuring the device: %d\n", ret);
		return -ENODEV;
	}

	data->voltage_lsb[1] = data->config->bus_voltage_lsb;

	data->power_lsb = DIV_ROUND_CLOSEST(data->current_lsb *
					    data->config->power_calculate_factor,
					    100);

	data->energy_lsb = data->power_lsb * 16;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, data,
							 &ina238_chip_info, NULL);
	if (IS_ERR(hwmon_dev))
		return PTR_ERR(hwmon_dev);

	if (data->rshunt)
		dev_info(dev, "power monitor %s (Rshunt = %u uOhm, gain = %u)\n",
			 client->name, data->rshunt, data->gain);

	return 0;
}

static const struct i2c_device_id ina238_id[] = {
	{ "ina228", ina228 },
	{ "ina237", ina237 },
	{ "ina238", ina238 },
	{ "ina700", ina700 },
	{ "ina780", ina780 },
	{ "sq52206", sq52206 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ina238_id);

static const struct of_device_id __maybe_unused ina238_of_match[] = {
	{
		.compatible = "ti,ina228",
		.data = (void *)ina228
	},
	{
		.compatible = "ti,ina237",
		.data = (void *)ina237
	},
	{
		.compatible = "ti,ina238",
		.data = (void *)ina238
	},
	{
		.compatible = "ti,ina700",
		.data = (void *)ina700
	},
	{
		.compatible = "ti,ina780",
		.data = (void *)ina780
	},
	{
		.compatible = "silergy,sq52206",
		.data = (void *)sq52206
	},
	{ }
};
MODULE_DEVICE_TABLE(of, ina238_of_match);

static struct i2c_driver ina238_driver = {
	.driver = {
		.name	= "ina238",
		.of_match_table = of_match_ptr(ina238_of_match),
	},
	.probe		= ina238_probe,
	.id_table	= ina238_id,
};

module_i2c_driver(ina238_driver);

MODULE_AUTHOR("Nathan Rossi <nathan.rossi@digi.com>");
MODULE_DESCRIPTION("ina238 driver");
MODULE_LICENSE("GPL");
