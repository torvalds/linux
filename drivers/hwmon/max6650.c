// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * max6650.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring.
 *
 * (C) 2007 by Hans J. Koch <hjk@hansjkoch.de>
 *
 * based on code written by John Morris <john.morris@spirentcom.com>
 * Copyright (c) 2003 Spirent Communications
 * and Claus Gindhart <claus.gindhart@kontron.com>
 *
 * This module has only been tested with the MAX6650 chip. It should
 * also work with the MAX6651. It does not distinguish max6650 and max6651
 * chips.
 *
 * The datasheet was last seen at:
 *
 *        http://pdfserv.maxim-ic.com/en/ds/MAX6650-MAX6651.pdf
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>
#include <linux/of_device.h>
#include <linux/thermal.h>

/*
 * Insmod parameters
 */

/* fan_voltage: 5=5V fan, 12=12V fan, 0=don't change */
static int fan_voltage;
/* prescaler: Possible values are 1, 2, 4, 8, 16 or 0 for don't change */
static int prescaler;
/* clock: The clock frequency of the chip (max6651 can be clocked externally) */
static int clock = 254000;

module_param(fan_voltage, int, 0444);
module_param(prescaler, int, 0444);
module_param(clock, int, 0444);

/*
 * MAX 6650/6651 registers
 */

#define MAX6650_REG_SPEED	0x00
#define MAX6650_REG_CONFIG	0x02
#define MAX6650_REG_GPIO_DEF	0x04
#define MAX6650_REG_DAC		0x06
#define MAX6650_REG_ALARM_EN	0x08
#define MAX6650_REG_ALARM	0x0A
#define MAX6650_REG_TACH0	0x0C
#define MAX6650_REG_TACH1	0x0E
#define MAX6650_REG_TACH2	0x10
#define MAX6650_REG_TACH3	0x12
#define MAX6650_REG_GPIO_STAT	0x14
#define MAX6650_REG_COUNT	0x16

/*
 * Config register bits
 */

#define MAX6650_CFG_V12			0x08
#define MAX6650_CFG_PRESCALER_MASK	0x07
#define MAX6650_CFG_PRESCALER_2		0x01
#define MAX6650_CFG_PRESCALER_4		0x02
#define MAX6650_CFG_PRESCALER_8		0x03
#define MAX6650_CFG_PRESCALER_16	0x04
#define MAX6650_CFG_MODE_MASK		0x30
#define MAX6650_CFG_MODE_ON		0x00
#define MAX6650_CFG_MODE_OFF		0x10
#define MAX6650_CFG_MODE_CLOSED_LOOP	0x20
#define MAX6650_CFG_MODE_OPEN_LOOP	0x30
#define MAX6650_COUNT_MASK		0x03

/*
 * Alarm status register bits
 */

#define MAX6650_ALRM_MAX	0x01
#define MAX6650_ALRM_MIN	0x02
#define MAX6650_ALRM_TACH	0x04
#define MAX6650_ALRM_GPIO1	0x08
#define MAX6650_ALRM_GPIO2	0x10

/* Minimum and maximum values of the FAN-RPM */
#define FAN_RPM_MIN 240
#define FAN_RPM_MAX 30000

#define DIV_FROM_REG(reg)	(1 << ((reg) & 7))
#define DAC_LIMIT(v12)		((v12) ? 180 : 76)

/*
 * Client data (each client gets its own)
 */

struct max6650_data {
	struct i2c_client *client;
	const struct attribute_group *groups[3];
	struct mutex update_lock;
	int nr_fans;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* register values */
	u8 speed;
	u8 config;
	u8 tach[4];
	u8 count;
	u8 dac;
	u8 alarm;
	unsigned long cooling_dev_state;
};

static const u8 tach_reg[] = {
	MAX6650_REG_TACH0,
	MAX6650_REG_TACH1,
	MAX6650_REG_TACH2,
	MAX6650_REG_TACH3,
};

static const struct of_device_id __maybe_unused max6650_dt_match[] = {
	{
		.compatible = "maxim,max6650",
		.data = (void *)1
	},
	{
		.compatible = "maxim,max6651",
		.data = (void *)4
	},
	{ },
};
MODULE_DEVICE_TABLE(of, max6650_dt_match);

