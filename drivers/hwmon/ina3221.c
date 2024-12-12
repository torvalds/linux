// SPDX-License-Identifier: GPL-2.0-only
/*
 * INA3221 Triple Current/Voltage Monitor
 *
 * Copyright (C) 2016 Texas Instruments Incorporated - https://www.ti.com/
 *	Andrew F. Davis <afd@ti.com>
 */

#include <linux/debugfs.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/of.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

#define INA3221_DRIVER_NAME		"ina3221"

#define INA3221_CONFIG			0x00
#define INA3221_SHUNT1			0x01
#define INA3221_BUS1			0x02
#define INA3221_SHUNT2			0x03
#define INA3221_BUS2			0x04
#define INA3221_SHUNT3			0x05
#define INA3221_BUS3			0x06
#define INA3221_CRIT1			0x07
#define INA3221_WARN1			0x08
#define INA3221_CRIT2			0x09
#define INA3221_WARN2			0x0a
#define INA3221_CRIT3			0x0b
#define INA3221_WARN3			0x0c
#define INA3221_SHUNT_SUM		0x0d
#define INA3221_CRIT_SUM		0x0e
#define INA3221_MASK_ENABLE		0x0f

#define INA3221_CONFIG_MODE_MASK	GENMASK(2, 0)
#define INA3221_CONFIG_MODE_POWERDOWN	0
#define INA3221_CONFIG_MODE_SHUNT	BIT(0)
#define INA3221_CONFIG_MODE_BUS		BIT(1)
#define INA3221_CONFIG_MODE_CONTINUOUS	BIT(2)
#define INA3221_CONFIG_VSH_CT_SHIFT	3
#define INA3221_CONFIG_VSH_CT_MASK	GENMASK(5, 3)
#define INA3221_CONFIG_VSH_CT(x)	(((x) & GENMASK(5, 3)) >> 3)
#define INA3221_CONFIG_VBUS_CT_SHIFT	6
#define INA3221_CONFIG_VBUS_CT_MASK	GENMASK(8, 6)
#define INA3221_CONFIG_VBUS_CT(x)	(((x) & GENMASK(8, 6)) >> 6)
#define INA3221_CONFIG_AVG_SHIFT	9
#define INA3221_CONFIG_AVG_MASK		GENMASK(11, 9)
#define INA3221_CONFIG_AVG(x)		(((x) & GENMASK(11, 9)) >> 9)
#define INA3221_CONFIG_CHs_EN_MASK	GENMASK(14, 12)
#define INA3221_CONFIG_CHx_EN(x)	BIT(14 - (x))

#define INA3221_MASK_ENABLE_SCC_MASK	GENMASK(14, 12)

#define INA3221_CONFIG_DEFAULT		0x7127
#define INA3221_RSHUNT_DEFAULT		10000

enum ina3221_fields {
	/* Configuration */
	F_RST,

	/* Status Flags */
	F_CVRF,

	/* Warning Flags */
	F_WF3, F_WF2, F_WF1,

	/* Alert Flags: SF is the summation-alert flag */
	F_SF, F_CF3, F_CF2, F_CF1,

	/* sentinel */
	F_MAX_FIELDS
};

static const struct reg_field ina3221_reg_fields[] = {
	[F_RST] = REG_FIELD(INA3221_CONFIG, 15, 15),

	[F_CVRF] = REG_FIELD(INA3221_MASK_ENABLE, 0, 0),
	[F_WF3] = REG_FIELD(INA3221_MASK_ENABLE, 3, 3),
	[F_WF2] = REG_FIELD(INA3221_MASK_ENABLE, 4, 4),
	[F_WF1] = REG_FIELD(INA3221_MASK_ENABLE, 5, 5),
	[F_SF] = REG_FIELD(INA3221_MASK_ENABLE, 6, 6),
	[F_CF3] = REG_FIELD(INA3221_MASK_ENABLE, 7, 7),
	[F_CF2] = REG_FIELD(INA3221_MASK_ENABLE, 8, 8),
	[F_CF1] = REG_FIELD(INA3221_MASK_ENABLE, 9, 9),
};

enum ina3221_channels {
	INA3221_CHANNEL1,
	INA3221_CHANNEL2,
	INA3221_CHANNEL3,
	INA3221_NUM_CHANNELS
};

