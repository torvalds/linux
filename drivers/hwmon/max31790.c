/*
 * max31790.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring.
 *
 * (C) 2015 by Il Han <corone.il.han@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/jiffies.h>
#include <linux/module.h>
#include <linux/slab.h>

/* MAX31790 registers */
#define MAX31790_REG_GLOBAL_CONFIG	0x00
#define MAX31790_REG_FAN_CONFIG(ch)	(0x02 + (ch))
#define MAX31790_REG_FAN_DYNAMICS(ch)	(0x08 + (ch))
#define MAX31790_REG_FAN_FAULT_STATUS2	0x10
#define MAX31790_REG_FAN_FAULT_STATUS1	0x11
#define MAX31790_REG_TACH_COUNT(ch)	(0x18 + (ch) * 2)
#define MAX31790_REG_PWM_DUTY_CYCLE(ch)	(0x30 + (ch) * 2)
#define MAX31790_REG_PWMOUT(ch)		(0x40 + (ch) * 2)
#define MAX31790_REG_TARGET_COUNT(ch)	(0x50 + (ch) * 2)

/* Fan Config register bits */
#define MAX31790_FAN_CFG_RPM_MODE	0x80
#define MAX31790_FAN_CFG_TACH_INPUT_EN	0x08
#define MAX31790_FAN_CFG_TACH_INPUT	0x01

/* Fan Dynamics register bits */
#define MAX31790_FAN_DYN_SR_SHIFT	5
#define MAX31790_FAN_DYN_SR_MASK	0xE0
#define SR_FROM_REG(reg)		(((reg) & MAX31790_FAN_DYN_SR_MASK) \
					 >> MAX31790_FAN_DYN_SR_SHIFT)

#define FAN_RPM_MIN			120
#define FAN_RPM_MAX			7864320

#define RPM_FROM_REG(reg, sr)		(((reg) >> 4) ? \
					 ((60 * (sr) * 8192) / ((reg) >> 4)) : \
					 FAN_RPM_MAX)
#define RPM_TO_REG(rpm, sr)		((60 * (sr) * 8192) / ((rpm) * 2))

#define NR_CHANNEL			6

/*
 * Client data (each client gets its own)
 */
struct max31790_data {
	struct i2c_client *client;
	struct mutex update_lock;
	bool valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* register values */
	u8 fan_config[NR_CHANNEL];
	u8 fan_dynamics[NR_CHANNEL];
	u16 fault_status;
	u16 tach[NR_CHANNEL * 2];
	u16 pwm[NR_CHANNEL];
	u16 target_count[NR_CHANNEL];
};

static struct max31790_data *max31790_update_device(struct device *dev)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	struct max31790_data *ret = data;
	int i;
	int rv;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		rv = i2c_smbus_read_byte_data(client,
				MAX31790_REG_FAN_FAULT_STATUS1);
		if (rv < 0)
			goto abort;
		data->fault_status = rv & 0x3F;

		rv = i2c_smbus_read_byte_data(client,
				MAX31790_REG_FAN_FAULT_STATUS2);
		if (rv < 0)
			goto abort;
		data->fault_status |= (rv & 0x3F) << 6;

		for (i = 0; i < NR_CHANNEL; i++) {
			rv = i2c_smbus_read_word_swapped(client,
					MAX31790_REG_TACH_COUNT(i));
			if (rv < 0)
				goto abort;
			data->tach[i] = rv;

			if (data->fan_config[i]
			    & MAX31790_FAN_CFG_TACH_INPUT) {
				rv = i2c_smbus_read_word_swapped(client,
					MAX31790_REG_TACH_COUNT(NR_CHANNEL
								+ i));
				if (rv < 0)
					goto abort;
				data->tach[NR_CHANNEL + i] = rv;
			} else {
				rv = i2c_smbus_read_word_swapped(client,
						MAX31790_REG_PWMOUT(i));
				if (rv < 0)
					goto abort;
				data->pwm[i] = rv;

				rv = i2c_smbus_read_word_swapped(client,
						MAX31790_REG_TARGET_COUNT(i));
				if (rv < 0)
					goto abort;
				data->target_count[i] = rv;
			}
		}

		data->last_updated = jiffies;
		data->valid = true;
	}
	goto done;

abort:
	data->valid = false;
	ret = ERR_PTR(rv);

done:
	mutex_unlock(&data->update_lock);

	return ret;
}

static const u8 tach_period[8] = { 1, 2, 4, 8, 16, 32, 32, 32 };