static int dac_to_pwm(int dac, bool v12)
{
	/*
	 * Useful range for dac is 0-180 for 12V fans and 0-76 for 5V fans.
	 * Lower DAC values mean higher speeds.
	 */
	return clamp_val(255 - (255 * dac) / DAC_LIMIT(v12), 0, 255);
}

static u8 pwm_to_dac(unsigned int pwm, bool v12)
{
	int limit = DAC_LIMIT(v12);

	return limit - (limit * pwm) / 255;
}

static struct max6650_data *max6650_update_device(struct device *dev)
{
	struct max6650_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		data->speed = i2c_smbus_read_byte_data(client,
						       MAX6650_REG_SPEED);
		data->config = i2c_smbus_read_byte_data(client,
							MAX6650_REG_CONFIG);
		for (i = 0; i < data->nr_fans; i++) {
			data->tach[i] = i2c_smbus_read_byte_data(client,
								 tach_reg[i]);
		}
		data->count = i2c_smbus_read_byte_data(client,
							MAX6650_REG_COUNT);
		data->dac = i2c_smbus_read_byte_data(client, MAX6650_REG_DAC);

		/*
		 * Alarms are cleared on read in case the condition that
		 * caused the alarm is removed. Keep the value latched here
		 * for providing the register through different alarm files.
		 */
		data->alarm |= i2c_smbus_read_byte_data(client,
							MAX6650_REG_ALARM);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/*
 * Change the operating mode of the chip (if needed).
 * mode is one of the MAX6650_CFG_MODE_* values.
 */
static int max6650_set_operating_mode(struct max6650_data *data, u8 mode)
{
	int result;
	u8 config = data->config;

	if (mode == (config & MAX6650_CFG_MODE_MASK))
		return 0;

	config = (config & ~MAX6650_CFG_MODE_MASK) | mode;

	result = i2c_smbus_write_byte_data(data->client, MAX6650_REG_CONFIG,
					   config);
	if (result < 0)
		return result;

	data->config = config;

	return 0;
}

static ssize_t fan_show(struct device *dev, struct device_attribute *devattr,
			char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max6650_data *data = max6650_update_device(dev);
	int rpm;

	/*
	 * Calculation details:
	 *
	 * Each tachometer counts over an interval given by the "count"
	 * register (0.25, 0.5, 1 or 2 seconds). This module assumes
	 * that the fans produce two pulses per revolution (this seems
	 * to be the most common).
	 */

	rpm = ((data->tach[attr->index] * 120) / DIV_FROM_REG(data->count));
	return sprintf(buf, "%d\n", rpm);
}

/*
 * Set the fan speed to the specified RPM (or read back the RPM setting).
 * This works in closed loop mode only. Use pwm1 for open loop speed setting.
 *
 * The MAX6650/1 will automatically control fan speed when in closed loop
 * mode.
 *
 * Assumptions:
 *
 * 1) The MAX6650/1 internal 254kHz clock frequency is set correctly. Use
 *    the clock module parameter if you need to fine tune this.
 *
 * 2) The prescaler (low three bits of the config register) has already
 *    been set to an appropriate value. Use the prescaler module parameter
 *    if your BIOS doesn't initialize the chip properly.
 *
 * The relevant equations are given on pages 21 and 22 of the datasheet.
 *
 * From the datasheet, the relevant equation when in regulation is:
 *
 *    [fCLK / (128 x (KTACH + 1))] = 2 x FanSpeed / KSCALE
 *
 * where:
 *
 *    fCLK is the oscillator frequency (either the 254kHz internal
 *         oscillator or the externally applied clock)
 *
 *    KTACH is the value in the speed register
 *
 *    FanSpeed is the speed of the fan in rps
 *
 *    KSCALE is the prescaler value (1, 2, 4, 8, or 16)
 *
 * When reading, we need to solve for FanSpeed. When writing, we need to
 * solve for KTACH.
 *
 * Note: this tachometer is completely separate from the tachometers
 * used to measure the fan speeds. Only one fan's speed (fan1) is
 * controlled.
 */

static ssize_t fan1_target_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct max6650_data *data = max6650_update_device(dev);
	int kscale, ktach, rpm;

