// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max6639.c - Support for Maxim MAX6639
 *
 * 2-Channel Temperature Monitor with Dual PWM Fan-Speed Controller
 *
 * Copyright (C) 2010, 2011 Roland Stigge <stigge@antcom.de>
 *
 * based on the initial MAX6639 support from semptian.net
 * by He Changqing <hechangqing@semptian.com>
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/util_macros.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2c, 0x2e, 0x2f, I2C_CLIENT_END };

/* The MAX6639 registers, valid channel numbers: 0, 1 */
#define MAX6639_REG_TEMP(ch)			(0x00 + (ch))
#define MAX6639_REG_STATUS			0x02
#define MAX6639_REG_OUTPUT_MASK			0x03
#define MAX6639_REG_GCONFIG			0x04
#define MAX6639_REG_TEMP_EXT(ch)		(0x05 + (ch))
#define MAX6639_REG_ALERT_LIMIT(ch)		(0x08 + (ch))
#define MAX6639_REG_OT_LIMIT(ch)		(0x0A + (ch))
#define MAX6639_REG_THERM_LIMIT(ch)		(0x0C + (ch))
#define MAX6639_REG_FAN_CONFIG1(ch)		(0x10 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG2a(ch)		(0x11 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG2b(ch)		(0x12 + (ch) * 4)
#define MAX6639_REG_FAN_CONFIG3(ch)		(0x13 + (ch) * 4)
#define MAX6639_REG_FAN_CNT(ch)			(0x20 + (ch))
#define MAX6639_REG_TARGET_CNT(ch)		(0x22 + (ch))
#define MAX6639_REG_FAN_PPR(ch)			(0x24 + (ch))
#define MAX6639_REG_TARGTDUTY(ch)		(0x26 + (ch))
#define MAX6639_REG_FAN_START_TEMP(ch)		(0x28 + (ch))
#define MAX6639_REG_DEVID			0x3D
#define MAX6639_REG_MANUID			0x3E
#define MAX6639_REG_DEVREV			0x3F

/* Register bits */
#define MAX6639_GCONFIG_STANDBY			0x80
#define MAX6639_GCONFIG_POR			0x40
#define MAX6639_GCONFIG_DISABLE_TIMEOUT		0x20
#define MAX6639_GCONFIG_CH2_LOCAL		0x10
#define MAX6639_GCONFIG_PWM_FREQ_HI		0x08

#define MAX6639_FAN_CONFIG1_PWM			0x80
#define MAX6639_FAN_CONFIG3_FREQ_MASK		0x03
#define MAX6639_FAN_CONFIG3_THERM_FULL_SPEED	0x40

#define MAX6639_NUM_CHANNELS			2

static const int rpm_ranges[] = { 2000, 4000, 8000, 16000 };

/* Supported PWM frequency */
static const unsigned int freq_table[] = { 20, 33, 50, 100, 5000, 8333, 12500,
					   25000 };

#define FAN_FROM_REG(val, rpm_range)	((val) == 0 || (val) == 255 ? \
				0 : (rpm_ranges[rpm_range] * 30) / (val))
#define TEMP_LIMIT_TO_REG(val)	clamp_val((val) / 1000, 0, 255)

/*
 * Client data (each client gets its own)
 */
struct max6639_data {
	struct regmap *regmap;
	struct mutex update_lock;

	/* Register values initialized only once */
	u8 ppr[MAX6639_NUM_CHANNELS];	/* Pulses per rotation 0..3 for 1..4 ppr */
	u8 rpm_range[MAX6639_NUM_CHANNELS]; /* Index in above rpm_ranges table */
	u32 target_rpm[MAX6639_NUM_CHANNELS];

	/* Optional regulator for FAN supply */
	struct regulator *reg;
};

static int max6639_temp_read_input(struct device *dev, int channel, long *temp)
{
	u32 regs[2] = { MAX6639_REG_TEMP_EXT(channel), MAX6639_REG_TEMP(channel) };
	struct max6639_data *data = dev_get_drvdata(dev);
	u8 regvals[2];
	int res;

	res = regmap_multi_reg_read(data->regmap, regs, regvals, 2);
	if (res < 0)
		return res;

	*temp = ((regvals[0] >> 5) | (regvals[1] << 3)) * 125;

	return 0;
}

static int max6639_temp_read_fault(struct device *dev, int channel, long *fault)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_TEMP_EXT(channel), &val);
	if (res < 0)
		return res;

	*fault = val & 1;

	return 0;
}

static int max6639_temp_read_max(struct device *dev, int channel, long *max)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_THERM_LIMIT(channel), &val);
	if (res < 0)
		return res;

	*max = (long)val * 1000;

	return 0;
}

