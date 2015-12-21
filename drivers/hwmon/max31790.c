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
#include <linux/hwmon-sysfs.h>
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

static ssize_t get_fan(struct device *dev,
		       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = max31790_update_device(dev);
	int sr, rpm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	sr = get_tach_period(data->fan_dynamics[attr->index]);
	rpm = RPM_FROM_REG(data->tach[attr->index], sr);

	return sprintf(buf, "%d\n", rpm);
}

static ssize_t get_fan_target(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = max31790_update_device(dev);
	int sr, rpm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	sr = get_tach_period(data->fan_dynamics[attr->index]);
	rpm = RPM_FROM_REG(data->target_count[attr->index], sr);

	return sprintf(buf, "%d\n", rpm);
}

static ssize_t set_fan_target(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 bits;
	int sr;
	int target_count;
	unsigned long rpm;
	int err;

	err = kstrtoul(buf, 10, &rpm);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	rpm = clamp_val(rpm, FAN_RPM_MIN, FAN_RPM_MAX);
	bits = bits_for_tach_period(rpm);
	data->fan_dynamics[attr->index] =
			((data->fan_dynamics[attr->index]
			  & ~MAX31790_FAN_DYN_SR_MASK)
			 | (bits << MAX31790_FAN_DYN_SR_SHIFT));
	err = i2c_smbus_write_byte_data(client,
			MAX31790_REG_FAN_DYNAMICS(attr->index),
			data->fan_dynamics[attr->index]);

	if (err < 0) {
		mutex_unlock(&data->update_lock);
		return err;
	}

	sr = get_tach_period(data->fan_dynamics[attr->index]);
	target_count = RPM_TO_REG(rpm, sr);
	target_count = clamp_val(target_count, 0x1, 0x7FF);

	data->target_count[attr->index] = target_count << 5;

	err = i2c_smbus_write_word_swapped(client,
			MAX31790_REG_TARGET_COUNT(attr->index),
			data->target_count[attr->index]);

	mutex_unlock(&data->update_lock);

	if (err < 0)
		return err;

	return count;
}

static ssize_t get_pwm(struct device *dev,
		       struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = max31790_update_device(dev);
	int pwm;

	if (IS_ERR(data))
		return PTR_ERR(data);

	pwm = data->pwm[attr->index] >> 8;

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t set_pwm(struct device *dev,
		       struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long pwm;
	int err;

	err = kstrtoul(buf, 10, &pwm);
	if (err)
		return err;

	if (pwm > 255)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	data->pwm[attr->index] = pwm << 8;
	err = i2c_smbus_write_word_swapped(client,
			MAX31790_REG_PWMOUT(attr->index),
			data->pwm[attr->index]);

	mutex_unlock(&data->update_lock);

	if (err < 0)
		return err;

	return count;
}

static ssize_t get_pwm_enable(struct device *dev,
			      struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = max31790_update_device(dev);
	int mode;

	if (IS_ERR(data))
		return PTR_ERR(data);

	if (data->fan_config[attr->index] & MAX31790_FAN_CFG_RPM_MODE)
		mode = 2;
	else if (data->fan_config[attr->index] & MAX31790_FAN_CFG_TACH_INPUT_EN)
		mode = 1;
	else
		mode = 0;

	return sprintf(buf, "%d\n", mode);
}

static ssize_t set_pwm_enable(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long mode;
	int err;

	err = kstrtoul(buf, 10, &mode);
	if (err)
		return err;

	switch (mode) {
	case 0:
		data->fan_config[attr->index] =
			data->fan_config[attr->index]
			& ~(MAX31790_FAN_CFG_TACH_INPUT_EN
			    | MAX31790_FAN_CFG_RPM_MODE);
		break;
	case 1:
		data->fan_config[attr->index] =
			(data->fan_config[attr->index]
			 | MAX31790_FAN_CFG_TACH_INPUT_EN)
			& ~MAX31790_FAN_CFG_RPM_MODE;
		break;
	case 2:
		data->fan_config[attr->index] =
			data->fan_config[attr->index]
			| MAX31790_FAN_CFG_TACH_INPUT_EN
			| MAX31790_FAN_CFG_RPM_MODE;
		break;
	default:
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);

	err = i2c_smbus_write_byte_data(client,
			MAX31790_REG_FAN_CONFIG(attr->index),
			data->fan_config[attr->index]);

	mutex_unlock(&data->update_lock);

	if (err < 0)
		return err;

	return count;
}

static ssize_t get_fan_fault(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max31790_data *data = max31790_update_device(dev);
	int fault;

	if (IS_ERR(data))
		return PTR_ERR(data);

	fault = !!(data->fault_status & (1 << attr->index));

	return sprintf(buf, "%d\n", fault);
}

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, get_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, get_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, get_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, get_fan, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_input, S_IRUGO, get_fan, NULL, 4);
static SENSOR_DEVICE_ATTR(fan6_input, S_IRUGO, get_fan, NULL, 5);