	/*
	 * Use the datasheet equation:
	 *
	 *    FanSpeed = KSCALE x fCLK / [256 x (KTACH + 1)]
	 *
	 * then multiply by 60 to give rpm.
	 */

	kscale = DIV_FROM_REG(data->config);
	ktach = data->speed;
	rpm = 60 * kscale * clock / (256 * (ktach + 1));
	return sprintf(buf, "%d\n", rpm);
}

static int max6650_set_target(struct max6650_data *data, unsigned long rpm)
{
	int kscale, ktach;

	if (rpm == 0)
		return max6650_set_operating_mode(data, MAX6650_CFG_MODE_OFF);

	rpm = clamp_val(rpm, FAN_RPM_MIN, FAN_RPM_MAX);

	/*
	 * Divide the required speed by 60 to get from rpm to rps, then
	 * use the datasheet equation:
	 *
	 *     KTACH = [(fCLK x KSCALE) / (256 x FanSpeed)] - 1
	 */

	kscale = DIV_FROM_REG(data->config);
	ktach = ((clock * kscale) / (256 * rpm / 60)) - 1;
	if (ktach < 0)
		ktach = 0;
	if (ktach > 255)
		ktach = 255;
	data->speed = ktach;

	return i2c_smbus_write_byte_data(data->client, MAX6650_REG_SPEED,
					 data->speed);
}

static ssize_t fan1_target_store(struct device *dev,
				 struct device_attribute *devattr,
				 const char *buf, size_t count)
{
	struct max6650_data *data = dev_get_drvdata(dev);
	unsigned long rpm;
	int err;

	err = kstrtoul(buf, 10, &rpm);
	if (err)
		return err;

	mutex_lock(&data->update_lock);

	err = max6650_set_target(data, rpm);

	mutex_unlock(&data->update_lock);

	if (err < 0)
		return err;

	return count;
}

/*
 * Get/set the fan speed in open loop mode using pwm1 sysfs file.
 * Speed is given as a relative value from 0 to 255, where 255 is maximum
 * speed. Note that this is done by writing directly to the chip's DAC,
 * it won't change the closed loop speed set by fan1_target.
 * Also note that due to rounding errors it is possible that you don't read
 * back exactly the value you have set.
 */

static ssize_t pwm1_show(struct device *dev, struct device_attribute *devattr,
			 char *buf)
{
	struct max6650_data *data = max6650_update_device(dev);

	return sprintf(buf, "%d\n", dac_to_pwm(data->dac,
					       data->config & MAX6650_CFG_V12));
}

static ssize_t pwm1_store(struct device *dev,
			  struct device_attribute *devattr, const char *buf,
			  size_t count)
{
	struct max6650_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long pwm;
	int err;
	u8 dac;

	err = kstrtoul(buf, 10, &pwm);
	if (err)
		return err;

	pwm = clamp_val(pwm, 0, 255);

	mutex_lock(&data->update_lock);
	dac = pwm_to_dac(pwm, data->config & MAX6650_CFG_V12);
	err = i2c_smbus_write_byte_data(client, MAX6650_REG_DAC, dac);
	if (!err)
		data->dac = dac;
	mutex_unlock(&data->update_lock);

	return err < 0 ? err : count;
}

/*
 * Get/Set controller mode:
 * Possible values:
 * 0 = Fan always on
 * 1 = Open loop, Voltage is set according to speed, not regulated.
 * 2 = Closed loop, RPM for all fans regulated by fan1 tachometer
 * 3 = Fan off
 */
static ssize_t pwm1_enable_show(struct device *dev,
				struct device_attribute *devattr, char *buf)
{
	struct max6650_data *data = max6650_update_device(dev);
	int mode = (data->config & MAX6650_CFG_MODE_MASK) >> 4;
	int sysfs_modes[4] = {0, 3, 2, 1};

	return sprintf(buf, "%d\n", sysfs_modes[mode]);
}