static int max6639_temp_read_crit(struct device *dev, int channel, long *crit)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_ALERT_LIMIT(channel), &val);
	if (res < 0)
		return res;

	*crit = (long)val * 1000;

	return 0;
}

static int max6639_temp_read_emergency(struct device *dev, int channel, long *emerg)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_OT_LIMIT(channel), &val);
	if (res < 0)
		return res;

	*emerg = (long)val * 1000;

	return 0;
}

static int max6639_get_status(struct device *dev, unsigned int *status)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	res = regmap_read(data->regmap, MAX6639_REG_STATUS, &val);
	if (res < 0)
		return res;

	*status = val;

	return 0;
}

static int max6639_temp_set_max(struct max6639_data *data, int channel, long val)
{
	int res;

	res = regmap_write(data->regmap, MAX6639_REG_THERM_LIMIT(channel),
			   TEMP_LIMIT_TO_REG(val));
	return res;
}

static int max6639_temp_set_crit(struct max6639_data *data, int channel, long val)
{
	int res;

	res = regmap_write(data->regmap, MAX6639_REG_ALERT_LIMIT(channel), TEMP_LIMIT_TO_REG(val));

	return res;
}

static int max6639_temp_set_emergency(struct max6639_data *data, int channel, long val)
{
	int res;

	res = regmap_write(data->regmap, MAX6639_REG_OT_LIMIT(channel), TEMP_LIMIT_TO_REG(val));

	return res;
}