static SENSOR_DEVICE_ATTR(fan1_fault, S_IRUGO, get_fan_fault, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_fault, S_IRUGO, get_fan_fault, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_fault, S_IRUGO, get_fan_fault, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_fault, S_IRUGO, get_fan_fault, NULL, 3);
static SENSOR_DEVICE_ATTR(fan5_fault, S_IRUGO, get_fan_fault, NULL, 4);
static SENSOR_DEVICE_ATTR(fan6_fault, S_IRUGO, get_fan_fault, NULL, 5);

static SENSOR_DEVICE_ATTR(fan7_input, S_IRUGO, get_fan, NULL, 6);
static SENSOR_DEVICE_ATTR(fan8_input, S_IRUGO, get_fan, NULL, 7);
static SENSOR_DEVICE_ATTR(fan9_input, S_IRUGO, get_fan, NULL, 8);
static SENSOR_DEVICE_ATTR(fan10_input, S_IRUGO, get_fan, NULL, 9);
static SENSOR_DEVICE_ATTR(fan11_input, S_IRUGO, get_fan, NULL, 10);
static SENSOR_DEVICE_ATTR(fan12_input, S_IRUGO, get_fan, NULL, 11);

static SENSOR_DEVICE_ATTR(fan7_fault, S_IRUGO, get_fan_fault, NULL, 6);
static SENSOR_DEVICE_ATTR(fan8_fault, S_IRUGO, get_fan_fault, NULL, 7);
static SENSOR_DEVICE_ATTR(fan9_fault, S_IRUGO, get_fan_fault, NULL, 8);
static SENSOR_DEVICE_ATTR(fan10_fault, S_IRUGO, get_fan_fault, NULL, 9);
static SENSOR_DEVICE_ATTR(fan11_fault, S_IRUGO, get_fan_fault, NULL, 10);
static SENSOR_DEVICE_ATTR(fan12_fault, S_IRUGO, get_fan_fault, NULL, 11);

static SENSOR_DEVICE_ATTR(fan1_target, S_IWUSR | S_IRUGO,
		get_fan_target, set_fan_target, 0);
static SENSOR_DEVICE_ATTR(fan2_target, S_IWUSR | S_IRUGO,
		get_fan_target, set_fan_target, 1);
static SENSOR_DEVICE_ATTR(fan3_target, S_IWUSR | S_IRUGO,
		get_fan_target, set_fan_target, 2);
static SENSOR_DEVICE_ATTR(fan4_target, S_IWUSR | S_IRUGO,
		get_fan_target, set_fan_target, 3);
static SENSOR_DEVICE_ATTR(fan5_target, S_IWUSR | S_IRUGO,
		get_fan_target, set_fan_target, 4);
static SENSOR_DEVICE_ATTR(fan6_target, S_IWUSR | S_IRUGO,
		get_fan_target, set_fan_target, 5);

static SENSOR_DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, get_pwm, set_pwm, 0);
static SENSOR_DEVICE_ATTR(pwm2, S_IWUSR | S_IRUGO, get_pwm, set_pwm, 1);
static SENSOR_DEVICE_ATTR(pwm3, S_IWUSR | S_IRUGO, get_pwm, set_pwm, 2);
static SENSOR_DEVICE_ATTR(pwm4, S_IWUSR | S_IRUGO, get_pwm, set_pwm, 3);
static SENSOR_DEVICE_ATTR(pwm5, S_IWUSR | S_IRUGO, get_pwm, set_pwm, 4);
static SENSOR_DEVICE_ATTR(pwm6, S_IWUSR | S_IRUGO, get_pwm, set_pwm, 5);

static SENSOR_DEVICE_ATTR(pwm1_enable, S_IWUSR | S_IRUGO,
		get_pwm_enable, set_pwm_enable, 0);
