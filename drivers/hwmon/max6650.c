/*
 * max6650.c - Part of lm_sensors, Linux kernel modules for hardware
 *             monitoring.
 *
 * (C) 2007 by Hans J. Koch <hjk@linutronix.de>
 *
 * based on code written by John Morris <john.morris@spirentcom.com>
 * Copyright (c) 2003 Spirent Communications
 * and Claus Gindhart <claus.gindhart@kontron.com>
 *
 * This module has only been tested with the MAX6650 chip. It should
 * also work with the MAX6651. It does not distinguish max6650 and max6651
 * chips.
 *
 * Tha datasheet was last seen at:
 *
 *        http://pdfserv.maxim-ic.com/en/ds/MAX6650-MAX6651.pdf
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
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/err.h>

/*
 * Addresses to scan. There are four disjoint possibilities, by pin config.
 */

static unsigned short normal_i2c[] = {0x1b, 0x1f, 0x48, 0x4b, I2C_CLIENT_END};

/*
 * Insmod parameters
 */

/* fan_voltage: 5=5V fan, 12=12V fan, 0=don't change */
static int fan_voltage;
/* prescaler: Possible values are 1, 2, 4, 8, 16 or 0 for don't change */
static int prescaler;
/* clock: The clock frequency of the chip the driver should assume */
static int clock = 254000;

module_param(fan_voltage, int, S_IRUGO);
module_param(prescaler, int, S_IRUGO);
module_param(clock, int, S_IRUGO);

I2C_CLIENT_INSMOD_1(max6650);

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

/* Minimum and maximum values of the FAN-RPM */
#define FAN_RPM_MIN 240
#define FAN_RPM_MAX 30000

#define DIV_FROM_REG(reg) (1 << (reg & 7))

static int max6650_attach_adapter(struct i2c_adapter *adapter);
static int max6650_detect(struct i2c_adapter *adapter, int address, int kind);
static int max6650_init_client(struct i2c_client *client);
static int max6650_detach_client(struct i2c_client *client);
static struct max6650_data *max6650_update_device(struct device *dev);

/*
 * Driver data (common to all clients)
 */

static struct i2c_driver max6650_driver = {
	.driver = {
		.name	= "max6650",
	},
	.attach_adapter	= max6650_attach_adapter,
	.detach_client	= max6650_detach_client,
};

/*
 * Client data (each client gets its own)
 */

struct max6650_data
{
	struct i2c_client client;
	struct class_device *class_dev;
	struct mutex update_lock;
	char valid; /* zero until following fields are valid */
	unsigned long last_updated; /* in jiffies */

	/* register values */
	u8 speed;
	u8 config;
	u8 tach[4];
	u8 count;
	u8 dac;
};