/**
 * struct ina3221_input - channel input source specific information
 * @label: label of channel input source
 * @shunt_resistor: shunt resistor value of channel input source
 * @disconnected: connection status of channel input source
 * @summation_disable: channel summation status of input source
 */
struct ina3221_input {
	const char *label;
	int shunt_resistor;
	bool disconnected;
	bool summation_disable;
};

/**
 * struct ina3221_data - device specific information
 * @pm_dev: Device pointer for pm runtime
 * @regmap: Register map of the device
 * @fields: Register fields of the device
 * @inputs: Array of channel input source specific structures
 * @lock: mutex lock to serialize sysfs attribute accesses
 * @debugfs: Pointer to debugfs entry for device
 * @reg_config: Register value of INA3221_CONFIG
 * @summation_shunt_resistor: equivalent shunt resistor value for summation
 * @summation_channel_control: Value written to SCC field in INA3221_MASK_ENABLE
 * @single_shot: running in single-shot operating mode
 */
struct ina3221_data {
	struct device *pm_dev;
	struct regmap *regmap;
	struct regmap_field *fields[F_MAX_FIELDS];
	struct ina3221_input inputs[INA3221_NUM_CHANNELS];
	struct mutex lock;
	struct dentry *debugfs;
	u32 reg_config;
	int summation_shunt_resistor;
	u32 summation_channel_control;

	bool single_shot;
};

static inline bool ina3221_is_enabled(struct ina3221_data *ina, int channel)
{
	/* Summation channel checks shunt resistor values */
	if (channel > INA3221_CHANNEL3)
		return ina->summation_shunt_resistor != 0;

	return pm_runtime_active(ina->pm_dev) &&
	       (ina->reg_config & INA3221_CONFIG_CHx_EN(channel));
}

/*
 * Helper function to return the resistor value for current summation.
 *
 * There is a condition to calculate current summation -- all the shunt
 * resistor values should be the same, so as to simply fit the formula:
 *     current summation = shunt voltage summation / shunt resistor
 *
 * Returns the equivalent shunt resistor value on success or 0 on failure
 */
static inline int ina3221_summation_shunt_resistor(struct ina3221_data *ina)
{
	struct ina3221_input *input = ina->inputs;
	int i, shunt_resistor = 0;

	for (i = 0; i < INA3221_NUM_CHANNELS; i++) {
		if (input[i].disconnected || !input[i].shunt_resistor ||
		    input[i].summation_disable)
			continue;
		if (!shunt_resistor) {
			/* Found the reference shunt resistor value */
			shunt_resistor = input[i].shunt_resistor;
		} else {
			/* No summation if resistor values are different */
			if (shunt_resistor != input[i].shunt_resistor)
				return 0;
		}
	}

	return shunt_resistor;
}

/* Lookup table for Bus and Shunt conversion times in usec */
static const u16 ina3221_conv_time[] = {
	140, 204, 332, 588, 1100, 2116, 4156, 8244,
};

/* Lookup table for number of samples using in averaging mode */
static const int ina3221_avg_samples[] = {
	1, 4, 16, 64, 128, 256, 512, 1024,
};

/* Converting update_interval in msec to conversion time in usec */
static inline u32 ina3221_interval_ms_to_conv_time(u16 config, int interval)
{
	u32 channels = hweight16(config & INA3221_CONFIG_CHs_EN_MASK);
	u32 samples_idx = INA3221_CONFIG_AVG(config);
	u32 samples = ina3221_avg_samples[samples_idx];

	/* Bisect the result to Bus and Shunt conversion times */
	return DIV_ROUND_CLOSEST(interval * 1000 / 2, channels * samples);
}

/* Converting CONFIG register value to update_interval in usec */
static inline u32 ina3221_reg_to_interval_us(u16 config)
{
	u32 channels = hweight16(config & INA3221_CONFIG_CHs_EN_MASK);
	u32 vbus_ct_idx = INA3221_CONFIG_VBUS_CT(config);
	u32 vsh_ct_idx = INA3221_CONFIG_VSH_CT(config);
	u32 vbus_ct = ina3221_conv_time[vbus_ct_idx];
	u32 vsh_ct = ina3221_conv_time[vsh_ct_idx];

	/* Calculate total conversion time */
	return channels * (vbus_ct + vsh_ct);
}

