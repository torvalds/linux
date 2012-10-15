/*
 * Driver for Linear Technology LTC4245 I2C Multiple Supply Hot Swap Controller
 *
 * Copyright (C) 2008 Ira W. Snyder <iws@ovro.caltech.edu>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This driver is based on the ds1621 and ina209 drivers.
 *
 * Datasheet:
 * http://www.linear.com/pc/downloadDocument.do?navId=H0,C1,C1003,C1006,C1140,P19392,D13517
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/i2c/ltc4245.h>

/* Here are names of the chip's registers (a.k.a. commands) */
enum ltc4245_cmd {
	LTC4245_STATUS			= 0x00, /* readonly */
	LTC4245_ALERT			= 0x01,
	LTC4245_CONTROL			= 0x02,
	LTC4245_ON			= 0x03,
	LTC4245_FAULT1			= 0x04,
	LTC4245_FAULT2			= 0x05,
	LTC4245_GPIO			= 0x06,
	LTC4245_ADCADR			= 0x07,

	LTC4245_12VIN			= 0x10,
	LTC4245_12VSENSE		= 0x11,
	LTC4245_12VOUT			= 0x12,
	LTC4245_5VIN			= 0x13,
	LTC4245_5VSENSE			= 0x14,
	LTC4245_5VOUT			= 0x15,
	LTC4245_3VIN			= 0x16,
	LTC4245_3VSENSE			= 0x17,
	LTC4245_3VOUT			= 0x18,
	LTC4245_VEEIN			= 0x19,
	LTC4245_VEESENSE		= 0x1a,
	LTC4245_VEEOUT			= 0x1b,
	LTC4245_GPIOADC			= 0x1c,
};

struct ltc4245_data {
	struct device *hwmon_dev;

	struct mutex update_lock;
	bool valid;
	unsigned long last_updated; /* in jiffies */

	/* Control registers */
	u8 cregs[0x08];

	/* Voltage registers */
	u8 vregs[0x0d];

	/* GPIO ADC registers */
	bool use_extra_gpios;
	int gpios[3];
};

/*
 * Update the readings from the GPIO pins. If the driver has been configured to
 * sample all GPIO's as analog voltages, a round-robin sampling method is used.
 * Otherwise, only the configured GPIO pin is sampled.
 *
 * LOCKING: must hold data->update_lock
 */
static void ltc4245_update_gpios(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltc4245_data *data = i2c_get_clientdata(client);
	u8 gpio_curr, gpio_next, gpio_reg;
	int i;

	/* no extra gpio support, we're basically done */
	if (!data->use_extra_gpios) {
		data->gpios[0] = data->vregs[LTC4245_GPIOADC - 0x10];
		return;
	}

	/*
	 * If the last reading was too long ago, then we mark all old GPIO
	 * readings as stale by setting them to -EAGAIN
	 */
	if (time_after(jiffies, data->last_updated + 5 * HZ)) {
		dev_dbg(&client->dev, "Marking GPIOs invalid\n");
		for (i = 0; i < ARRAY_SIZE(data->gpios); i++)
			data->gpios[i] = -EAGAIN;
	}

	/*
	 * Get the current GPIO pin
	 *
	 * The datasheet calls these GPIO[1-3], but we'll calculate the zero
	 * based array index instead, and call them GPIO[0-2]. This is much
	 * easier to think about.
	 */
	gpio_curr = (data->cregs[LTC4245_GPIO] & 0xc0) >> 6;
	if (gpio_curr > 0)
		gpio_curr -= 1;

	/* Read the GPIO voltage from the GPIOADC register */
	data->gpios[gpio_curr] = data->vregs[LTC4245_GPIOADC - 0x10];

	/* Find the next GPIO pin to read */
	gpio_next = (gpio_curr + 1) % ARRAY_SIZE(data->gpios);

	/*
	 * Calculate the correct setting for the GPIO register so it will
	 * sample the next GPIO pin
	 */
	gpio_reg = (data->cregs[LTC4245_GPIO] & 0x3f) | ((gpio_next + 1) << 6);

	/* Update the GPIO register */
	i2c_smbus_write_byte_data(client, LTC4245_GPIO, gpio_reg);

	/* Update saved data */
	data->cregs[LTC4245_GPIO] = gpio_reg;
}