static u8 get_tach_period(u8 fan_dynamics)
{
	return tach_period[SR_FROM_REG(fan_dynamics)];
}

static u8 bits_for_tach_period(int rpm)
{
	u8 bits;

	if (rpm < 500)
		bits = 0x0;
	else if (rpm < 1000)
		bits = 0x1;
	else if (rpm < 2000)
		bits = 0x2;
	else if (rpm < 4000)
		bits = 0x3;
	else if (rpm < 8000)
		bits = 0x4;
	else
		bits = 0x5;

	return bits;
}

static int max31790_read_fan(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct max31790_data *data = max31790_update_device(dev);
	int sr, rpm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	switch (attr) {
	case hwmon_fan_input:
		sr = get_tach_period(data->fan_dynamics[channel]);
		rpm = RPM_FROM_REG(data->tach[channel], sr);
		*val = rpm;
		return 0;
	case hwmon_fan_target:
		sr = get_tach_period(data->fan_dynamics[channel]);
		rpm = RPM_FROM_REG(data->target_count[channel], sr);
		*val = rpm;
		return 0;
	case hwmon_fan_fault:
		*val = !!(data->fault_status & (1 << channel));
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max31790_write_fan(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int target_count;
	int err = 0;
	u8 bits;
	int sr;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_fan_target:
		val = clamp_val(val, FAN_RPM_MIN, FAN_RPM_MAX);
		bits = bits_for_tach_period(val);
		data->fan_dynamics[channel] =
			((data->fan_dynamics[channel] &
			  ~MAX31790_FAN_DYN_SR_MASK) |
			 (bits << MAX31790_FAN_DYN_SR_SHIFT));
		err = i2c_smbus_write_byte_data(client,
					MAX31790_REG_FAN_DYNAMICS(channel),
					data->fan_dynamics[channel]);
		if (err < 0)
			break;

		sr = get_tach_period(data->fan_dynamics[channel]);
		target_count = RPM_TO_REG(val, sr);
		target_count = clamp_val(target_count, 0x1, 0x7FF);

		data->target_count[channel] = target_count << 5;

		err = i2c_smbus_write_word_swapped(client,
					MAX31790_REG_TARGET_COUNT(channel),
					data->target_count[channel]);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&data->update_lock);

	return err;
}

static umode_t max31790_fan_is_visible(const void *_data, u32 attr, int channel)
{
	const struct max31790_data *data = _data;
	u8 fan_config = data->fan_config[channel % NR_CHANNEL];

	switch (attr) {
	case hwmon_fan_input:
	case hwmon_fan_fault:
		if (channel < NR_CHANNEL ||
		    (fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return S_IRUGO;
		return 0;
	case hwmon_fan_target:
		if (channel < NR_CHANNEL &&
		    !(fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return S_IRUGO | S_IWUSR;
		return 0;
	default:
		return 0;
	}
}

static int max31790_read_pwm(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct max31790_data *data = max31790_update_device(dev);
	u8 fan_config;

	if (IS_ERR(data))
		return PTR_ERR(data);

	fan_config = data->fan_config[channel];

	switch (attr) {
	case hwmon_pwm_input:
		*val = data->pwm[channel] >> 8;
		return 0;
	case hwmon_pwm_enable:
		if (fan_config & MAX31790_FAN_CFG_RPM_MODE)
			*val = 2;
		else if (fan_config & MAX31790_FAN_CFG_TACH_INPUT_EN)
			*val = 1;
		else
			*val = 0;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int max31790_write_pwm(struct device *dev, u32 attr, int channel,
			      long val)
{
	struct max31790_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 fan_config;
	int err = 0;

	mutex_lock(&data->update_lock);

	switch (attr) {
	case hwmon_pwm_input:
		if (val < 0 || val > 255) {
			err = -EINVAL;
			break;
		}
		data->pwm[channel] = val << 8;
		err = i2c_smbus_write_word_swapped(client,
						   MAX31790_REG_PWMOUT(channel),
						   data->pwm[channel]);
		break;
	case hwmon_pwm_enable:
		fan_config = data->fan_config[channel];
		if (val == 0) {
			fan_config &= ~(MAX31790_FAN_CFG_TACH_INPUT_EN |
					MAX31790_FAN_CFG_RPM_MODE);
		} else if (val == 1) {
			fan_config = (fan_config |
				      MAX31790_FAN_CFG_TACH_INPUT_EN) &
				     ~MAX31790_FAN_CFG_RPM_MODE;
		} else if (val == 2) {
			fan_config |= MAX31790_FAN_CFG_TACH_INPUT_EN |
				      MAX31790_FAN_CFG_RPM_MODE;
		} else {
			err = -EINVAL;
			break;
		}
		data->fan_config[channel] = fan_config;
		err = i2c_smbus_write_byte_data(client,
					MAX31790_REG_FAN_CONFIG(channel),
					fan_config);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	mutex_unlock(&data->update_lock);

	return err;
}

static umode_t max31790_pwm_is_visible(const void *_data, u32 attr, int channel)
{
	const struct max31790_data *data = _data;
	u8 fan_config = data->fan_config[channel];

	switch (attr) {
	case hwmon_pwm_input:
	case hwmon_pwm_enable:
		if (!(fan_config & MAX31790_FAN_CFG_TACH_INPUT))
			return S_IRUGO | S_IWUSR;
		return 0;
	default:
		return 0;
	}
}

static int max31790_read(struct device *dev, enum hwmon_sensor_types type,
			 u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_fan:
		return max31790_read_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return max31790_read_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static int max31790_write(struct device *dev, enum hwmon_sensor_types type,
			  u32 attr, int channel, long val)
{
	switch (type) {
	case hwmon_fan:
		return max31790_write_fan(dev, attr, channel, val);
	case hwmon_pwm:
		return max31790_write_pwm(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t max31790_is_visible(const void *data,
				   enum hwmon_sensor_types type,
				   u32 attr, int channel)
{
	switch (type) {
	case hwmon_fan:
		return max31790_fan_is_visible(data, attr, channel);
	case hwmon_pwm:
		return max31790_pwm_is_visible(data, attr, channel);
	default:
		return 0;
	}
}

static const u32 max31790_fan_config[] = {
	HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_TARGET | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_FAULT,
	HWMON_F_INPUT | HWMON_F_FAULT,
	0
};

static const struct hwmon_channel_info max31790_fan = {
	.type = hwmon_fan,
	.config = max31790_fan_config,
};

static const u32 max31790_pwm_config[] = {
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	HWMON_PWM_INPUT | HWMON_PWM_ENABLE,
	0
};

static const struct hwmon_channel_info max31790_pwm = {
	.type = hwmon_pwm,
	.config = max31790_pwm_config,
};

static const struct hwmon_channel_info *max31790_info[] = {
	&max31790_fan,
	&max31790_pwm,
	NULL
};

static const struct hwmon_ops max31790_hwmon_ops = {
	.is_visible = max31790_is_visible,
	.read = max31790_read,
	.write = max31790_write,
};

static const struct hwmon_chip_info max31790_chip_info = {
	.ops = &max31790_hwmon_ops,
	.info = max31790_info,
};

static int max31790_init_client(struct i2c_client *client,
				struct max31790_data *data)
{
	int i, rv;

	for (i = 0; i < NR_CHANNEL; i++) {
		rv = i2c_smbus_read_byte_data(client,
				MAX31790_REG_FAN_CONFIG(i));
		if (rv < 0)
			return rv;
		data->fan_config[i] = rv;

		rv = i2c_smbus_read_byte_data(client,
				MAX31790_REG_FAN_DYNAMICS(i));
		if (rv < 0)
			return rv;
		data->fan_dynamics[i] = rv;
	}

	return 0;
}

static int max31790_probe(struct i2c_client *client,
			  const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct device *dev = &client->dev;
	struct max31790_data *data;
	struct device *hwmon_dev;
	int err;

	if (!i2c_check_functionality(adapter,
			I2C_FUNC_SMBUS_BYTE_DATA | I2C_FUNC_SMBUS_WORD_DATA))
		return -ENODEV;

	data = devm_kzalloc(dev, sizeof(struct max31790_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	mutex_init(&data->update_lock);

	/*
	 * Initialize the max31790 chip
	 */
	err = max31790_init_client(client, data);
	if (err)
		return err;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &max31790_chip_info,
							 NULL);

	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id max31790_id[] = {
	{ "max31790", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max31790_id);

static struct i2c_driver max31790_driver = {
	.class		= I2C_CLASS_HWMON,
	.probe		= max31790_probe,
	.driver = {
		.name	= "max31790",
	},
	.id_table	= max31790_id,
};

module_i2c_driver(max31790_driver);

MODULE_AUTHOR("Il Han <corone.il.han@gmail.com>");
MODULE_DESCRIPTION("MAX31790 sensor driver");
MODULE_LICENSE("GPL");