static inline int ina3221_wait_for_data(struct ina3221_data *ina)
{
	u32 wait, cvrf;

	wait = ina3221_reg_to_interval_us(ina->reg_config);

	/* Polling the CVRF bit to make sure read data is ready */
	return regmap_field_read_poll_timeout(ina->fields[F_CVRF],
					      cvrf, cvrf, wait, wait * 2);
}

static int ina3221_read_value(struct ina3221_data *ina, unsigned int reg,
			      int *val)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(ina->regmap, reg, &regval);
	if (ret)
		return ret;

	/*
	 * Shunt Voltage Sum register has 14-bit value with 1-bit shift
	 * Other Shunt Voltage registers have 12 bits with 3-bit shift
	 */
	if (reg == INA3221_SHUNT_SUM || reg == INA3221_CRIT_SUM)
		*val = sign_extend32(regval >> 1, 14);
	else
		*val = sign_extend32(regval >> 3, 12);

	return 0;
}

static const u8 ina3221_in_reg[] = {
	INA3221_BUS1,
	INA3221_BUS2,
	INA3221_BUS3,
	INA3221_SHUNT1,
	INA3221_SHUNT2,
	INA3221_SHUNT3,
	INA3221_SHUNT_SUM,
};

static int ina3221_read_chip(struct device *dev, u32 attr, long *val)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int regval;

	switch (attr) {
	case hwmon_chip_samples:
		regval = INA3221_CONFIG_AVG(ina->reg_config);
		*val = ina3221_avg_samples[regval];
		return 0;
	case hwmon_chip_update_interval:
		/* Return in msec */
		*val = ina3221_reg_to_interval_us(ina->reg_config);
		*val = DIV_ROUND_CLOSEST(*val, 1000);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int ina3221_read_in(struct device *dev, u32 attr, int channel, long *val)
{
	const bool is_shunt = channel > INA3221_CHANNEL3;
	struct ina3221_data *ina = dev_get_drvdata(dev);
	u8 reg = ina3221_in_reg[channel];
	int regval, ret;

	/*
	 * Translate shunt channel index to sensor channel index except
	 * the 7th channel (6 since being 0-aligned) is for summation.
	 */
	if (channel != 6)
		channel %= INA3221_NUM_CHANNELS;

	switch (attr) {
	case hwmon_in_input:
		if (!ina3221_is_enabled(ina, channel))
			return -ENODATA;

		/* Write CONFIG register to trigger a single-shot measurement */
		if (ina->single_shot) {
			regmap_write(ina->regmap, INA3221_CONFIG,
				     ina->reg_config);

			ret = ina3221_wait_for_data(ina);
			if (ret)
				return ret;
		}

		ret = ina3221_read_value(ina, reg, &regval);
		if (ret)
			return ret;

		/*
		 * Scale of shunt voltage (uV): LSB is 40uV
		 * Scale of bus voltage (mV): LSB is 8mV
		 */
		*val = regval * (is_shunt ? 40 : 8);
		return 0;
	case hwmon_in_enable:
		*val = ina3221_is_enabled(ina, channel);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static const u8 ina3221_curr_reg[][INA3221_NUM_CHANNELS + 1] = {
	[hwmon_curr_input] = { INA3221_SHUNT1, INA3221_SHUNT2,
			       INA3221_SHUNT3, INA3221_SHUNT_SUM },
	[hwmon_curr_max] = { INA3221_WARN1, INA3221_WARN2, INA3221_WARN3, 0 },
	[hwmon_curr_crit] = { INA3221_CRIT1, INA3221_CRIT2,
			      INA3221_CRIT3, INA3221_CRIT_SUM },
	[hwmon_curr_max_alarm] = { F_WF1, F_WF2, F_WF3, 0 },
	[hwmon_curr_crit_alarm] = { F_CF1, F_CF2, F_CF3, F_SF },
};

static int ina3221_read_curr(struct device *dev, u32 attr,
			     int channel, long *val)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	struct ina3221_input *input = ina->inputs;
	u8 reg = ina3221_curr_reg[attr][channel];
	int resistance_uo, voltage_nv;
	int regval, ret;

	if (channel > INA3221_CHANNEL3)
		resistance_uo = ina->summation_shunt_resistor;
	else
		resistance_uo = input[channel].shunt_resistor;

	switch (attr) {
	case hwmon_curr_input:
		if (!ina3221_is_enabled(ina, channel))
			return -ENODATA;

		/* Write CONFIG register to trigger a single-shot measurement */
		if (ina->single_shot) {
			regmap_write(ina->regmap, INA3221_CONFIG,
				     ina->reg_config);

			ret = ina3221_wait_for_data(ina);
			if (ret)
				return ret;
		}

		fallthrough;
	case hwmon_curr_crit:
	case hwmon_curr_max:
		if (!resistance_uo)
			return -ENODATA;

		ret = ina3221_read_value(ina, reg, &regval);
		if (ret)
			return ret;

		/* Scale of shunt voltage: LSB is 40uV (40000nV) */
		voltage_nv = regval * 40000;
		/* Return current in mA */
		*val = DIV_ROUND_CLOSEST(voltage_nv, resistance_uo);
		return 0;
	case hwmon_curr_crit_alarm:
	case hwmon_curr_max_alarm:
		/* No actual register read if channel is disabled */
		if (!ina3221_is_enabled(ina, channel)) {
			/* Return 0 for alert flags */
			*val = 0;
			return 0;
		}
		ret = regmap_field_read(ina->fields[reg], &regval);
		if (ret)
			return ret;
		*val = regval;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int ina3221_write_chip(struct device *dev, u32 attr, long val)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret, idx;
	u32 tmp;

	switch (attr) {
	case hwmon_chip_samples:
		idx = find_closest(val, ina3221_avg_samples,
				   ARRAY_SIZE(ina3221_avg_samples));

		tmp = (ina->reg_config & ~INA3221_CONFIG_AVG_MASK) |
		      (idx << INA3221_CONFIG_AVG_SHIFT);
		ret = regmap_write(ina->regmap, INA3221_CONFIG, tmp);
		if (ret)
			return ret;

		/* Update reg_config accordingly */
		ina->reg_config = tmp;
		return 0;
	case hwmon_chip_update_interval:
		tmp = ina3221_interval_ms_to_conv_time(ina->reg_config, val);
		idx = find_closest(tmp, ina3221_conv_time,
				   ARRAY_SIZE(ina3221_conv_time));

		/* Update Bus and Shunt voltage conversion times */
		tmp = INA3221_CONFIG_VBUS_CT_MASK | INA3221_CONFIG_VSH_CT_MASK;
		tmp = (ina->reg_config & ~tmp) |
		      (idx << INA3221_CONFIG_VBUS_CT_SHIFT) |
		      (idx << INA3221_CONFIG_VSH_CT_SHIFT);
		ret = regmap_write(ina->regmap, INA3221_CONFIG, tmp);
		if (ret)
			return ret;

		/* Update reg_config accordingly */
		ina->reg_config = tmp;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int ina3221_write_curr(struct device *dev, u32 attr,
			      int channel, long val)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	struct ina3221_input *input = ina->inputs;
	u8 reg = ina3221_curr_reg[attr][channel];
	int resistance_uo, current_ma, voltage_uv;
	int regval;

	if (channel > INA3221_CHANNEL3)
		resistance_uo = ina->summation_shunt_resistor;
	else
		resistance_uo = input[channel].shunt_resistor;

	if (!resistance_uo)
		return -EOPNOTSUPP;

	/* clamp current */
	current_ma = clamp_val(val,
			       INT_MIN / resistance_uo,
			       INT_MAX / resistance_uo);

	voltage_uv = DIV_ROUND_CLOSEST(current_ma * resistance_uo, 1000);

	/* clamp voltage */
	voltage_uv = clamp_val(voltage_uv, -163800, 163800);

	/*
	 * Formula to convert voltage_uv to register value:
	 *     regval = (voltage_uv / scale) << shift
	 * Note:
	 *     The scale is 40uV for all shunt voltage registers
	 *     Shunt Voltage Sum register left-shifts 1 bit
	 *     All other Shunt Voltage registers shift 3 bits
	 * Results:
	 *     SHUNT_SUM: (1 / 40uV) << 1 = 1 / 20uV
	 *     SHUNT[1-3]: (1 / 40uV) << 3 = 1 / 5uV
	 */
	if (reg == INA3221_SHUNT_SUM || reg == INA3221_CRIT_SUM)
		regval = DIV_ROUND_CLOSEST(voltage_uv, 20) & 0xfffe;
	else
		regval = DIV_ROUND_CLOSEST(voltage_uv, 5) & 0xfff8;

	return regmap_write(ina->regmap, reg, regval);
}

static int ina3221_write_enable(struct device *dev, int channel, bool enable)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	u16 config, mask = INA3221_CONFIG_CHx_EN(channel);
	u16 config_old = ina->reg_config & mask;
	u32 tmp;
	int ret;

	config = enable ? mask : 0;

	/* Bypass if enable status is not being changed */
	if (config_old == config)
		return 0;

	/* For enabling routine, increase refcount and resume() at first */
	if (enable) {
		ret = pm_runtime_resume_and_get(ina->pm_dev);
		if (ret < 0) {
			dev_err(dev, "Failed to get PM runtime\n");
			return ret;
		}
	}

	/* Enable or disable the channel */
	tmp = (ina->reg_config & ~mask) | (config & mask);
	ret = regmap_write(ina->regmap, INA3221_CONFIG, tmp);
	if (ret)
		goto fail;

	/* Cache the latest config register value */
	ina->reg_config = tmp;

	/* For disabling routine, decrease refcount or suspend() at last */
	if (!enable)
		pm_runtime_put_sync(ina->pm_dev);

	return 0;

fail:
	if (enable) {
		dev_err(dev, "Failed to enable channel %d: error %d\n",
			channel, ret);
		pm_runtime_put_sync(ina->pm_dev);
	}

	return ret;
}

static int ina3221_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&ina->lock);

	switch (type) {
	case hwmon_chip:
		ret = ina3221_read_chip(dev, attr, val);
		break;
	case hwmon_in:
		/* 0-align channel ID */
		ret = ina3221_read_in(dev, attr, channel - 1, val);
		break;
	case hwmon_curr:
		ret = ina3221_read_curr(dev, attr, channel, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&ina->lock);

	return ret;
}

static int ina3221_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret;

	mutex_lock(&ina->lock);

	switch (type) {
	case hwmon_chip:
		ret = ina3221_write_chip(dev, attr, val);
		break;
	case hwmon_in:
		/* 0-align channel ID */
		ret = ina3221_write_enable(dev, channel - 1, val);
		break;
	case hwmon_curr:
		ret = ina3221_write_curr(dev, attr, channel, val);
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&ina->lock);

	return ret;
}

static int ina3221_read_string(struct device *dev, enum hwmon_sensor_types type,
			       u32 attr, int channel, const char **str)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int index = channel - 1;

	if (channel == 7)
		*str = "sum of shunt voltages";
	else
		*str = ina->inputs[index].label;

	return 0;
}