static struct ltc4245_data *ltc4245_update_device(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct ltc4245_data *data = i2c_get_clientdata(client);
	s32 val;
	int i;

	mutex_lock(&data->update_lock);

	if (time_after(jiffies, data->last_updated + HZ) || !data->valid) {

		dev_dbg(&client->dev, "Starting ltc4245 update\n");

		/* Read control registers -- 0x00 to 0x07 */
		for (i = 0; i < ARRAY_SIZE(data->cregs); i++) {
			val = i2c_smbus_read_byte_data(client, i);
			if (unlikely(val < 0))
				data->cregs[i] = 0;
			else
				data->cregs[i] = val;
		}

		/* Read voltage registers -- 0x10 to 0x1c */
		for (i = 0; i < ARRAY_SIZE(data->vregs); i++) {
			val = i2c_smbus_read_byte_data(client, i+0x10);
			if (unlikely(val < 0))
				data->vregs[i] = 0;
			else
				data->vregs[i] = val;
		}

		/* Update GPIO readings */
		ltc4245_update_gpios(dev);

		data->last_updated = jiffies;
		data->valid = 1;
	}

	mutex_unlock(&data->update_lock);

	return data;
}

/* Return the voltage from the given register in millivolts */
static int ltc4245_get_voltage(struct device *dev, u8 reg)
{
	struct ltc4245_data *data = ltc4245_update_device(dev);
	const u8 regval = data->vregs[reg - 0x10];
	u32 voltage = 0;

	switch (reg) {
	case LTC4245_12VIN:
	case LTC4245_12VOUT:
		voltage = regval * 55;
		break;
	case LTC4245_5VIN:
	case LTC4245_5VOUT:
		voltage = regval * 22;
		break;
	case LTC4245_3VIN:
	case LTC4245_3VOUT:
		voltage = regval * 15;
		break;
	case LTC4245_VEEIN:
	case LTC4245_VEEOUT:
		voltage = regval * -55;
		break;
	case LTC4245_GPIOADC:
		voltage = regval * 10;
		break;
	default:
		/* If we get here, the developer messed up */
		WARN_ON_ONCE(1);
		break;
	}

	return voltage;
}

/* Return the current in the given sense register in milliAmperes */
static unsigned int ltc4245_get_current(struct device *dev, u8 reg)
{
	struct ltc4245_data *data = ltc4245_update_device(dev);
	const u8 regval = data->vregs[reg - 0x10];
	unsigned int voltage;
	unsigned int curr;

	/*
	 * The strange looking conversions that follow are fixed-point
	 * math, since we cannot do floating point in the kernel.
	 *
	 * Step 1: convert sense register to microVolts
	 * Step 2: convert voltage to milliAmperes
	 *
	 * If you play around with the V=IR equation, you come up with
	 * the following: X uV / Y mOhm == Z mA
	 *
	 * With the resistors that are fractions of a milliOhm, we multiply
	 * the voltage and resistance by 10, to shift the decimal point.
	 * Now we can use the normal division operator again.
	 */

	switch (reg) {
	case LTC4245_12VSENSE:
		voltage = regval * 250; /* voltage in uV */
		curr = voltage / 50; /* sense resistor 50 mOhm */
		break;
	case LTC4245_5VSENSE:
		voltage = regval * 125; /* voltage in uV */
		curr = (voltage * 10) / 35; /* sense resistor 3.5 mOhm */
		break;
	case LTC4245_3VSENSE:
		voltage = regval * 125; /* voltage in uV */
		curr = (voltage * 10) / 25; /* sense resistor 2.5 mOhm */
		break;
	case LTC4245_VEESENSE:
		voltage = regval * 250; /* voltage in uV */
		curr = voltage / 100; /* sense resistor 100 mOhm */
		break;
	default:
		/* If we get here, the developer messed up */
		WARN_ON_ONCE(1);
		curr = 0;
		break;
	}

	return curr;
}