static ssize_t pwm1_enable_store(struct device *dev,
				 struct device_attribute *devattr,
				 const char *buf, size_t count)
{
	struct max6650_data *data = dev_get_drvdata(dev);
	unsigned long mode;
	int err;
	const u8 max6650_modes[] = {
		MAX6650_CFG_MODE_ON,
		MAX6650_CFG_MODE_OPEN_LOOP,
		MAX6650_CFG_MODE_CLOSED_LOOP,
		MAX6650_CFG_MODE_OFF,
		};

	err = kstrtoul(buf, 10, &mode);
	if (err)
		return err;

	if (mode >= ARRAY_SIZE(max6650_modes))
		return -EINVAL;

	mutex_lock(&data->update_lock);

	max6650_set_operating_mode(data, max6650_modes[mode]);

	mutex_unlock(&data->update_lock);

	return count;
}

/*
 * Read/write functions for fan1_div sysfs file. The MAX6650 has no such
 * divider. We handle this by converting between divider and counttime:
 *
 * (counttime == k) <==> (divider == 2^k), k = 0, 1, 2, or 3
 *
 * Lower values of k allow to connect a faster fan without the risk of
 * counter overflow. The price is lower resolution. You can also set counttime
 * using the module parameter. Note that the module parameter "prescaler" also
 * influences the behaviour. Unfortunately, there's no sysfs attribute
 * defined for that. See the data sheet for details.
 */

static ssize_t fan1_div_show(struct device *dev,
			     struct device_attribute *devattr, char *buf)
{
	struct max6650_data *data = max6650_update_device(dev);

	return sprintf(buf, "%d\n", DIV_FROM_REG(data->count));
}

static ssize_t fan1_div_store(struct device *dev,
			      struct device_attribute *devattr,
			      const char *buf, size_t count)
{
	struct max6650_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	unsigned long div;
	int err;

	err = kstrtoul(buf, 10, &div);
	if (err)
		return err;

	mutex_lock(&data->update_lock);
	switch (div) {
	case 1:
		data->count = 0;
		break;
	case 2:
		data->count = 1;
		break;
	case 4:
		data->count = 2;
		break;
	case 8:
		data->count = 3;
		break;
	default:
		mutex_unlock(&data->update_lock);
		return -EINVAL;
	}

	i2c_smbus_write_byte_data(client, MAX6650_REG_COUNT, data->count);
	mutex_unlock(&data->update_lock);

	return count;
}

/*
 * Get alarm stati:
 * Possible values:
 * 0 = no alarm
 * 1 = alarm
 */

static ssize_t alarm_show(struct device *dev,
			  struct device_attribute *devattr, char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(devattr);
	struct max6650_data *data = max6650_update_device(dev);
	struct i2c_client *client = data->client;
	int alarm = 0;

	if (data->alarm & attr->index) {
		mutex_lock(&data->update_lock);
		alarm = 1;
		data->alarm &= ~attr->index;
		data->alarm |= i2c_smbus_read_byte_data(client,
							MAX6650_REG_ALARM);
		mutex_unlock(&data->update_lock);
	}

	return sprintf(buf, "%d\n", alarm);
}

static SENSOR_DEVICE_ATTR_RO(fan1_input, fan, 0);
static SENSOR_DEVICE_ATTR_RO(fan2_input, fan, 1);
static SENSOR_DEVICE_ATTR_RO(fan3_input, fan, 2);
static SENSOR_DEVICE_ATTR_RO(fan4_input, fan, 3);
static DEVICE_ATTR_RW(fan1_target);
static DEVICE_ATTR_RW(fan1_div);
static DEVICE_ATTR_RW(pwm1_enable);
static DEVICE_ATTR_RW(pwm1);
static SENSOR_DEVICE_ATTR_RO(fan1_max_alarm, alarm, MAX6650_ALRM_MAX);
static SENSOR_DEVICE_ATTR_RO(fan1_min_alarm, alarm, MAX6650_ALRM_MIN);
static SENSOR_DEVICE_ATTR_RO(fan1_fault, alarm, MAX6650_ALRM_TACH);
static SENSOR_DEVICE_ATTR_RO(gpio1_alarm, alarm, MAX6650_ALRM_GPIO1);
static SENSOR_DEVICE_ATTR_RO(gpio2_alarm, alarm, MAX6650_ALRM_GPIO2);

