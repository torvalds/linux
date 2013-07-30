/*
 * f75375s.c - driver for the Fintek F75375/SP, F75373 and
 *             F75387SG/RG hardware monitoring features
 * Copyright (C) 2006-2007  Riku Voipio
 *
 * Datasheets available at:
 *
 * f75375:
 * http://www.fintek.com.tw/files/productfiles/F75375_V026P.pdf
 *
 * f75373:
 * http://www.fintek.com.tw/files/productfiles/F75373_V025P.pdf
 *
 * f75387:
 * http://www.fintek.com.tw/files/productfiles/F75387_V027P.pdf
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#include <linux/module.h>
#include <linux/jiffies.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/f75375s.h>
#include <linux/slab.h>

/* Addresses to scan */
static const unsigned short normal_i2c[] = { 0x2d, 0x2e, I2C_CLIENT_END };

enum chips { f75373, f75375, f75387 };

/* Fintek F75375 registers  */
#define F75375_REG_CONFIG0		0x0
#define F75375_REG_CONFIG1		0x1
#define F75375_REG_CONFIG2		0x2
#define F75375_REG_CONFIG3		0x3
#define F75375_REG_ADDR			0x4
#define F75375_REG_INTR			0x31
#define F75375_CHIP_ID			0x5A
#define F75375_REG_VERSION		0x5C
#define F75375_REG_VENDOR		0x5D
#define F75375_REG_FAN_TIMER		0x60

#define F75375_REG_VOLT(nr)		(0x10 + (nr))
#define F75375_REG_VOLT_HIGH(nr)	(0x20 + (nr) * 2)
#define F75375_REG_VOLT_LOW(nr)		(0x21 + (nr) * 2)

#define F75375_REG_TEMP(nr)		(0x14 + (nr))
#define F75387_REG_TEMP11_LSB(nr)	(0x1a + (nr))
#define F75375_REG_TEMP_HIGH(nr)	(0x28 + (nr) * 2)
#define F75375_REG_TEMP_HYST(nr)	(0x29 + (nr) * 2)

#define F75375_REG_FAN(nr)		(0x16 + (nr) * 2)
#define F75375_REG_FAN_MIN(nr)		(0x2C + (nr) * 2)
#define F75375_REG_FAN_FULL(nr)		(0x70 + (nr) * 0x10)
#define F75375_REG_FAN_PWM_DUTY(nr)	(0x76 + (nr) * 0x10)
#define F75375_REG_FAN_PWM_CLOCK(nr)	(0x7D + (nr) * 0x10)

#define F75375_REG_FAN_EXP(nr)		(0x74 + (nr) * 0x10)
#define F75375_REG_FAN_B_TEMP(nr, step)	((0xA0 + (nr) * 0x10) + (step))
#define F75375_REG_FAN_B_SPEED(nr, step) \
	((0xA5 + (nr) * 0x10) + (step) * 2)

#define F75375_REG_PWM1_RAISE_DUTY	0x69
#define F75375_REG_PWM2_RAISE_DUTY	0x6A
#define F75375_REG_PWM1_DROP_DUTY	0x6B
#define F75375_REG_PWM2_DROP_DUTY	0x6C

#define F75375_FAN_CTRL_LINEAR(nr)	(4 + nr)
#define F75387_FAN_CTRL_LINEAR(nr)	(1 + ((nr) * 4))
#define FAN_CTRL_MODE(nr)		(4 + ((nr) * 2))
#define F75387_FAN_DUTY_MODE(nr)	(2 + ((nr) * 4))
#define F75387_FAN_MANU_MODE(nr)	((nr) * 4)

/*
 * Data structures and manipulation thereof
 */

struct f75375_data {
	unsigned short addr;
	struct device *hwmon_dev;

	const char *name;
	int kind;
	struct mutex update_lock; /* protect register access */
	char valid;
	unsigned long last_updated;	/* In jiffies */
	unsigned long last_limits;	/* In jiffies */