static umode_t ina3221_is_visible(const void *drvdata,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct ina3221_data *ina = drvdata;
	const struct ina3221_input *input = NULL;

	switch (type) {
	case hwmon_chip:
		switch (attr) {
		case hwmon_chip_samples:
		case hwmon_chip_update_interval:
			return 0644;
		default:
			return 0;
		}
	case hwmon_in:
		/* Ignore in0_ */
		if (channel == 0)
			return 0;

		switch (attr) {
		case hwmon_in_label:
			if (channel - 1 <= INA3221_CHANNEL3)
				input = &ina->inputs[channel - 1];
			else if (channel == 7)
				return 0444;
			/* Hide label node if label is not provided */
			return (input && input->label) ? 0444 : 0;
		case hwmon_in_input:
			return 0444;
		case hwmon_in_enable:
			return 0644;
		default:
			return 0;
		}
	case hwmon_curr:
		switch (attr) {
		case hwmon_curr_input:
		case hwmon_curr_crit_alarm:
		case hwmon_curr_max_alarm:
			return 0444;
		case hwmon_curr_crit:
		case hwmon_curr_max:
			return 0644;
		default:
			return 0;
		}
	default:
		return 0;
	}
}

#define INA3221_HWMON_CURR_CONFIG (HWMON_C_INPUT | \
				   HWMON_C_CRIT | HWMON_C_CRIT_ALARM | \
				   HWMON_C_MAX | HWMON_C_MAX_ALARM)