static ssize_t ltc4245_show_voltage(struct device *dev,
				    struct device_attribute *da,
				    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	const int voltage = ltc4245_get_voltage(dev, attr->index);

	return snprintf(buf, PAGE_SIZE, "%d\n", voltage);
}

static ssize_t ltc4245_show_current(struct device *dev,
				    struct device_attribute *da,
				    char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	const unsigned int curr = ltc4245_get_current(dev, attr->index);

	return snprintf(buf, PAGE_SIZE, "%u\n", curr);
}

static ssize_t ltc4245_show_power(struct device *dev,
				  struct device_attribute *da,
				  char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	const unsigned int curr = ltc4245_get_current(dev, attr->index);
	const int output_voltage = ltc4245_get_voltage(dev, attr->index+1);

	/* current in mA * voltage in mV == power in uW */
	const unsigned int power = abs(output_voltage * curr);

	return snprintf(buf, PAGE_SIZE, "%u\n", power);
}

static ssize_t ltc4245_show_alarm(struct device *dev,
					  struct device_attribute *da,
					  char *buf)
{
	struct sensor_device_attribute_2 *attr = to_sensor_dev_attr_2(da);
	struct ltc4245_data *data = ltc4245_update_device(dev);
	const u8 reg = data->cregs[attr->index];
	const u32 mask = attr->nr;

	return snprintf(buf, PAGE_SIZE, "%u\n", (reg & mask) ? 1 : 0);
}

static ssize_t ltc4245_show_gpio(struct device *dev,
				 struct device_attribute *da,
				 char *buf)
{
	struct sensor_device_attribute *attr = to_sensor_dev_attr(da);
	struct ltc4245_data *data = ltc4245_update_device(dev);
	int val = data->gpios[attr->index];

	/* handle stale GPIO's */
	if (val < 0)
		return val;

	/* Convert to millivolts and print */
	return snprintf(buf, PAGE_SIZE, "%u\n", val * 10);
}

/*
 * These macros are used below in constructing device attribute objects
 * for use with sysfs_create_group() to make a sysfs device file
 * for each register.
 */

#define LTC4245_VOLTAGE(name, ltc4245_cmd_idx) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, \
	ltc4245_show_voltage, NULL, ltc4245_cmd_idx)

#define LTC4245_CURRENT(name, ltc4245_cmd_idx) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, \
	ltc4245_show_current, NULL, ltc4245_cmd_idx)

#define LTC4245_POWER(name, ltc4245_cmd_idx) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, \
	ltc4245_show_power, NULL, ltc4245_cmd_idx)

#define LTC4245_ALARM(name, mask, reg) \
	static SENSOR_DEVICE_ATTR_2(name, S_IRUGO, \
	ltc4245_show_alarm, NULL, (mask), reg)

#define LTC4245_GPIO_VOLTAGE(name, gpio_num) \
	static SENSOR_DEVICE_ATTR(name, S_IRUGO, \
	ltc4245_show_gpio, NULL, gpio_num)

/* Construct a sensor_device_attribute structure for each register */

/* Input voltages */
LTC4245_VOLTAGE(in1_input,			LTC4245_12VIN);
LTC4245_VOLTAGE(in2_input,			LTC4245_5VIN);
LTC4245_VOLTAGE(in3_input,			LTC4245_3VIN);
LTC4245_VOLTAGE(in4_input,			LTC4245_VEEIN);

/* Input undervoltage alarms */
LTC4245_ALARM(in1_min_alarm,	(1 << 0),	LTC4245_FAULT1);
LTC4245_ALARM(in2_min_alarm,	(1 << 1),	LTC4245_FAULT1);
LTC4245_ALARM(in3_min_alarm,	(1 << 2),	LTC4245_FAULT1);
LTC4245_ALARM(in4_min_alarm,	(1 << 3),	LTC4245_FAULT1);