static ssize_t get_fan(struct device *dev, struct device_attribute *devattr,
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

static ssize_t get_target(struct device *dev, struct device_attribute *devattr,
			 char *buf)
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

static ssize_t set_target(struct device *dev, struct device_attribute *devattr,
			 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6650_data *data = i2c_get_clientdata(client);
	int rpm = simple_strtoul(buf, NULL, 10);
	int kscale, ktach;

	rpm = SENSORS_LIMIT(rpm, FAN_RPM_MIN, FAN_RPM_MAX);

	/*
	* Divide the required speed by 60 to get from rpm to rps, then
	* use the datasheet equation:
	*
	*     KTACH = [(fCLK x KSCALE) / (256 x FanSpeed)] - 1
	*/

	mutex_lock(&data->update_lock);

	kscale = DIV_FROM_REG(data->config);
	ktach = ((clock * kscale) / (256 * rpm / 60)) - 1;
	if (ktach < 0)
		ktach = 0;
	if (ktach > 255)
		ktach = 255;
	data->speed = ktach;

	i2c_smbus_write_byte_data(client, MAX6650_REG_SPEED, data->speed);

	mutex_unlock(&data->update_lock);

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

static ssize_t get_pwm(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	int pwm;
	struct max6650_data *data = max6650_update_device(dev);

	/* Useful range for dac is 0-180 for 12V fans and 0-76 for 5V fans.
	   Lower DAC values mean higher speeds. */
	if (data->config & MAX6650_CFG_V12)
		pwm = 255 - (255 * (int)data->dac)/180;
	else
		pwm = 255 - (255 * (int)data->dac)/76;

	if (pwm < 0)
		pwm = 0;

	return sprintf(buf, "%d\n", pwm);
}

static ssize_t set_pwm(struct device *dev, struct device_attribute *devattr,
			const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6650_data *data = i2c_get_clientdata(client);
	int pwm = simple_strtoul(buf, NULL, 10);

	pwm = SENSORS_LIMIT(pwm, 0, 255);

	mutex_lock(&data->update_lock);

	if (data->config & MAX6650_CFG_V12)
		data->dac = 180 - (180 * pwm)/255;
	else
		data->dac = 76 - (76 * pwm)/255;

	i2c_smbus_write_byte_data(client, MAX6650_REG_DAC, data->dac);

	mutex_unlock(&data->update_lock);

	return count;
}

/*
 * Get/Set controller mode:
 * Possible values:
 * 0 = Fan always on
 * 1 = Open loop, Voltage is set according to speed, not regulated.
 * 2 = Closed loop, RPM for all fans regulated by fan1 tachometer
 */

static ssize_t get_enable(struct device *dev, struct device_attribute *devattr,
			  char *buf)
{
	struct max6650_data *data = max6650_update_device(dev);
	int mode = (data->config & MAX6650_CFG_MODE_MASK) >> 4;
	int sysfs_modes[4] = {0, 1, 2, 1};

	return sprintf(buf, "%d\n", sysfs_modes[mode]);
}

static ssize_t set_enable(struct device *dev, struct device_attribute *devattr,
			  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6650_data *data = i2c_get_clientdata(client);
	int mode = simple_strtoul(buf, NULL, 10);
	int max6650_modes[3] = {0, 3, 2};

	if ((mode < 0)||(mode > 2)) {
		dev_err(&client->dev,
			"illegal value for pwm1_enable (%d)\n", mode);
		return -EINVAL;
	}

	mutex_lock(&data->update_lock);

	data->config = i2c_smbus_read_byte_data(client, MAX6650_REG_CONFIG);
	data->config = (data->config & ~MAX6650_CFG_MODE_MASK)
		       | (max6650_modes[mode] << 4);

	i2c_smbus_write_byte_data(client, MAX6650_REG_CONFIG, data->config);

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

static ssize_t get_div(struct device *dev, struct device_attribute *devattr,
		       char *buf)
{
	struct max6650_data *data = max6650_update_device(dev);

	return sprintf(buf, "%d\n", DIV_FROM_REG(data->count));
}

static ssize_t set_div(struct device *dev, struct device_attribute *devattr,
		       const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct max6650_data *data = i2c_get_clientdata(client);
	int div = simple_strtoul(buf, NULL, 10);

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
		dev_err(&client->dev,
			"illegal value for fan divider (%d)\n", div);
		return -EINVAL;
	}

	i2c_smbus_write_byte_data(client, MAX6650_REG_COUNT, data->count);
	mutex_unlock(&data->update_lock);

	return count;
}

static SENSOR_DEVICE_ATTR(fan1_input, S_IRUGO, get_fan, NULL, 0);
static SENSOR_DEVICE_ATTR(fan2_input, S_IRUGO, get_fan, NULL, 1);
static SENSOR_DEVICE_ATTR(fan3_input, S_IRUGO, get_fan, NULL, 2);
static SENSOR_DEVICE_ATTR(fan4_input, S_IRUGO, get_fan, NULL, 3);
static DEVICE_ATTR(fan1_target, S_IWUSR | S_IRUGO, get_target, set_target);
static DEVICE_ATTR(fan1_div, S_IWUSR | S_IRUGO, get_div, set_div);
static DEVICE_ATTR(pwm1_enable, S_IWUSR | S_IRUGO, get_enable, set_enable);
static DEVICE_ATTR(pwm1, S_IWUSR | S_IRUGO, get_pwm, set_pwm);


static struct attribute *max6650_attrs[] = {
	&sensor_dev_attr_fan1_input.dev_attr.attr,
	&sensor_dev_attr_fan2_input.dev_attr.attr,
	&sensor_dev_attr_fan3_input.dev_attr.attr,
	&sensor_dev_attr_fan4_input.dev_attr.attr,
	&dev_attr_fan1_target.attr,
	&dev_attr_fan1_div.attr,
	&dev_attr_pwm1_enable.attr,
	&dev_attr_pwm1.attr,
	NULL
};

static struct attribute_group max6650_attr_grp = {
	.attrs = max6650_attrs,
};

/*
 * Real code
 */

static int max6650_attach_adapter(struct i2c_adapter *adapter)
{
	if (!(adapter->class & I2C_CLASS_HWMON)) {
		dev_dbg(&adapter->dev,
			"FATAL: max6650_attach_adapter class HWMON not set\n");
		return 0;
	}

	return i2c_probe(adapter, &addr_data, max6650_detect);
}

/*
 * The following function does more than just detection. If detection
 * succeeds, it also registers the new chip.
 */

static int max6650_detect(struct i2c_adapter *adapter, int address, int kind)
{
	struct i2c_client *client;
	struct max6650_data *data;
	int err = -ENODEV;

	dev_dbg(&adapter->dev, "max6650_detect called, kind = %d\n", kind);

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_dbg(&adapter->dev, "max6650: I2C bus doesn't support "
					"byte read mode, skipping.\n");
		return 0;
	}

	if (!(data = kzalloc(sizeof(struct max6650_data), GFP_KERNEL))) {
		dev_err(&adapter->dev, "max6650: out of memory.\n");
		return -ENOMEM;
	}

	client = &data->client;
	i2c_set_clientdata(client, data);
	client->addr = address;
	client->adapter = adapter;
	client->driver = &max6650_driver;

	/*
	 * Now we do the remaining detection. A negative kind means that
	 * the driver was loaded with no force parameter (default), so we
	 * must both detect and identify the chip (actually there is only
	 * one possible kind of chip for now, max6650). A zero kind means that
	 * the driver was loaded with the force parameter, the detection
	 * step shall be skipped. A positive kind means that the driver
	 * was loaded with the force parameter and a given kind of chip is
	 * requested, so both the detection and the identification steps
	 * are skipped.
	 *
	 * Currently I can find no way to distinguish between a MAX6650 and
	 * a MAX6651. This driver has only been tried on the former.
	 */

	if ((kind < 0) &&
	   (  (i2c_smbus_read_byte_data(client, MAX6650_REG_CONFIG) & 0xC0)
	    ||(i2c_smbus_read_byte_data(client, MAX6650_REG_GPIO_STAT) & 0xE0)
	    ||(i2c_smbus_read_byte_data(client, MAX6650_REG_ALARM_EN) & 0xE0)
	    ||(i2c_smbus_read_byte_data(client, MAX6650_REG_ALARM) & 0xE0)
	    ||(i2c_smbus_read_byte_data(client, MAX6650_REG_COUNT) & 0xFC))) {
		dev_dbg(&adapter->dev,
			"max6650: detection failed at 0x%02x.\n", address);
		goto err_free;
	}

	dev_info(&adapter->dev, "max6650: chip found at 0x%02x.\n", address);

	strlcpy(client->name, "max6650", I2C_NAME_SIZE);
	mutex_init(&data->update_lock);

	if ((err = i2c_attach_client(client))) {
		dev_err(&adapter->dev, "max6650: failed to attach client.\n");
		goto err_free;
	}

	/*
	 * Initialize the max6650 chip
	 */
	if (max6650_init_client(client))
		goto err_detach;

	err = sysfs_create_group(&client->dev.kobj, &max6650_attr_grp);
	if (err)
		goto err_detach;

	data->class_dev = hwmon_device_register(&client->dev);
	if (!IS_ERR(data->class_dev))
		return 0;

	err = PTR_ERR(data->class_dev);
	dev_err(&client->dev, "error registering hwmon device.\n");
	sysfs_remove_group(&client->dev.kobj, &max6650_attr_grp);
err_detach:
	i2c_detach_client(client);
err_free:
	kfree(data);
	return err;
}

static int max6650_detach_client(struct i2c_client *client)
{
	struct max6650_data *data = i2c_get_clientdata(client);
	int err;

	sysfs_remove_group(&client->dev.kobj, &max6650_attr_grp);
	hwmon_device_unregister(data->class_dev);
	err = i2c_detach_client(client);
	if (!err)
		kfree(data);
	return err;
}

static int max6650_init_client(struct i2c_client *client)
{
	struct max6650_data *data = i2c_get_clientdata(client);
	int config;
	int err = -EIO;

	config = i2c_smbus_read_byte_data(client, MAX6650_REG_CONFIG);

	if (config < 0) {
		dev_err(&client->dev, "Error reading config, aborting.\n");
		return err;
	}

	switch (fan_voltage) {
		case 0:
			break;
		case 5:
			config &= ~MAX6650_CFG_V12;
			break;
		case 12:
			config |= MAX6650_CFG_V12;
			break;
		default:
			dev_err(&client->dev,
				"illegal value for fan_voltage (%d)\n",
				fan_voltage);
	}

	dev_info(&client->dev, "Fan voltage is set to %dV.\n",
		 (config & MAX6650_CFG_V12) ? 12 : 5);

	switch (prescaler) {
		case 0:
			break;
		case 1:
			config &= ~MAX6650_CFG_PRESCALER_MASK;
			break;
		case 2:
			config = (config & ~MAX6650_CFG_PRESCALER_MASK)
				 | MAX6650_CFG_PRESCALER_2;
			break;
		case  4:
			config = (config & ~MAX6650_CFG_PRESCALER_MASK)
				 | MAX6650_CFG_PRESCALER_4;
			break;
		case  8:
			config = (config & ~MAX6650_CFG_PRESCALER_MASK)
				 | MAX6650_CFG_PRESCALER_8;
			break;
		case 16:
			config = (config & ~MAX6650_CFG_PRESCALER_MASK)
				 | MAX6650_CFG_PRESCALER_16;
			break;
		default:
			dev_err(&client->dev,
				"illegal value for prescaler (%d)\n",
				prescaler);
	}

	dev_info(&client->dev, "Prescaler is set to %d.\n",
		 1 << (config & MAX6650_CFG_PRESCALER_MASK));

	/* If mode is set to "full off", we change it to "open loop" and
	 * set DAC to 255, which has the same effect. We do this because
	 * there's no "full off" mode defined in hwmon specifcations.
	 */

	if ((config & MAX6650_CFG_MODE_MASK) == MAX6650_CFG_MODE_OFF) {
		dev_dbg(&client->dev, "Change mode to open loop, full off.\n");
		config = (config & ~MAX6650_CFG_MODE_MASK)
			 | MAX6650_CFG_MODE_OPEN_LOOP;
		if (i2c_smbus_write_byte_data(client, MAX6650_REG_DAC, 255)) {
			dev_err(&client->dev, "DAC write error, aborting.\n");
			return err;
		}
	}

	if (i2c_smbus_write_byte_data(client, MAX6650_REG_CONFIG, config)) {
		dev_err(&client->dev, "Config write error, aborting.\n");
		return err;
	}

	data->config = config;
	data->count = i2c_smbus_read_byte_data(client, MAX6650_REG_COUNT);

	return 0;
}

static const u8 tach_reg[] = {
	MAX6650_REG_TACH0,
	MAX6650_REG_TACH1,
	MAX6650_REG_TACH2,
	MAX6650_REG_TACH3,
};

static struct max6650_data *max6650_update_device(struct device *dev)
{
	int i;
	struct i2c_client *client = to_i2c_client(dev);
	struct max6650_data *data = i2c_get_clientdata(client);

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {
		data->speed = i2c_smbus_read_byte_data(client,
						       MAX6650_REG_SPEED);
		data->config = i2c_smbus_read_byte_data(client,
							MAX6650_REG_CONFIG);
		for (i = 0; i < 4; i++) {
			data->tach[i] = i2c_smbus_read_byte_data(client,
								 tach_reg[i]);
		}
		data->count = i2c_smbus_read_byte_data(client,
							MAX6650_REG_COUNT);
		data->dac = i2c_smbus_read_byte_data(client, MAX6650_REG_DAC);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

static int __init sensors_max6650_init(void)
{
	return i2c_add_driver(&max6650_driver);
}

static void __exit sensors_max6650_exit(void)
{
	i2c_del_driver(&max6650_driver);
}

MODULE_AUTHOR("Hans J. Koch");
MODULE_DESCRIPTION("MAX6650 sensor driver");
MODULE_LICENSE("GPL");

module_init(sensors_max6650_init);
module_exit(sensors_max6650_exit);