static const struct hwmon_channel_info * const ina3221_info[] = {
	HWMON_CHANNEL_INFO(chip,
			   HWMON_C_SAMPLES,
			   HWMON_C_UPDATE_INTERVAL),
	HWMON_CHANNEL_INFO(in,
			   /* 0: dummy, skipped in is_visible */
			   HWMON_I_INPUT,
			   /* 1-3: input voltage Channels */
			   HWMON_I_INPUT | HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_ENABLE | HWMON_I_LABEL,
			   HWMON_I_INPUT | HWMON_I_ENABLE | HWMON_I_LABEL,
			   /* 4-6: shunt voltage Channels */
			   HWMON_I_INPUT,
			   HWMON_I_INPUT,
			   HWMON_I_INPUT,
			   /* 7: summation of shunt voltage channels */
			   HWMON_I_INPUT | HWMON_I_LABEL),
	HWMON_CHANNEL_INFO(curr,
			   /* 1-3: current channels*/
			   INA3221_HWMON_CURR_CONFIG,
			   INA3221_HWMON_CURR_CONFIG,
			   INA3221_HWMON_CURR_CONFIG,
			   /* 4: summation of current channels */
			   HWMON_C_INPUT | HWMON_C_CRIT | HWMON_C_CRIT_ALARM),
	NULL
};