/* Currents (via sense resistor) */
LTC4245_CURRENT(curr1_input,			LTC4245_12VSENSE);
LTC4245_CURRENT(curr2_input,			LTC4245_5VSENSE);
LTC4245_CURRENT(curr3_input,			LTC4245_3VSENSE);
LTC4245_CURRENT(curr4_input,			LTC4245_VEESENSE);

/* Overcurrent alarms */
LTC4245_ALARM(curr1_max_alarm,	(1 << 4),	LTC4245_FAULT1);
LTC4245_ALARM(curr2_max_alarm,	(1 << 5),	LTC4245_FAULT1);
LTC4245_ALARM(curr3_max_alarm,	(1 << 6),	LTC4245_FAULT1);
LTC4245_ALARM(curr4_max_alarm,	(1 << 7),	LTC4245_FAULT1);

/* Output voltages */
LTC4245_VOLTAGE(in5_input,			LTC4245_12VOUT);
LTC4245_VOLTAGE(in6_input,			LTC4245_5VOUT);
LTC4245_VOLTAGE(in7_input,			LTC4245_3VOUT);
LTC4245_VOLTAGE(in8_input,			LTC4245_VEEOUT);

/* Power Bad alarms */
LTC4245_ALARM(in5_min_alarm,	(1 << 0),	LTC4245_FAULT2);
LTC4245_ALARM(in6_min_alarm,	(1 << 1),	LTC4245_FAULT2);
LTC4245_ALARM(in7_min_alarm,	(1 << 2),	LTC4245_FAULT2);
LTC4245_ALARM(in8_min_alarm,	(1 << 3),	LTC4245_FAULT2);

/* GPIO voltages */
LTC4245_GPIO_VOLTAGE(in9_input,			0);
LTC4245_GPIO_VOLTAGE(in10_input,		1);
LTC4245_GPIO_VOLTAGE(in11_input,		2);

/* Power Consumption (virtual) */
LTC4245_POWER(power1_input,			LTC4245_12VSENSE);
LTC4245_POWER(power2_input,			LTC4245_5VSENSE);
LTC4245_POWER(power3_input,			LTC4245_3VSENSE);
LTC4245_POWER(power4_input,			LTC4245_VEESENSE);

/*
 * Finally, construct an array of pointers to members of the above objects,
 * as required for sysfs_create_group()
 */
static struct attribute *ltc4245_std_attributes[] = {
	&sensor_dev_attr_in1_input.dev_attr.attr,
	&sensor_dev_attr_in2_input.dev_attr.attr,
	&sensor_dev_attr_in3_input.dev_attr.attr,
	&sensor_dev_attr_in4_input.dev_attr.attr,

	&sensor_dev_attr_in1_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in2_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in3_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in4_min_alarm.dev_attr.attr,

	&sensor_dev_attr_curr1_input.dev_attr.attr,
	&sensor_dev_attr_curr2_input.dev_attr.attr,
	&sensor_dev_attr_curr3_input.dev_attr.attr,
	&sensor_dev_attr_curr4_input.dev_attr.attr,

	&sensor_dev_attr_curr1_max_alarm.dev_attr.attr,
	&sensor_dev_attr_curr2_max_alarm.dev_attr.attr,
	&sensor_dev_attr_curr3_max_alarm.dev_attr.attr,
	&sensor_dev_attr_curr4_max_alarm.dev_attr.attr,

	&sensor_dev_attr_in5_input.dev_attr.attr,
	&sensor_dev_attr_in6_input.dev_attr.attr,
	&sensor_dev_attr_in7_input.dev_attr.attr,
	&sensor_dev_attr_in8_input.dev_attr.attr,

	&sensor_dev_attr_in5_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in6_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in7_min_alarm.dev_attr.attr,
	&sensor_dev_attr_in8_min_alarm.dev_attr.attr,

	&sensor_dev_attr_in9_input.dev_attr.attr,

	&sensor_dev_attr_power1_input.dev_attr.attr,
	&sensor_dev_attr_power2_input.dev_attr.attr,
	&sensor_dev_attr_power3_input.dev_attr.attr,
	&sensor_dev_attr_power4_input.dev_attr.attr,