static umode_t max6650_attrs_visible(struct kobject *kobj, struct attribute *a,
				    int n)
{
	struct device *dev = container_of(kobj, struct device, kobj);
	struct max6650_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	u8 alarm_en = i2c_smbus_read_byte_data(client, MAX6650_REG_ALARM_EN);
	struct device_attribute *devattr;

	/*
	 * Hide the alarms that have not been enabled by the firmware
	 */

	devattr = container_of(a, struct device_attribute, attr);
	if (devattr == &sensor_dev_attr_fan1_max_alarm.dev_attr
	 || devattr == &sensor_dev_attr_fan1_min_alarm.dev_attr
	 || devattr == &sensor_dev_attr_fan1_fault.dev_attr
	 || devattr == &sensor_dev_attr_gpio1_alarm.dev_attr
	 || devattr == &sensor_dev_attr_gpio2_alarm.dev_attr) {
		if (!(alarm_en & to_sensor_dev_attr(devattr)->index))
			return 0;
	}

	return a->mode;
}

static struct attribute *max6650_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&dev_attr_fan1_target.attr,
	&dev_attr_fan1_div.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm1.attr,
	&sensor_dev_attr_fan1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_fan1_fault.dev_attr.attr,
	&sensor_dev_attr_gpio1_alarm.dev_attr.attr,
	&sensor_dev_attr_gpio2_alarm.dev_attr.attr,
	NULL
};

static const struct attribute_group max6650_group = {
	.attrs = max6650_attrs,
	.is_visible = max6650_attrs_visible,
};

static struct attribute *max6651_attrs[] = {
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	NULL
};

static const struct attribute_group max6651_group = {
	.attrs = max6651_attrs,
};

/*
 * Real code
 */

static int max6650_init_client(struct max6650_data *data,
			       struct i2c_client *client)
{
	struct device *dev = &client->dev;
	int reg;
	int err;
	u32 voltage;
	u32 prescale;
	u32 target_rpm;

	if (of_property_read_u32(dev->of_node, "maxim,fan-microvolt",
				 &voltage))
		voltage = fan_voltage;
	else
		voltage /= 1000000; /* Microvolts to volts */
	if (of_property_read_u32(dev->of_node, "maxim,fan-prescale",
				 &prescale))
		prescale = prescaler;

	reg = i2c_smbus_read_byte_data(client, MAX6650_REG_CONFIG);
	if (reg < 0) {
		dev_err(dev, "Error reading config register, aborting.\n");
		return reg;
	}

	switch (voltage) {
	case 0:
		break;
	case 5:
		reg &= ~MAX6650_CFG_V12;
		break;
	case 12:
		reg |= MAX6650_CFG_V12;
		break;
	default:
		dev_err(dev, "illegal value for fan_voltage (%d)\n", voltage);
	}

	switch (prescale) {
	case 0:
		break;
	case 1:
		reg &= ~MAX6650_CFG_PRESCALER_MASK;
		break;
	case 2:
		reg = (reg & ~MAX6650_CFG_PRESCALER_MASK)
			 | MAX6650_CFG_PRESCALER_2;
		break;
	case  4:
		reg = (reg & ~MAX6650_CFG_PRESCALER_MASK)
			 | MAX6650_CFG_PRESCALER_4;
		break;
	case  8:
		reg = (reg & ~MAX6650_CFG_PRESCALER_MASK)
			 | MAX6650_CFG_PRESCALER_8;
		break;
	case 16:
		reg = (reg & ~MAX6650_CFG_PRESCALER_MASK)
			 | MAX6650_CFG_PRESCALER_16;
		break;
	default:
		dev_err(dev, "illegal value for prescaler (%d)\n", prescale);
	}

	dev_info(dev, "Fan voltage: %dV, prescaler: %d.\n",
		 (reg & MAX6650_CFG_V12) ? 12 : 5,
		 1 << (reg & MAX6650_CFG_PRESCALER_MASK));