static SENSOR_DEVICE_ATTR(pwm2_enable, S_IWUSR | S_IRUGO,
		get_pwm_enable, set_pwm_enable, 1);
static SENSOR_DEVICE_ATTR(pwm3_enable, S_IWUSR | S_IRUGO,
		get_pwm_enable, set_pwm_enable, 2);
static SENSOR_DEVICE_ATTR(pwm4_enable, S_IWUSR | S_IRUGO,
		get_pwm_enable, set_pwm_enable, 3);
static SENSOR_DEVICE_ATTR(pwm5_enable, S_IWUSR | S_IRUGO,
		get_pwm_enable, set_pwm_enable, 4);
static SENSOR_DEVICE_ATTR(pwm6_enable, S_IWUSR | S_IRUGO,
		get_pwm_enable, set_pwm_enable, 5);

static struct attribute *max31790_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&sensor_dev_attr_fan5_input.dev_attr.attr,
	&sensor_dev_attr_fan6_input.dev_attr.attr,

	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_fan2_fault.dev_attr.attr,
	&sensor_dev_attr_fan3_fault.dev_attr.attr,
	&sensor_dev_attr_fan4_fault.dev_attr.attr,
	&sensor_dev_attr_fan5_fault.dev_attr.attr,
	&sensor_dev_attr_fan6_fault.dev_attr.attr,

	&sensor_dev_attr_fan7_input.dev_attr.attr,
	&sensor_dev_attr_fan8_input.dev_attr.attr,
	&sensor_dev_attr_fan9_input.dev_attr.attr,
	&sensor_dev_attr_fan10_input.dev_attr.attr,
	&sensor_dev_attr_fan11_input.dev_attr.attr,
	&sensor_dev_attr_fan12_input.dev_attr.attr,

	&sensor_dev_attr_fan7_fault.dev_attr.attr,
	&sensor_dev_attr_fan8_fault.dev_attr.attr,
	&sensor_dev_attr_fan9_fault.dev_attr.attr,
	&sensor_dev_attr_fan10_fault.dev_attr.attr,
	&sensor_dev_attr_fan11_fault.dev_attr.attr,
	&sensor_dev_attr_fan12_fault.dev_attr.attr,

	&sensor_dev_attr_fan1_target.dev_attr.attr,
	&sensor_dev_attr_fan2_target.dev_attr.attr,
	&sensor_dev_attr_fan3_target.dev_attr.attr,
	&sensor_dev_attr_fan4_target.dev_attr.attr,
	&sensor_dev_attr_fan5_target.dev_attr.attr,
	&sensor_dev_attr_fan6_target.dev_attr.attr,

	&sensor_dev_attr_pwm1.dev_attr.attr,
	&sensor_dev_attr_pwm2.dev_attr.attr,
	&sensor_dev_attr_pwm3.dev_attr.attr,
	&sensor_dev_attr_pwm4.dev_attr.attr,
	&sensor_dev_attr_pwm5.dev_attr.attr,
	&sensor_dev_attr_pwm6.dev_attr.attr,

	&sensor_dev_attr_pwm1_enable.dev_attr.attr,
	&sensor_dev_attr_pwm2_enable.dev_attr.attr,
	&sensor_dev_attr_pwm3_enable.dev_attr.attr,
	&sensor_dev_attr_pwm4_enable.dev_attr.attr,
	&sensor_dev_attr_pwm5_enable.dev_attr.attr,
	&sensor_dev_attr_pwm6_enable.dev_attr.attr,
	NULL
};

static umode_t max31790_attrs_visible(struct kobject *kobj,
				     struct attribute *a, int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct max31790_data *data = dev_get_drvdata(dev);
	struct device_attribute *devattr =
			container_of(a, struct device_attribute, attr);
	int index = to_sensor_dev_attr(devattr)->index % NR_CHANNEL;
	u8 fan_config;

	fan_config = data->fan_config[index];

	if (n >= NR_CHANNEL * 2 && n < NR_CHANNEL * 4 &&
	    !(fan_config & MAX31790_FAN_CFG_TACH_INPUT))
		return 0;
	if (n >= NR_CHANNEL * 4 && (fan_config & MAX31790_FAN_CFG_TACH_INPUT))
		return 0;

	return a->mode;
}

static const struct attribute_group max31790_group = {
	.attrs = max31790_attrs,
	.is_visible = max31790_attrs_visible,
};
__ATTRIBUTE_GROUPS(max31790);

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

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
			client->name, data, max31790_groups);

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