static const struct hwmon_ops ina3221_hwmon_ops = {
	.is_visible = ina3221_is_visible,
	.read_string = ina3221_read_string,
	.read = ina3221_read,
	.write = ina3221_write,
};

static const struct hwmon_chip_info ina3221_chip_info = {
	.ops = &ina3221_hwmon_ops,
	.info = ina3221_info,
};

/* Extra attribute groups */
static ssize_t ina3221_shunt_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;
	struct ina3221_input *input = &ina->inputs[channel];

	return sysfs_emit(buf, "%d\n", input->shunt_resistor);
}

static ssize_t ina3221_shunt_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct sensor_device_attribute *sd_attr = to_sensor_dev_attr(attr);
	struct ina3221_data *ina = dev_get_drvdata(dev);
	unsigned int channel = sd_attr->index;
	struct ina3221_input *input = &ina->inputs[channel];
	int val;
	int ret;

	ret = kstrtoint(buf, 0, &val);
	if (ret)
		return ret;

	val = clamp_val(val, 1, INT_MAX);

	input->shunt_resistor = val;

	/* Update summation_shunt_resistor for summation channel */
	ina->summation_shunt_resistor = ina3221_summation_shunt_resistor(ina);

	return count;
}

/* shunt resistance */
static SENSOR_DEVICE_ATTR_RW(shunt1_resistor, ina3221_shunt, INA3221_CHANNEL1);
static SENSOR_DEVICE_ATTR_RW(shunt2_resistor, ina3221_shunt, INA3221_CHANNEL2);
static SENSOR_DEVICE_ATTR_RW(shunt3_resistor, ina3221_shunt, INA3221_CHANNEL3);

static struct attribute *ina3221_attrs[] = {
	&sensor_dev_attr_shunt1_resistor.dev_attr.attr,
	&sensor_dev_attr_shunt2_resistor.dev_attr.attr,
	&sensor_dev_attr_shunt3_resistor.dev_attr.attr,
	NULL,
};
ATTRIBUTE_GROUPS(ina3221);

static const struct regmap_range ina3221_yes_ranges[] = {
	regmap_reg_range(INA3221_CONFIG, INA3221_BUS3),
	regmap_reg_range(INA3221_SHUNT_SUM, INA3221_SHUNT_SUM),
	regmap_reg_range(INA3221_MASK_ENABLE, INA3221_MASK_ENABLE),
};

static const struct regmap_access_table ina3221_volatile_table = {
	.yes_ranges = ina3221_yes_ranges,
	.n_yes_ranges = ARRAY_SIZE(ina3221_yes_ranges),
};

static const struct regmap_config ina3221_regmap_config = {
	.reg_bits = 8,
	.val_bits = 16,

	.cache_type = REGCACHE_MAPLE,
	.volatile_table = &ina3221_volatile_table,
};

static int ina3221_probe_child_from_dt(struct device *dev,
				       struct device_node *child,
				       struct ina3221_data *ina)
{
	struct ina3221_input *input;
	u32 val;
	int ret;

	ret = of_property_read_u32(child, "reg", &val);
	if (ret) {
		dev_err(dev, "missing reg property of %pOFn\n", child);
		return ret;
	} else if (val > INA3221_CHANNEL3) {
		dev_err(dev, "invalid reg %d of %pOFn\n", val, child);
		return -EINVAL;
	}