	err = i2c_smbus_write_byte_data(client, MAX6650_REG_CONFIG, reg);
	if (err) {
		dev_err(dev, "Config write error, aborting.\n");
		return err;
	}

	data->config = reg;
	reg = i2c_smbus_read_byte_data(client, MAX6650_REG_COUNT);
	if (reg < 0) {
		dev_err(dev, "Failed to read count register, aborting.\n");
		return reg;
	}
	data->count = reg;

	if (!of_property_read_u32(client->dev.of_node, "maxim,fan-target-rpm",
				  &target_rpm)) {
		max6650_set_target(data, target_rpm);
		max6650_set_operating_mode(data, MAX6650_CFG_MODE_CLOSED_LOOP);
	}

	return 0;
}

#if IS_ENABLED(CONFIG_THERMAL)

static int max6650_get_max_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	*state = 255;

	return 0;
}

static int max6650_get_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long *state)
{
	struct max6650_data *data = cdev->devdata;

	*state = data->cooling_dev_state;

	return 0;
}

static int max6650_set_cur_state(struct thermal_cooling_device *cdev,
				 unsigned long state)
{
	struct max6650_data *data = cdev->devdata;
	struct i2c_client *client = data->client;
	int err;

	state = clamp_val(state, 0, 255);

	mutex_lock(&data->update_lock);

	data->dac = pwm_to_dac(state, data->config & MAX6650_CFG_V12);
	err = i2c_smbus_write_byte_data(client, MAX6650_REG_DAC, data->dac);

	if (!err) {
		max6650_set_operating_mode(data, state ?
						   MAX6650_CFG_MODE_OPEN_LOOP :
						   MAX6650_CFG_MODE_OFF);
		data->cooling_dev_state = state;
	}

	mutex_unlock(&data->update_lock);

	return err;
}

static const struct thermal_cooling_device_ops max6650_cooling_ops = {
	.get_max_state = max6650_get_max_state,
	.get_cur_state = max6650_get_cur_state,
	.set_cur_state = max6650_set_cur_state,
};
#endif

static int max6650_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct thermal_cooling_device *cooling_dev;
	struct device *dev = &client->dev;
	const struct of_device_id *of_id =
		of_match_device(of_match_ptr(max6650_dt_match), dev);
	struct max6650_data *data;
	struct device *hwmon_dev;
	int err;

	data = devm_kzalloc(dev, sizeof(struct max6650_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);
	data->nr_fans = of_id ? (int)(uintptr_t)of_id->data : id->driver_data;

	/*
	 * Initialize the max6650 chip
	 */
	err = max6650_init_client(data, client);
	if (err)
		return err;

	data->groups[0] = &max6650_group;
	/* 3 additional fan inputs for the MAX6651 */
	if (data->nr_fans == 4)
		data->groups[1] = &max6651_group;

	hwmon_dev = devm_hwmon_device_register_with_groups(dev,
							   client->name, data,
							   data->groups);
	err = PTR_ERR_OR_ZERO(hwmon_dev);
	if (err)
		return err;

#if IS_ENABLED(CONFIG_THERMAL)
	cooling_dev = devm_thermal_of_cooling_device_register(dev, dev->of_node,
				client->name, data, &max6650_cooling_ops);
	if (IS_ERR(cooling_dev)) {
		dev_warn(dev, "thermal cooling device register failed: %ld\n",
			 PTR_ERR(cooling_dev));
	}
#endif
	return 0;
}

static const struct i2c_device_id max6650_id[] = {
	{ "max6650", 1 },
	{ "max6651", 4 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, max6650_id);

static struct i2c_driver max6650_driver = {
	.driver = {
		.name	= "max6650",
		.of_match_table = of_match_ptr(max6650_dt_match),
	},
	.probe		= max6650_probe,
	.id_table	= max6650_id,
};

module_i2c_driver(max6650_driver);

MODULE_AUTHOR("Hans J. Koch");
MODULE_DESCRIPTION("MAX6650 sensor driver");
MODULE_LICENSE("GPL");