	/* Register values */
	u8 in[4];
	u8 in_max[4];
	u8 in_min[4];
	u16 fan[2];
	u16 fan_min[2];
	u16 fan_max[2];
	u16 fan_target[2];
	u8 fan_timer;
	u8 pwm[2];
	u8 pwm_mode[2];
	u8 pwm_enable[2];
	/*
	 * f75387: For remote temperature reading, it uses signed 11-bit
	 * values with LSB = 0.125 degree Celsius, left-justified in 16-bit
	 * registers. For original 8-bit temp readings, the LSB just is 0.
	 */
	s16 temp11[2];
	s8 temp_high[2];
	s8 temp_max_hyst[2];
};

static int f75375_detect(struct i2c_client *client,
			 struct i2c_board_info *info);
static int f75375_probe(struct i2c_client *client,
			const struct i2c_device_id *id);
static int f75375_remove(struct i2c_client *client);

static const struct i2c_device_id f75375_id[] = {
	{ "f75373", f75373 },
	{ "f75375", f75375 },
	{ "f75387", f75387 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, f75375_id);

static struct i2c_driver f75375_driver = {
	.class = I2C_CLASS_HWMON,
	.driver = {
		.name = "f75375",
	},
	.probe = f75375_probe,
	.remove = f75375_remove,
	.id_table = f75375_id,
	.detect = f75375_detect,
	.address_list = normal_i2c,
};

static inline int f75375_read8(struct i2c_client *client, u8 reg)
{
	return i2c_smbus_read_byte_data(client, reg);
}

/* in most cases, should be called while holding update_lock */
static inline u16 f75375_read16(struct i2c_client *client, u8 reg)
{
	return (i2c_smbus_read_byte_data(client, reg) << 8)
		| i2c_smbus_read_byte_data(client, reg + 1);
}

static inline void f75375_write8(struct i2c_client *client, u8 reg,
		u8 value)
{
	i2c_smbus_write_byte_data(client, reg, value);
}

static inline void f75375_write16(struct i2c_client *client, u8 reg,
		u16 value)
{
	int err = i2c_smbus_write_byte_data(client, reg, (value >> 8));
	if (err)
		return;
	i2c_smbus_write_byte_data(client, reg + 1, (value & 0xFF));
}

static void f75375_write_pwm(struct i2c_client *client, int nr)
{
	struct f75375_data *data = i2c_get_clientdata(client);
	if (data->kind == f75387)
		f75375_write16(client, F75375_REG_FAN_EXP(nr), data->pwm[nr]);
	else
		f75375_write8(client, F75375_REG_FAN_PWM_DUTY(nr),
			      data->pwm[nr]);
}

static struct f75375_data *f75375_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	int nr;

	mutex_lock(&data->update_lock);

	/* Limit registers cache is refreshed after 60 seconds */
	if (time_after(jiffies, data->last_limits + 60 * HZ)
		|| !data->valid) {
		for (nr = 0; nr < 2; nr++) {
			data->temp_high[nr] =
				f75375_read8(client, F75375_REG_TEMP_HIGH(nr));
			data->temp_max_hyst[nr] =
				f75375_read8(client, F75375_REG_TEMP_HYST(nr));
			data->fan_max[nr] =
				f75375_read16(client, F75375_REG_FAN_FULL(nr));
			data->fan_min[nr] =
				f75375_read16(client, F75375_REG_FAN_MIN(nr));
			data->fan_target[nr] =
				f75375_read16(client, F75375_REG_FAN_EXP(nr));
		}
		for (nr = 0; nr < 4; nr++) {
			data->in_max[nr] =
				f75375_read8(client, F75375_REG_VOLT_HIGH(nr));
			data->in_min[nr] =
				f75375_read8(client, F75375_REG_VOLT_LOW(nr));
		}
		data->fan_timer = f75375_read8(client, F75375_REG_FAN_TIMER);
		data->last_limits = jiffies;
	}

	/* Measurement registers cache is refreshed after 2 second */
	if (time_after(jiffies, data->last_updated + 2 * HZ)
		|| !data->valid) {
		for (nr = 0; nr < 2; nr++) {
			data->pwm[nr] =	f75375_read8(client,
				F75375_REG_FAN_PWM_DUTY(nr));
			/* assign MSB, therefore shift it by 8 bits */
			data->temp11[nr] =
				f75375_read8(client, F75375_REG_TEMP(nr)) << 8;
			if (data->kind == f75387)
				/* merge F75387's temperature LSB (11-bit) */
				data->temp11[nr] |=
					f75375_read8(client,
						     F75387_REG_TEMP11_LSB(nr));
			data->fan[nr] =
				f75375_read16(client, F75375_REG_FAN(nr));
		}
		for (nr = 0; nr < 4; nr++)
			data->in[nr] =
				f75375_read8(client, F75375_REG_VOLT(nr));

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);
	return data;
}