	input = &ina->inputs[val];

	/* Log the disconnected channel input */
	if (!of_device_is_available(child)) {
		input->disconnected = true;
		return 0;
	}

	/* Save the connected input label if available */
	of_property_read_string(child, "label", &input->label);

	/* summation channel control */
	input->summation_disable = of_property_read_bool(child, "ti,summation-disable");

	/* Overwrite default shunt resistor value optionally */
	if (!of_property_read_u32(child, "shunt-resistor-micro-ohms", &val)) {
		if (val < 1 || val > INT_MAX) {
			dev_err(dev, "invalid shunt resistor value %u of %pOFn\n",
				val, child);
			return -EINVAL;
		}
		input->shunt_resistor = val;
	}

	return 0;
}

static int ina3221_probe_from_dt(struct device *dev, struct ina3221_data *ina)
{
	const struct device_node *np = dev->of_node;
	int ret;

	/* Compatible with non-DT platforms */
	if (!np)
		return 0;

	ina->single_shot = of_property_read_bool(np, "ti,single-shot");

	for_each_child_of_node_scoped(np, child) {
		ret = ina3221_probe_child_from_dt(dev, child, ina);
		if (ret)
			return ret;
	}

	return 0;
}

static int ina3221_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ina3221_data *ina;
	struct device *hwmon_dev;
	char name[32];
	int i, ret;

	ina = devm_kzalloc(dev, sizeof(*ina), GFP_KERNEL);
	if (!ina)
		return -ENOMEM;

	ina->regmap = devm_regmap_init_i2c(client, &ina3221_regmap_config);
	if (IS_ERR(ina->regmap)) {
		dev_err(dev, "Unable to allocate register map\n");
		return PTR_ERR(ina->regmap);
	}

	for (i = 0; i < F_MAX_FIELDS; i++) {
		ina->fields[i] = devm_regmap_field_alloc(dev,
							 ina->regmap,
							 ina3221_reg_fields[i]);
		if (IS_ERR(ina->fields[i])) {
			dev_err(dev, "Unable to allocate regmap fields\n");
			return PTR_ERR(ina->fields[i]);
		}
	}

	for (i = 0; i < INA3221_NUM_CHANNELS; i++)
		ina->inputs[i].shunt_resistor = INA3221_RSHUNT_DEFAULT;

	ret = ina3221_probe_from_dt(dev, ina);
	if (ret) {
		dev_err(dev, "Unable to probe from device tree\n");
		return ret;
	}

	/* The driver will be reset, so use reset value */
	ina->reg_config = INA3221_CONFIG_DEFAULT;

	/* Clear continuous bit to use single-shot mode */
	if (ina->single_shot)
		ina->reg_config &= ~INA3221_CONFIG_MODE_CONTINUOUS;

	/* Disable channels if their inputs are disconnected */
	for (i = 0; i < INA3221_NUM_CHANNELS; i++) {
		if (ina->inputs[i].disconnected)
			ina->reg_config &= ~INA3221_CONFIG_CHx_EN(i);
	}

	/* Initialize summation_shunt_resistor for summation channel control */
	ina->summation_shunt_resistor = ina3221_summation_shunt_resistor(ina);
	for (i = 0; i < INA3221_NUM_CHANNELS; i++) {
		if (!ina->inputs[i].summation_disable)
			ina->summation_channel_control |= BIT(14 - i);
	}

	ina->pm_dev = dev;
	mutex_init(&ina->lock);
	dev_set_drvdata(dev, ina);

	/* Enable PM runtime -- status is suspended by default */
	pm_runtime_enable(ina->pm_dev);

	/* Initialize (resume) the device */
	for (i = 0; i < INA3221_NUM_CHANNELS; i++) {
		if (ina->inputs[i].disconnected)
			continue;
		/* Match the refcount with number of enabled channels */
		ret = pm_runtime_get_sync(ina->pm_dev);
		if (ret < 0)
			goto fail;
	}

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name, ina,
							 &ina3221_chip_info,
							 ina3221_groups);
	if (IS_ERR(hwmon_dev)) {
		dev_err(dev, "Unable to register hwmon device\n");
		ret = PTR_ERR(hwmon_dev);
		goto fail;
	}

	scnprintf(name, sizeof(name), "%s-%s", INA3221_DRIVER_NAME, dev_name(dev));
	ina->debugfs = debugfs_create_dir(name, NULL);

	for (i = 0; i < INA3221_NUM_CHANNELS; i++) {
		scnprintf(name, sizeof(name), "in%d_summation_disable", i);
		debugfs_create_bool(name, 0400, ina->debugfs,
				    &ina->inputs[i].summation_disable);
	}

	return 0;