static int max6639_read_fan(struct device *dev, u32 attr, int channel,
			    long *fan_val)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	int res;

	switch (attr) {
	case hwmon_fan_input:
		res = regmap_read(data->regmap, MAX6639_REG_FAN_CNT(channel), &val);
		if (res < 0)
			return res;
		*fan_val = FAN_FROM_REG(val, data->rpm_range[channel]);
		return 0;
	case hwmon_fan_fault:
		res = max6639_get_status(dev, &val);
		if (res < 0)
			return res;
		*fan_val = !!(val & BIT(1 - channel));
		return 0;
	case hwmon_fan_pulses:
		*fan_val = data->ppr[channel];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max6639_set_ppr(struct max6639_data *data, int channel, u8 ppr)
{
	/* Decrement the PPR value and shift left by 6 to match the register format */
	return regmap_write(data->regmap, MAX6639_REG_FAN_PPR(channel), ppr-- << 6);
}

static int max6639_write_fan(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	int err;

	switch (attr) {
	case hwmon_fan_pulses:
		if (val <= 0 || val > 4)
			return -EINVAL;

		mutex_lock(&data->update_lock);
		/* Set Fan pulse per revolution */
		err = max6639_set_ppr(data, channel, val);
		if (err < 0) {
			mutex_unlock(&data->update_lock);
			return err;
		}
		data->ppr[channel] = val;

		mutex_unlock(&data->update_lock);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max6639_fan_is_visible(const void *_data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_fault:
		return 0444;
	case hwmon_fan_pulses:
		return 0644;
	default:
		return 0;
	}
}

static int max6639_read_pwm(struct device *dev, u32 attr, int channel,
			    long *pwm_val)
{
	u32 regs[2] = { MAX6639_REG_FAN_CONFIG3(channel), MAX6639_REG_GCONFIG };
	struct max6639_data *data = dev_get_drvdata(dev);
	unsigned int val;
	u8 regvals[2];
	int res;
	u8 i;

	switch (attr) {
	case hwmon_pwm_input:
		res = regmap_read(data->regmap, MAX6639_REG_TARGTDUTY(channel), &val);
		if (res < 0)
			return res;
		*pwm_val = val * 255 / 120;
		return 0;
	case hwmon_pwm_freq:
		res = regmap_multi_reg_read(data->regmap, regs, regvals, 2);
		if (res < 0)
			return res;
		i = regvals[0] & MAX6639_FAN_CONFIG3_FREQ_MASK;
		if (regvals[1] & MAX6639_GCONFIG_PWM_FREQ_HI)
			i |= 0x4;
		*pwm_val = freq_table[i];
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max6639_write_pwm(struct device *dev, u32 attr, int channel,
			     long val)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	int err;
	u8 i;

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > 255)
			return -EINVAL;
		err = regmap_write(data->regmap, MAX6639_REG_TARGTDUTY(channel),
				   val * 120 / 255);
		return err;
	case hwmon_pwm_freq:
		val = clamp_val(val, 0, 25000);

		i = find_closest(val, freq_table, ARRAY_SIZE(freq_table));

		mutex_lock(&data->update_lock);
		err = regmap_update_bits(data->regmap, MAX6639_REG_FAN_CONFIG3(channel),
					 MAX6639_FAN_CONFIG3_FREQ_MASK, i);
		if (err < 0) {
			mutex_unlock(&data->update_lock);
			return err;
		}

		if (i >> 2)
			err = regmap_set_bits(data->regmap, MAX6639_REG_GCONFIG,
					      MAX6639_GCONFIG_PWM_FREQ_HI);
		else
			err = regmap_clear_bits(data->regmap, MAX6639_REG_GCONFIG,
						MAX6639_GCONFIG_PWM_FREQ_HI);

		mutex_unlock(&data->update_lock);
		return err;
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max6639_pwm_is_visible(const void *_data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_pwm_input:
	case hwmon_pwm_freq:
		return 0644;
	default:
		return 0;
	}
}

static int max6639_read_temp(struct device *dev, u32 attr, int channel,
			     long *val)
{
	unsigned int status;
	int res;

	switch (attr) {
	case hwmon_temp_input:
		res = max6639_temp_read_input(dev, channel, val);
		return res;
	case hwmon_temp_fault:
		res = max6639_temp_read_fault(dev, channel, val);
		return res;
	case hwmon_temp_max:
		res = max6639_temp_read_max(dev, channel, val);
		return res;
	case hwmon_temp_crit:
		res = max6639_temp_read_crit(dev, channel, val);
		return res;
	case hwmon_temp_emergency:
		res = max6639_temp_read_emergency(dev, channel, val);
		return res;
	case hwmon_temp_max_alarm:
		res = max6639_get_status(dev, &status);
		if (res < 0)
			return res;
		*val = !!(status & BIT(3 - channel));
		return 0;
	case hwmon_temp_crit_alarm:
		res = max6639_get_status(dev, &status);
		if (res < 0)
			return res;
		*val = !!(status & BIT(7 - channel));
		return 0;
	case hwmon_temp_emergency_alarm:
		res = max6639_get_status(dev, &status);
		if (res < 0)
			return res;
		*val = !!(status & BIT(5 - channel));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max6639_write_temp(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct max6639_data *data = dev_get_drvdata(dev);

	switch (attr) {
	case hwmon_temp_max:
		return max6639_temp_set_max(data, channel, val);
	case hwmon_temp_crit:
		return max6639_temp_set_crit(data, channel, val);
	case hwmon_temp_emergency:
		return max6639_temp_set_emergency(data, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max6639_temp_is_visible(const void *_data, u32 attr, int channel)
{
	switch (attr) {
	case hwmon_temp_input:
	case hwmon_temp_fault:
	case hwmon_temp_max_alarm:
	case hwmon_temp_crit_alarm:
	case hwmon_temp_emergency_alarm:
		return 0444;
	case hwmon_temp_max:
	case hwmon_temp_crit:
	case hwmon_temp_emergency:
		return 0644;
	default:
		return 0;
	}
}

static int max6639_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return max6639_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return max6639_read_pwm(dev, attr, channel, val);
	case hwmon_temp:
		return max6639_read_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int max6639_write(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_fan:
		return max6639_write_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return max6639_write_pwm(dev, attr, channel, val);
	case hwmon_temp:
		return max6639_write_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max6639_is_visible(const void *data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return max6639_fan_is_visible(data, attr, channel);
	case hwmon_pwm:
		return max6639_pwm_is_visible(data, attr, channel);
	case hwmon_temp:
		return max6639_temp_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const struct hwmon_channel_info * const max6639_info[] = {
	HWMON_CHANNEL_INFO(fan,
			   HWMON_F_INPUT | HWMON_F_FAULT | HWMON_F_PULSES,
			   HWMON_F_INPUT | HWMON_F_FAULT | HWMON_F_PULSES),
	HWMON_CHANNEL_INFO(pwm,
			   HWMON_PWM_INPUT | HWMON_PWM_FREQ,
			   HWMON_PWM_INPUT | HWMON_PWM_FREQ),
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_EMERGENCY |
			   HWMON_T_EMERGENCY_ALARM,
			   HWMON_T_INPUT | HWMON_T_FAULT | HWMON_T_MAX | HWMON_T_MAX_ALARM |
			   HWMON_T_CRIT | HWMON_T_CRIT_ALARM | HWMON_T_EMERGENCY |
			   HWMON_T_EMERGENCY_ALARM),
	NULL
};

static const struct hwmon_ops max6639_hwmon_ops = {
	.is_visible = max6639_is_visible,
	.read = max6639_read,
	.write = max6639_write,
};

static const struct hwmon_chip_info max6639_chip_info = {
	.ops = &max6639_hwmon_ops,
	.info = max6639_info,
};

/*
 *  returns respective index in rpm_ranges table
 *  1 by default on invalid range
 */
static int rpm_range_to_reg(int range)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(rpm_ranges); i++) {
		if (rpm_ranges[i] == range)
			return i;
	}

	return 1; /* default: 4000 RPM */
}

static int max6639_probe_child_from_dt(struct i2c_client *client,
				       struct device_node *child,
				       struct max6639_data *data)

{
	struct device *dev = &client->dev;
	u32 i;
	int err, val;

	err = of_property_read_u32(child, "reg", &i);
	if (err) {
		dev_err(dev, "missing reg property of %pOFn\n", child);
		return err;
	}

	if (i > 1) {
		dev_err(dev, "Invalid fan index reg %d\n", i);
		return -EINVAL;
	}

	err = of_property_read_u32(child, "pulses-per-revolution", &val);
	if (!err) {
		if (val < 1 || val > 5) {
			dev_err(dev, "invalid pulses-per-revolution %d of %pOFn\n", val, child);
			return -EINVAL;
		}
		data->ppr[i] = val;
	}

	err = of_property_read_u32(child, "max-rpm", &val);
	if (!err)
		data->rpm_range[i] = rpm_range_to_reg(val);

	err = of_property_read_u32(child, "target-rpm", &val);
	if (!err)
		data->target_rpm[i] = val;

	return 0;
}

static int max6639_init_client(struct i2c_client *client,
			       struct max6639_data *data)
{
	struct device *dev = &client->dev;
	const struct device_node *np = dev->of_node;
	struct device_node *child;
	int i, err;
	u8 target_duty;

	/* Reset chip to default values, see below for GCONFIG setup */
	err = regmap_write(data->regmap, MAX6639_REG_GCONFIG, MAX6639_GCONFIG_POR);
	if (err)
		return err;

	/* Fans pulse per revolution is 2 by default */
	data->ppr[0] = 2;
	data->ppr[1] = 2;

	/* default: 4000 RPM */
	data->rpm_range[0] = 1;
	data->rpm_range[1] = 1;
	data->target_rpm[0] = 4000;
	data->target_rpm[1] = 4000;

	for_each_child_of_node(np, child) {
		if (strcmp(child->name, "fan"))
			continue;

		err = max6639_probe_child_from_dt(client, child, data);
		if (err) {
			of_node_put(child);
			return err;
		}
	}

	for (i = 0; i < MAX6639_NUM_CHANNELS; i++) {
		err = regmap_set_bits(data->regmap, MAX6639_REG_OUTPUT_MASK, BIT(1 - i));
		if (err)
			return err;

		/* Set Fan pulse per revolution */
		err = max6639_set_ppr(data, i, data->ppr[i]);
		if (err)
			return err;

		/* Fans config PWM, RPM */
		err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG1(i),
				   MAX6639_FAN_CONFIG1_PWM | data->rpm_range[i]);
		if (err)
			return err;

		/* Fans PWM polarity high by default */
		err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG2a(i), 0x00);
		if (err)
			return err;

		/*
		 * /THERM full speed enable,
		 * PWM frequency 25kHz, see also GCONFIG below
		 */
		err = regmap_write(data->regmap, MAX6639_REG_FAN_CONFIG3(i),
				   MAX6639_FAN_CONFIG3_THERM_FULL_SPEED | 0x03);
		if (err)
			return err;

		/* Max. temp. 80C/90C/100C */
		err = regmap_write(data->regmap, MAX6639_REG_THERM_LIMIT(i), 80);
		if (err)
			return err;
		err = regmap_write(data->regmap, MAX6639_REG_ALERT_LIMIT(i), 90);
		if (err)
			return err;
		err = regmap_write(data->regmap, MAX6639_REG_OT_LIMIT(i), 100);
		if (err)
			return err;

		/* Set PWM based on target RPM if specified */
		if (data->target_rpm[i] >  rpm_ranges[data->rpm_range[i]])
			data->target_rpm[i] = rpm_ranges[data->rpm_range[i]];

		target_duty = 120 * data->target_rpm[i] / rpm_ranges[data->rpm_range[i]];
		err = regmap_write(data->regmap, MAX6639_REG_TARGTDUTY(i), target_duty);
		if (err)
			return err;
	}
	/* Start monitoring */
	return regmap_write(data->regmap, MAX6639_REG_GCONFIG,
			    MAX6639_GCONFIG_DISABLE_TIMEOUT | MAX6639_GCONFIG_CH2_LOCAL |
			    MAX6639_GCONFIG_PWM_FREQ_HI);

}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int max6639_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	int dev_id, manu_id;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	/* Actual detection via device and manufacturer ID */
	dev_id = i2c_smbus_read_byte_data(client, MAX6639_REG_DEVID);
	manu_id = i2c_smbus_read_byte_data(client, MAX6639_REG_MANUID);
	if (dev_id != 0x58 || manu_id != 0x4D)
		return -ENODEV;

	strscpy(info->type, "max6639", I2C_NAME_SIZE);

	return 0;
}

static void max6639_regulator_disable(void *data)
{
	regulator_disable(data);
}

static bool max6639_regmap_is_volatile(struct device *dev, unsigned int reg)
{
	switch (reg) {
	case MAX6639_REG_TEMP(0):
	case MAX6639_REG_TEMP_EXT(0):
	case MAX6639_REG_TEMP(1):
	case MAX6639_REG_TEMP_EXT(1):
	case MAX6639_REG_STATUS:
	case MAX6639_REG_FAN_CNT(0):
	case MAX6639_REG_FAN_CNT(1):
	case MAX6639_REG_TARGTDUTY(0):
	case MAX6639_REG_TARGTDUTY(1):
		return true;
	default:
		return false;
	}
}

static const struct regmap_config max6639_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.max_register = MAX6639_REG_DEVREV,
	.cache_type = REGCACHE_MAPLE,
	.volatile_reg = max6639_regmap_is_volatile,
};

static int max6639_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct max6639_data *data;
	struct device *hwmon_dev;
	int err;

	data = devm_kzalloc(dev, sizeof(struct max6639_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->regmap = devm_regmap_init_i2c(client, &max6639_regmap_config);
	if (IS_ERR(data->regmap))
		return dev_err_probe(dev,
				     PTR_ERR(data->regmap),
				     "regmap initialization failed\n");

	data->reg = devm_regulator_get_optional(dev, "fan");
	if (IS_ERR(data->reg)) {
		if (PTR_ERR(data->reg) != -ENODEV)
			return PTR_ERR(data->reg);

		data->reg = NULL;
	} else {
		/* Spin up fans */
		err = regulator_enable(data->reg);
		if (err) {
			dev_err(dev, "Failed to enable fan supply: %d\n", err);
			return err;
		}
		err = devm_add_action_or_reset(dev, max6639_regulator_disable,
					       data->reg);
		if (err) {
			dev_err(dev, "Failed to register action: %d\n", err);
			return err;
		}
	}

	mutex_init(&data->update_lock);

	/* Initialize the max6639 chip */
	err = max6639_init_client(client, data);
	if (err < 0)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data, &max6639_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static int max6639_suspend(struct device *dev)
{
	struct max6639_data *data = dev_get_drvdata(dev);

	if (data->reg)
		regulator_disable(data->reg);

	return regmap_write_bits(data->regmap, MAX6639_REG_GCONFIG, MAX6639_GCONFIG_STANDBY,
				 MAX6639_GCONFIG_STANDBY);
}

static int max6639_resume(struct device *dev)
{
	struct max6639_data *data = dev_get_drvdata(dev);
	int ret;

	if (data->reg) {
		ret = regulator_enable(data->reg);
		if (ret) {
			dev_err(dev, "Failed to enable fan supply: %d\n", ret);
			return ret;
		}
	}

	return regmap_write_bits(data->regmap, MAX6639_REG_GCONFIG, MAX6639_GCONFIG_STANDBY,
				 ~MAX6639_GCONFIG_STANDBY);
}

static const struct i2c_device_id max6639_id[] = {
	{"max6639"},
	{ }
};

MODULE_DEVICE_TABLE(i2c, max6639_id);

static DEFINE_SIMPLE_DEV_PM_OPS(max6639_pm_ops, max6639_suspend, max6639_resume);

static const struct of_device_id max6639_of_match[] = {
	{ .compatible = "maxim,max6639", },
	{ },
};

static struct i2c_driver max6639_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		   .name = "max6639",
		   .pm = pm_sleep_ptr(&max6639_pm_ops),
		   .of_match_table = max6639_of_match,
		   },
	.probe = max6639_probe,
	.id_table = max6639_id,
	.detect = max6639_detect,
	.address_list = normal_i2c,
};

module_i2c_driver(max6639_driver);

MODULE_AUTHOR("Roland Stigge <stigge@antcom.de>");
MODULE_DESCRIPTION("max6639 driver");
MODULE_LICENSE("GPL");