static inline u16 rpm_from_reg(u16 reg)
{
	if (reg == 0 || reg == 0xffff)
		return 0;
	return 1500000 / reg;
}

static inline u16 rpm_to_reg(int rpm)
{
	if (rpm < 367 || rpm > 0xffff)
		return 0xffff;
	return 1500000 / rpm;
}

static bool duty_mode_enabled(u8 pwm_enable)
{
	switch (pwm_enable) {
	case 0: /* Manual, duty mode (full speed) */
	case 1: /* Manual, duty mode */
	case 4: /* Auto, duty mode */
		return true;
	case 2: /* Auto, speed mode */
	case 3: /* Manual, speed mode */
		return false;
	default:
		BUG();
		return true;
	}
}

static bool auto_mode_enabled(u8 pwm_enable)
{
	switch (pwm_enable) {
	case 0: /* Manual, duty mode (full speed) */
	case 1: /* Manual, duty mode */
	case 3: /* Manual, speed mode */
		return false;
	case 2: /* Auto, speed mode */
	case 4: /* Auto, duty mode */
		return true;
	default:
		BUG();
		return false;
	}
}

static ssize_t set_fan_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	data->fan_min[nr] = rpm_to_reg(val);
	f75375_write16(client, F75375_REG_FAN_MIN(nr), data->fan_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_fan_target(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (auto_mode_enabled(data->pwm_enable[nr]))
		return -EINVAL;
	if (data->kind == f75387 && duty_mode_enabled(data->pwm_enable[nr]))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->fan_target[nr] = rpm_to_reg(val);
	f75375_write16(client, F75375_REG_FAN_EXP(nr), data->fan_target[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (auto_mode_enabled(data->pwm_enable[nr]) ||
	    !duty_mode_enabled(data->pwm_enable[nr]))
		return -EINVAL;

	mutex_lock(&data->update_lock);
	data->pwm[nr] = clamp_val(val, 0, 255);
	f75375_write_pwm(client, nr);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm_enable(struct device *dev, struct device_attribute
		*attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_enable[nr]);
}

static int set_pwm_enable_direct(struct i2c_client *client, int nr, int val)
{
	struct f75375_data *data = i2c_get_clientdata(client);
	u8 fanmode;

	if (val < 0 || val > 4)
		return -EINVAL;

	fanmode = f75375_read8(client, F75375_REG_FAN_TIMER);
	if (data->kind == f75387) {
		/* For now, deny dangerous toggling of duty mode */
		if (duty_mode_enabled(data->pwm_enable[nr]) !=
				duty_mode_enabled(val))
			return -EOPNOTSUPP;
		/* clear each fanX_mode bit before setting them properly */
		fanmode &= ~(1 << F75387_FAN_DUTY_MODE(nr));
		fanmode &= ~(1 << F75387_FAN_MANU_MODE(nr));
		switch (val) {
		case 0: /* full speed */
			fanmode |= (1 << F75387_FAN_MANU_MODE(nr));
			fanmode |= (1 << F75387_FAN_DUTY_MODE(nr));
			data->pwm[nr] = 255;
			break;
		case 1: /* PWM */
			fanmode  |= (1 << F75387_FAN_MANU_MODE(nr));
			fanmode  |= (1 << F75387_FAN_DUTY_MODE(nr));
			break;
		case 2: /* Automatic, speed mode */
			break;
		case 3: /* fan speed */
			fanmode |= (1 << F75387_FAN_MANU_MODE(nr));
			break;
		case 4: /* Automatic, pwm */
			fanmode |= (1 << F75387_FAN_DUTY_MODE(nr));
			break;
		}
	} else {
		/* clear each fanX_mode bit before setting them properly */
		fanmode &= ~(3 << FAN_CTRL_MODE(nr));
		switch (val) {
		case 0: /* full speed */
			fanmode  |= (3 << FAN_CTRL_MODE(nr));
			data->pwm[nr] = 255;
			break;
		case 1: /* PWM */
			fanmode  |= (3 << FAN_CTRL_MODE(nr));
			break;
		case 2: /* AUTOMATIC*/
			fanmode  |= (1 << FAN_CTRL_MODE(nr));
			break;
		case 3: /* fan speed */
			break;
		case 4: /* Automatic pwm */
			return -EINVAL;
		}
	}

	f75375_write8(client, F75375_REG_FAN_TIMER, fanmode);
	data->pwm_enable[nr] = val;
	if (val == 0)
		f75375_write_pwm(client, nr);
	return 0;
}

static ssize_t set_pwm_enable(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	mutex_lock(&data->update_lock);
	err = set_pwm_enable_direct(client, nr, val);
	mutex_unlock(&data->update_lock);
	return err ? err : count;
}

static ssize_t set_pwm_mode(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;
	u8 conf;
	char reg, ctrl;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	if (!(val == 0 || val == 1))
		return -EINVAL;

	/* F75373 does not support DC (linear voltage) fan control mode */
	if (data->kind == f75373 && val == 0)
		return -EINVAL;

	/* take care for different registers */
	if (data->kind == f75387) {
		reg = F75375_REG_FAN_TIMER;
		ctrl = F75387_FAN_CTRL_LINEAR(nr);
	} else {
		reg = F75375_REG_CONFIG1;
		ctrl = F75375_FAN_CTRL_LINEAR(nr);
	}

	mutex_lock(&data->update_lock);
	conf = f75375_read8(client, reg);
	conf &= ~(1 << ctrl);

	if (val == 0)
		conf |= (1 << ctrl);

	f75375_write8(client, reg, conf);
	data->pwm_mode[nr] = val;
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t show_pwm(struct device *dev, struct device_attribute
		*attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm[nr]);
}

static ssize_t show_pwm_mode(struct device *dev, struct device_attribute
		*attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", data->pwm_mode[nr]);
}

#define VOLT_FROM_REG(val) ((val) * 8)
#define VOLT_TO_REG(val) ((val) / 8)

static ssize_t show_in(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", VOLT_FROM_REG(data->in[nr]));
}

static ssize_t show_in_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", VOLT_FROM_REG(data->in_max[nr]));
}

static ssize_t show_in_min(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", VOLT_FROM_REG(data->in_min[nr]));
}

static ssize_t set_in_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(VOLT_TO_REG(val), 0, 0xff);
	mutex_lock(&data->update_lock);
	data->in_max[nr] = val;
	f75375_write8(client, F75375_REG_VOLT_HIGH(nr), data->in_max[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_in_min(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(VOLT_TO_REG(val), 0, 0xff);
	mutex_lock(&data->update_lock);
	data->in_min[nr] = val;
	f75375_write8(client, F75375_REG_VOLT_LOW(nr), data->in_min[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}
#define TEMP_FROM_REG(val) ((val) * 1000)
#define TEMP_TO_REG(val) ((val) / 1000)
#define TEMP11_FROM_REG(reg)	((reg) / 32 * 125)

static ssize_t show_temp11(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", TEMP11_FROM_REG(data->temp11[nr]));
}

static ssize_t show_temp_max(struct device *dev, struct device_attribute *attr,
		char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_high[nr]));
}

static ssize_t show_temp_max_hyst(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct f75375_data *data = f75375_update_device(dev);
	return sprintf(buf, "%d\n", TEMP_FROM_REG(data->temp_max_hyst[nr]));
}

static ssize_t set_temp_max(struct device *dev, struct device_attribute *attr,
		const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(TEMP_TO_REG(val), 0, 127);
	mutex_lock(&data->update_lock);
	data->temp_high[nr] = val;
	f75375_write8(client, F75375_REG_TEMP_HIGH(nr), data->temp_high[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

static ssize_t set_temp_max_hyst(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int nr = to_sensor_dev_attr(attr)->index;
	struct i2c_client *client = to_i2c_client(dev);
	struct f75375_data *data = i2c_get_clientdata(client);
	unsigned long val;
	int err;

	err = kstrtoul(buf, 10, &val);
	if (err < 0)
		return err;

	val = clamp_val(TEMP_TO_REG(val), 0, 127);
	mutex_lock(&data->update_lock);
	data->temp_max_hyst[nr] = val;
	f75375_write8(client, F75375_REG_TEMP_HYST(nr),
		data->temp_max_hyst[nr]);
	mutex_unlock(&data->update_lock);
	return count;
}

#define show_fan(thing) \
static ssize_t show_##thing(struct device *dev, struct device_attribute *attr, \
			char *buf)\
{\
	int nr = to_sensor_dev_attr(attr)->index;\
	struct f75375_data *data = f75375_update_device(dev); \
	return sprintf(buf, "%d\n", rpm_from_reg(data->thing[nr])); \
}

show_fan(fan);
show_fan(fan_min);
show_fan(fan_max);
show_fan(fan_target);

static SENSOR_DEVICE_ATTR(in0_input, S_IRUGO, show_in, NULL, 0);
static SENSOR_DEVICE_ATTR(in0_max, S_IRUGO|S_IWUSR,
	show_in_max, set_in_max, 0);
static SENSOR_DEVICE_ATTR(in0_min, S_IRUGO|S_IWUSR,
	show_in_min, set_in_min, 0);
static SENSOR_DEVICE_ATTR(in1_input, S_IRUGO, show_in, NULL, 1);
static SENSOR_DEVICE_ATTR(in1_max, S_IRUGO|S_IWUSR,
	show_in_max, set_in_max, 1);
static SENSOR_DEVICE_ATTR(in1_min, S_IRUGO|S_IWUSR,
	show_in_min, set_in_min, 1);
static SENSOR_DEVICE_ATTR(in2_input, S_IRUGO, show_in, NULL, 2);
static SENSOR_DEVICE_ATTR(in2_max, S_IRUGO|S_IWUSR,
	show_in_max, set_in_max, 2);
static SENSOR_DEVICE_ATTR(in2_min, S_IRUGO|S_IWUSR,
	show_in_min, set_in_min, 2);
static SENSOR_DEVICE_ATTR(in3_input, S_IRUGO, show_in, NULL, 3);
static SENSOR_DEVICE_ATTR(in3_max, S_IRUGO|S_IWUSR,
	show_in_max, set_in_max, 3);
static SENSOR_DEVICE_ATTR(in3_min, S_IRUGO|S_IWUSR,
	show_in_min, set_in_min, 3);
static SENSOR_DEVICE_ATTR(temp1_input, S_IRUGO, show_temp11, NULL, 0);
static SENSOR_DEVICE_ATTR(temp1_max_hyst, S_IRUGO|S_IWUSR,
	show_temp_max_hyst, set_temp_max_hyst, 0);
static SENSOR_DEVICE_ATTR(temp1_max, S_IRUGO|S_IWUSR,
	show_temp_max, set_temp_max, 0);
static SENSOR_DEVICE_ATTR(temp2_input, S_IRUGO, show_temp11, NULL, 1);
static SENSOR_DEVICE_ATTR(temp2_max_hyst, S_IRUGO|S_IWUSR,
	show_temp_max_hyst, set_temp_max_hyst, 1);
static SENSOR_DEVICE_ATTR(temp2_max, S_IRUGO|S_IWUSR,
	show_temp_max, set_temp_max, 1);
static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, show_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_max, S_IRUGO, show_fan_max, NULL, 0);
static SENSOR_DEVICE_ATTR(fan1_min, S_IRUGO|S_IWUSR,
	show_fan_min, set_fan_min, 0);
static SENSOR_DEVICE_ATTR(fan1_target, S_IRUGO|S_IWUSR,
	show_fan_target, set_fan_target, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, show_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan2_max, S_IRUGO, show_fan_max, NULL, 1);
static SENSOR_DEVICE_ATTR(fan2_min, S_IRUGO|S_IWUSR,
	show_fan_min, set_fan_min, 1);
static SENSOR_DEVICE_ATTR(fan2_target, S_IRUGO|S_IWUSR,
	show_fan_target, set_fan_target, 1);
static SENSOR_DEVICE_ATTR(pwm1, S_IRUGO|S_IWUSR,
	show_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm1_enable, S_IRUGO|S_IWUSR,
	show_pwm_enable, set_pwm_enable, 0);
static SENSOR_DEVICE_ATTR(pwm1_mode, S_IRUGO,
	show_pwm_mode, set_pwm_mode, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IRUGO | S_IWUSR,
	show_pwm, set_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IRUGO|S_IWUSR,
	show_pwm_enable, set_pwm_enable, 1);
static SENSOR_DEVICE_ATTR(pwm2_mode, S_IRUGO,
	show_pwm_mode, set_pwm_mode, 1);

static struct attribute *f75375_attributes[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	&sensor_dev_attr_temp1_max.dev_attr.attr,
	&sensor_dev_attr_temp1_max_hyst.dev_attr.attr,
	&sensor_dev_attr_temp2_input.dev_attr.attr,
	&sensor_dev_attr_temp2_max.dev_attr.attr,
	&sensor_dev_attr_temp2_max_hyst.dev_attr.attr,
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan1_max.dev_attr.attr,
	&sensor_dev_attr_fan1_min.dev_attr.attr,
	&sensor_dev_attr_fan1_target.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan2_max.dev_attr.attr,
	&sensor_dev_attr_fan2_min.dev_attr.attr,
	&sensor_dev_attr_fan2_target.dev_attr.attr,
	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm1_mode.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_mode.dev_attr.attr,
	&sensor_dev_attr_in0_input.dev_attr.attr,
	&sensor_dev_attr_in0_max.dev_attr.attr,
	&sensor_dev_attr_in0_min.dev_attr.attr,
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in1_max.dev_attr.attr,
	&sensor_dev_attr_in1_min.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in2_max.dev_attr.attr,
	&sensor_dev_attr_in2_min.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in3_max.dev_attr.attr,
	&sensor_dev_attr_in3_min.dev_attr.attr,
	NULL
};

static const struct attribute_group f75375_group = {
	.attrs = f75375_attributes,
};

static void f75375_init(struct i2c_client *client, struct f75375_data *data,
		struct f75375s_platform_data *f75375s_pdata)
{
	int nr;

	if (!f75375s_pdata) {
		u8 conf, mode;
		int nr;

		conf = f75375_read8(client, F75375_REG_CONFIG1);
		mode = f75375_read8(client, F75375_REG_FAN_TIMER);
		for (nr = 0; nr < 2; nr++) {
			if (data->kind == f75387) {
				bool manu, duty;

				if (!(mode & (1 << F75387_FAN_CTRL_LINEAR(nr))))
					data->pwm_mode[nr] = 1;

				manu = ((mode >> F75387_FAN_MANU_MODE(nr)) & 1);
				duty = ((mode >> F75387_FAN_DUTY_MODE(nr)) & 1);
				if (!manu && duty)
					/* auto, pwm */
					data->pwm_enable[nr] = 4;
				else if (manu && !duty)
					/* manual, speed */
					data->pwm_enable[nr] = 3;
				else if (!manu && !duty)
					/* automatic, speed */
					data->pwm_enable[nr] = 2;
				else
					/* manual, pwm */
					data->pwm_enable[nr] = 1;
			} else {
				if (!(conf & (1 << F75375_FAN_CTRL_LINEAR(nr))))
					data->pwm_mode[nr] = 1;

				switch ((mode >> FAN_CTRL_MODE(nr)) & 3) {
				case 0:		/* speed */
					data->pwm_enable[nr] = 3;
					break;
				case 1:		/* automatic */
					data->pwm_enable[nr] = 2;
					break;
				default:	/* manual */
					data->pwm_enable[nr] = 1;
					break;
				}
			}
		}
		return;
	}

	set_pwm_enable_direct(client, 0, f75375s_pdata->pwm_enable[0]);
	set_pwm_enable_direct(client, 1, f75375s_pdata->pwm_enable[1]);
	for (nr = 0; nr < 2; nr++) {
		if (auto_mode_enabled(f75375s_pdata->pwm_enable[nr]) ||
		    !duty_mode_enabled(f75375s_pdata->pwm_enable[nr]))
			continue;
		data->pwm[nr] = clamp_val(f75375s_pdata->pwm[nr], 0, 255);
		f75375_write_pwm(client, nr);
	}

}

static int f75375_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	struct f75375_data *data;
	struct f75375s_platform_data *f75375s_pdata =
			dev_get_platdata(&client->dev);
	int err;

	if (!i2c_check_functionality(client->adapter,
				I2C_FUNC_SMBUS_BYTE_DATA))
		return -EIO;
	data = devm_kzalloc(&client->dev, sizeof(struct f75375_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);
	data->kind = id->driver_data;

	err = sysfs_create_group(&client->dev.kobj, &f75375_group);
	if (err)
		return err;

	if (data->kind != f75373) {
		err = sysfs_chmod_file(&client->dev.kobj,
			&sensor_dev_attr_pwm1_mode.dev_attr.attr,
			S_IRUGO | S_IWUSR);
		if (err)
			goto exit_remove;
		err = sysfs_chmod_file(&client->dev.kobj,
			&sensor_dev_attr_pwm2_mode.dev_attr.attr,
			S_IRUGO | S_IWUSR);
		if (err)
			goto exit_remove;
	}

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		err = PTR_ERR(data->hwmon_dev);
		goto exit_remove;
	}

	f75375_init(client, data, f75375s_pdata);

	return 0;

exit_remove:
	sysfs_remove_group(&client->dev.kobj, &f75375_group);
	return err;
}

static int f75375_remove(struct i2c_client *client)
{
	struct f75375_data *data = i2c_get_clientdata(client);
	hwmon_device_unregister(data->hwmon_dev);
	sysfs_remove_group(&client->dev.kobj, &f75375_group);
	return 0;
}

/* Return 0 if detection is successful, -ENODEV otherwise */
static int f75375_detect(struct i2c_client *client,
			 struct i2c_board_info *info)
{
	struct i2c_adapter *adapter = client->adapter;
	u16 vendid, chipid;
	u8 version;
	const char *name;

	vendid = f75375_read16(client, F75375_REG_VENDOR);
	chipid = f75375_read16(client, F75375_CHIP_ID);
	if (vendid != 0x1934)
		return -ENODEV;

	if (chipid == 0x0306)
		name = "f75375";
	else if (chipid == 0x0204)
		name = "f75373";
	else if (chipid == 0x0410)
		name = "f75387";
	else
		return -ENODEV;

	version = f75375_read8(client, F75375_REG_VERSION);
	dev_info(&adapter->dev, "found %s version: %02X\n", name, version);
	strlcpy(info->type, name, I2C_NAME_SIZE);

	return 0;
}

module_i2c_driver(f75375_driver);

MODULE_AUTHOR("Riku Voipio");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F75373/F75375/F75387 hardware monitoring driver");