fail:
	pm_runtime_disable(ina->pm_dev);
	pm_runtime_set_suspended(ina->pm_dev);
	/* pm_runtime_put_noidle() will decrease the PM refcount until 0 */
	for (i = 0; i < INA3221_NUM_CHANNELS; i++)
		pm_runtime_put_noidle(ina->pm_dev);
	mutex_destroy(&ina->lock);

	return ret;
}

static void ina3221_remove(struct i2c_client *client)
{
	struct ina3221_data *ina = dev_get_drvdata(&client->dev);
	int i;

	debugfs_remove_recursive(ina->debugfs);

	pm_runtime_disable(ina->pm_dev);
	pm_runtime_set_suspended(ina->pm_dev);

	/* pm_runtime_put_noidle() will decrease the PM refcount until 0 */
	for (i = 0; i < INA3221_NUM_CHANNELS; i++)
		pm_runtime_put_noidle(ina->pm_dev);

	mutex_destroy(&ina->lock);
}

static int ina3221_suspend(struct device *dev)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret;

	/* Save config register value and enable cache-only */
	ret = regmap_read(ina->regmap, INA3221_CONFIG, &ina->reg_config);
	if (ret)
		return ret;

	/* Set to power-down mode for power saving */
	ret = regmap_update_bits(ina->regmap, INA3221_CONFIG,
				 INA3221_CONFIG_MODE_MASK,
				 INA3221_CONFIG_MODE_POWERDOWN);
	if (ret)
		return ret;

	regcache_cache_only(ina->regmap, true);
	regcache_mark_dirty(ina->regmap);

	return 0;
}

static int ina3221_resume(struct device *dev)
{
	struct ina3221_data *ina = dev_get_drvdata(dev);
	int ret;

	regcache_cache_only(ina->regmap, false);

	/* Software reset the chip */
	ret = regmap_field_write(ina->fields[F_RST], true);
	if (ret) {
		dev_err(dev, "Unable to reset device\n");
		return ret;
	}

	/* Restore cached register values to hardware */
	ret = regcache_sync(ina->regmap);
	if (ret)
		return ret;

	/* Restore config register value to hardware */
	ret = regmap_write(ina->regmap, INA3221_CONFIG, ina->reg_config);
	if (ret)
		return ret;

	/* Initialize summation channel control */
	if (ina->summation_shunt_resistor) {
		/*
		 * Sum only channels that are not disabled for summation.
		 * Shunt measurements of disconnected channels should
		 * be 0, so it does not matter for summation.
		 */
		ret = regmap_update_bits(ina->regmap, INA3221_MASK_ENABLE,
					 INA3221_MASK_ENABLE_SCC_MASK,
					 ina->summation_channel_control);
		if (ret) {
			dev_err(dev, "Unable to control summation channel\n");
			return ret;
		}
	}

	return 0;
}

static DEFINE_RUNTIME_DEV_PM_OPS(ina3221_pm, ina3221_suspend, ina3221_resume,
				 NULL);

static const struct of_device_id ina3221_of_match_table[] = {
	{ .compatible = "ti,ina3221", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ina3221_of_match_table);

static const struct i2c_device_id ina3221_ids[] = {
	{ "ina3221" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(i2c, ina3221_ids);

static struct i2c_driver ina3221_i2c_driver = {
	.probe = ina3221_probe,
	.remove = ina3221_remove,
	.driver = {
		.name = INA3221_DRIVER_NAME,
		.of_match_table = ina3221_of_match_table,
		.pm = pm_ptr(&ina3221_pm),
	},
	.id_table = ina3221_ids,
};
module_i2c_driver(ina3221_i2c_driver);

MODULE_AUTHOR("Andrew F. Davis <afd@ti.com>");
MODULE_DESCRIPTION("Texas Instruments INA3221 HWMon Driver");
MODULE_LICENSE("GPL v2");