	NULL,
};

static struct attribute *ltc4245_gpio_attributes[] = {
	&sensor_dev_attr_in10_input.dev_attr.attr,
	&sensor_dev_attr_in11_input.dev_attr.attr,
	NULL,
};

static const struct attribute_group ltc4245_std_group = {
	.attrs = ltc4245_std_attributes,
};

static const struct attribute_group ltc4245_gpio_group = {
	.attrs = ltc4245_gpio_attributes,
};

static int ltc4245_sysfs_create_groups(struct i2c_client *client)
{
	struct ltc4245_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;
	int ret;

	/* register the standard sysfs attributes */
	ret = sysfs_create_group(&dev->kobj, &ltc4245_std_group);
	if (ret) {
		dev_err(dev, "unable to register standard attributes\n");
		return ret;
	}

	/* if we're using the extra gpio support, register it's attributes */
	if (data->use_extra_gpios) {
		ret = sysfs_create_group(&dev->kobj, &ltc4245_gpio_group);
		if (ret) {
			dev_err(dev, "unable to register gpio attributes\n");
			sysfs_remove_group(&dev->kobj, &ltc4245_std_group);
			return ret;
		}
	}

	return 0;
}

static void ltc4245_sysfs_remove_groups(struct i2c_client *client)
{
	struct ltc4245_data *data = i2c_get_clientdata(client);
	struct device *dev = &client->dev;

	if (data->use_extra_gpios)
		sysfs_remove_group(&dev->kobj, &ltc4245_gpio_group);

	sysfs_remove_group(&dev->kobj, &ltc4245_std_group);
}

static bool ltc4245_use_extra_gpios(struct i2c_client *client)
{
	struct ltc4245_platform_data *pdata = dev_get_platdata(&client->dev);
#ifdef CONFIG_OF
	struct device_node *np = client->dev.of_node;
#endif

	/* prefer platform data */
	if (pdata)
		return pdata->use_extra_gpios;

#ifdef CONFIG_OF
	/* fallback on OF */
	if (of_find_property(np, "ltc4245,use-extra-gpios", NULL))
		return true;
#endif

	return false;
}

static int ltc4245_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = client->adapter;
	struct ltc4245_data *data;
	int ret;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	data = devm_kzalloc(&client->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	mutex_init(&data->update_lock);
	data->use_extra_gpios = ltc4245_use_extra_gpios(client);

	/* Initialize the LTC4245 chip */
	i2c_smbus_write_byte_data(client, LTC4245_FAULT1, 0x00);
	i2c_smbus_write_byte_data(client, LTC4245_FAULT2, 0x00);

	/* Register sysfs hooks */
	ret = ltc4245_sysfs_create_groups(client);
	if (ret)
		return ret;

	data->hwmon_dev = hwmon_device_register(&client->dev);
	if (IS_ERR(data->hwmon_dev)) {
		ret = PTR_ERR(data->hwmon_dev);
		goto out_hwmon_device_register;
	}

	return 0;

out_hwmon_device_register:
	ltc4245_sysfs_remove_groups(client);
	return ret;
}

static int ltc4245_remove(struct i2c_client *client)
{
	struct ltc4245_data *data = i2c_get_clientdata(client);

	hwmon_device_unregister(data->hwmon_dev);
	ltc4245_sysfs_remove_groups(client);

	return 0;
}

static const struct i2c_device_id ltc4245_id[] = {
	{ "ltc4245", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ltc4245_id);

/* This is the driver that will be inserted */
static struct i2c_driver ltc4245_driver = {
	.driver = {
		.name	= "ltc4245",
	},
	.probe		= ltc4245_probe,
	.remove		= ltc4245_remove,
	.id_table	= ltc4245_id,
};

module_i2c_driver(ltc4245_driver);

MODULE_AUTHOR("Ira W. Snyder <iws@ovro.caltech.edu>");
MODULE_DESCRIPTION("LTC4245 driver");
MODULE_LICENSE("GPL");
