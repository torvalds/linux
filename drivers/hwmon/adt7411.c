/*
 *  Driver for the ADT7411 (I2C/SPI 8 channel 10 bit ADC & temperature-sensor)
 *
 *  Copyright (C) 2008, 2010 Pengutronix
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License version 2 as
 *  published by the Free Software Foundation.
 *
 *  TODO: SPI, use power-down mode for suspend?, interrupt handling?
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/err.h>
#include <linux/mutex.h>
#include <linux/jiffies.h>
#include <linux/i2c.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/slab.h>

#define ADT7411_REG_INT_TEMP_VDD_LSB		0x03
#define ADT7411_REG_EXT_TEMP_AIN14_LSB		0x04
#define ADT7411_REG_VDD_MSB			0x06
#define ADT7411_REG_INT_TEMP_MSB		0x07
#define ADT7411_REG_EXT_TEMP_AIN1_MSB		0x08

#define ADT7411_REG_CFG1			0x18
#define ADT7411_CFG1_START_MONITOR		(1 << 0)
#define ADT7411_CFG1_RESERVED_BIT1		(1 << 1)
#define ADT7411_CFG1_EXT_TDM			(1 << 2)
#define ADT7411_CFG1_RESERVED_BIT3		(1 << 3)

#define ADT7411_REG_CFG2			0x19
#define ADT7411_CFG2_DISABLE_AVG		(1 << 5)

#define ADT7411_REG_CFG3			0x1a
#define ADT7411_CFG3_ADC_CLK_225		(1 << 0)
#define ADT7411_CFG3_RESERVED_BIT1		(1 << 1)
#define ADT7411_CFG3_RESERVED_BIT2		(1 << 2)
#define ADT7411_CFG3_RESERVED_BIT3		(1 << 3)
#define ADT7411_CFG3_REF_VDD			(1 << 4)

#define ADT7411_REG_DEVICE_ID			0x4d
#define ADT7411_REG_MANUFACTURER_ID		0x4e

#define ADT7411_DEVICE_ID			0x2
#define ADT7411_MANUFACTURER_ID			0x41

static const unsigned short normal_i2c[] = { 0x48, 0x4a, 0x4b, I2C_CLIENT_END };

struct adt7411_data {
	struct mutex device_lock;	/* for "atomic" device accesses */
	struct mutex update_lock;
	unsigned long next_update;
	long vref_cached;
	struct i2c_client *client;
	bool use_ext_temp;
};

/*
 * When reading a register containing (up to 4) lsb, all associated
 * msb-registers get locked by the hardware. After _one_ of those msb is read,
 * _all_ are unlocked. In order to use this locking correctly, reading lsb/msb
 * is protected here with a mutex, too.
 */
static int adt7411_read_10_bit(struct i2c_client *client, u8 lsb_reg,
				u8 msb_reg, u8 lsb_shift)
{
	struct adt7411_data *data = i2c_get_clientdata(client);
	int val, tmp;

	mutex_lock(&data->device_lock);

	val = i2c_smbus_read_byte_data(client, lsb_reg);
	if (val < 0)
		goto exit_unlock;

	tmp = (val >> lsb_shift) & 3;
	val = i2c_smbus_read_byte_data(client, msb_reg);

	if (val >= 0)
		val = (val << 2) | tmp;

 exit_unlock:
	mutex_unlock(&data->device_lock);

	return val;
}

static int adt7411_modify_bit(struct i2c_client *client, u8 reg, u8 bit,
				bool flag)
{
	struct adt7411_data *data = i2c_get_clientdata(client);
	int ret, val;

	mutex_lock(&data->device_lock);

	ret = i2c_smbus_read_byte_data(client, reg);
	if (ret < 0)
		goto exit_unlock;

	if (flag)
		val = ret | bit;
	else
		val = ret & ~bit;

	ret = i2c_smbus_write_byte_data(client, reg, val);

 exit_unlock:
	mutex_unlock(&data->device_lock);
	return ret;
}

static ssize_t adt7411_show_bit(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct sensor_device_attribute_2 *attr2 = to_sensor_dev_attr_2(attr);
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret = i2c_smbus_read_byte_data(client, attr2->index);

	return ret < 0 ? ret : sprintf(buf, "%u\n", !!(ret & attr2->nr));
}

static ssize_t adt7411_set_bit(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct sensor_device_attribute_2 *s_attr2 = to_sensor_dev_attr_2(attr);
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;
	unsigned long flag;

	ret = kstrtoul(buf, 0, &flag);
	if (ret || flag > 1)
		return -EINVAL;

	ret = adt7411_modify_bit(client, s_attr2->index, s_attr2->nr, flag);

	/* force update */
	mutex_lock(&data->update_lock);
	data->next_update = jiffies;
	mutex_unlock(&data->update_lock);

	return ret < 0 ? ret : count;
}

#define ADT7411_BIT_ATTR(__name, __reg, __bit) \
	SENSOR_DEVICE_ATTR_2(__name, S_IRUGO | S_IWUSR, adt7411_show_bit, \
	adt7411_set_bit, __bit, __reg)

static ADT7411_BIT_ATTR(no_average, ADT7411_REG_CFG2, ADT7411_CFG2_DISABLE_AVG);
static ADT7411_BIT_ATTR(fast_sampling, ADT7411_REG_CFG3, ADT7411_CFG3_ADC_CLK_225);
static ADT7411_BIT_ATTR(adc_ref_vdd, ADT7411_REG_CFG3, ADT7411_CFG3_REF_VDD);

static struct attribute *adt7411_attrs[] = {
	&sensor_dev_attr_no_average.dev_attr.attr,
	&sensor_dev_attr_fast_sampling.dev_attr.attr,
	&sensor_dev_attr_adc_ref_vdd.dev_attr.attr,
	NULL
};
ATTRIBUTE_GROUPS(adt7411);

static int adt7411_read_in_vdd(struct device *dev, u32 attr, long *val)
{
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret;

	switch (attr) {
	case hwmon_in_input:
		ret = adt7411_read_10_bit(client, ADT7411_REG_INT_TEMP_VDD_LSB,
					  ADT7411_REG_VDD_MSB, 2);
		if (ret < 0)
			return ret;
		*val = ret * 7000 / 1024;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int adt7411_read_in_chan(struct device *dev, u32 attr, int channel,
				long *val)
{
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;

	int ret;
	int lsb_reg, lsb_shift;
	int nr = channel - 1;

	mutex_lock(&data->update_lock);
	if (time_after_eq(jiffies, data->next_update)) {
		ret = i2c_smbus_read_byte_data(client, ADT7411_REG_CFG3);
		if (ret < 0)
			goto exit_unlock;

		if (ret & ADT7411_CFG3_REF_VDD) {
			ret = adt7411_read_in_vdd(dev, hwmon_in_input,
						  &data->vref_cached);
			if (ret < 0)
				goto exit_unlock;
		} else {
			data->vref_cached = 2250;
		}

		data->next_update = jiffies + HZ;
	}

	switch (attr) {
	case hwmon_in_input:
		lsb_reg = ADT7411_REG_EXT_TEMP_AIN14_LSB + (nr >> 2);
		lsb_shift = 2 * (nr & 0x03);
		ret = adt7411_read_10_bit(client, lsb_reg,
					  ADT7411_REG_EXT_TEMP_AIN1_MSB + nr,
					  lsb_shift);
		if (ret < 0)
			goto exit_unlock;
		*val = ret * data->vref_cached / 1024;
		ret = 0;
		break;
	default:
		ret = -EOPNOTSUPP;
		break;
	}
 exit_unlock:
	mutex_unlock(&data->update_lock);
	return ret;
}

static int adt7411_read_in(struct device *dev, u32 attr, int channel,
			   long *val)
{
	if (channel == 0)
		return adt7411_read_in_vdd(dev, attr, val);
	else
		return adt7411_read_in_chan(dev, attr, channel, val);
}

static int adt7411_read_temp(struct device *dev, u32 attr, int channel,
			     long *val)
{
	struct adt7411_data *data = dev_get_drvdata(dev);
	struct i2c_client *client = data->client;
	int ret, regl, regh;

	switch (attr) {
	case hwmon_temp_input:
		regl = channel ? ADT7411_REG_EXT_TEMP_AIN14_LSB :
				 ADT7411_REG_INT_TEMP_VDD_LSB;
		regh = channel ? ADT7411_REG_EXT_TEMP_AIN1_MSB :
				 ADT7411_REG_INT_TEMP_MSB;
		ret = adt7411_read_10_bit(client, regl, regh, 0);
		if (ret < 0)
			return ret;
		ret = ret & 0x200 ? ret - 0x400 : ret; /* 10 bit signed */
		*val = ret * 250;
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int adt7411_read(struct device *dev, enum hwmon_sensor_types type,
			u32 attr, int channel, long *val)
{
	switch (type) {
	case hwmon_in:
		return adt7411_read_in(dev, attr, channel, val);
	case hwmon_temp:
		return adt7411_read_temp(dev, attr, channel, val);
	default:
		return -EOPNOTSUPP;
	}
}

static umode_t adt7411_is_visible(const void *_data,
				  enum hwmon_sensor_types type,
				  u32 attr, int channel)
{
	const struct adt7411_data *data = _data;

	switch (type) {
	case hwmon_in:
		if (channel > 0 && channel < 3)
			return data->use_ext_temp ? 0 : S_IRUGO;
		else
			return S_IRUGO;
	case hwmon_temp:
		if (channel == 1)
			return data->use_ext_temp ? S_IRUGO : 0;
		else
			return S_IRUGO;
	default:
		return 0;
	}
}

static int adt7411_detect(struct i2c_client *client,
			  struct i2c_board_info *info)
{
	int val;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_SMBUS_BYTE_DATA))
		return -ENODEV;

	val = i2c_smbus_read_byte_data(client, ADT7411_REG_MANUFACTURER_ID);
	if (val < 0 || val != ADT7411_MANUFACTURER_ID) {
		dev_dbg(&client->dev,
			"Wrong manufacturer ID. Got %d, expected %d\n",
			val, ADT7411_MANUFACTURER_ID);
		return -ENODEV;
	}

	val = i2c_smbus_read_byte_data(client, ADT7411_REG_DEVICE_ID);
	if (val < 0 || val != ADT7411_DEVICE_ID) {
		dev_dbg(&client->dev,
			"Wrong device ID. Got %d, expected %d\n",
			val, ADT7411_DEVICE_ID);
		return -ENODEV;
	}

	strlcpy(info->type, "adt7411", I2C_NAME_SIZE);

	return 0;
}

static int adt7411_init_device(struct adt7411_data *data)
{
	int ret;
	u8 val;

	ret = i2c_smbus_read_byte_data(data->client, ADT7411_REG_CFG3);
	if (ret < 0)
		return ret;

	/*
	 * We must only write zero to bit 1 and bit 2 and only one to bit 3
	 * according to the datasheet.
	 */
	val = ret;
	val &= ~(ADT7411_CFG3_RESERVED_BIT1 | ADT7411_CFG3_RESERVED_BIT2);
	val |= ADT7411_CFG3_RESERVED_BIT3;

	ret = i2c_smbus_write_byte_data(data->client, ADT7411_REG_CFG3, val);
	if (ret < 0)
		return ret;

	ret = i2c_smbus_read_byte_data(data->client, ADT7411_REG_CFG1);
	if (ret < 0)
		return ret;

	data->use_ext_temp = ret & ADT7411_CFG1_EXT_TDM;

	/*
	 * We must only write zero to bit 1 and only one to bit 3 according to
	 * the datasheet.
	 */
	val = ret;
	val &= ~ADT7411_CFG1_RESERVED_BIT1;
	val |= ADT7411_CFG1_RESERVED_BIT3;

	/* enable monitoring */
	val |= ADT7411_CFG1_START_MONITOR;

	return i2c_smbus_write_byte_data(data->client, ADT7411_REG_CFG1, val);
}

static const u32 adt7411_in_config[] = {
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	HWMON_I_INPUT,
	0
};

static const struct hwmon_channel_info adt7411_in = {
	.type = hwmon_in,
	.config = adt7411_in_config,
};

static const u32 adt7411_temp_config[] = {
	HWMON_T_INPUT,
	HWMON_T_INPUT,
	0
};

static const struct hwmon_channel_info adt7411_temp = {
	.type = hwmon_temp,
	.config = adt7411_temp_config,
};

static const struct hwmon_channel_info *adt7411_info[] = {
	&adt7411_in,
	&adt7411_temp,
	NULL
};

static const struct hwmon_ops adt7411_hwmon_ops = {
	.is_visible = adt7411_is_visible,
	.read = adt7411_read,
};

static const struct hwmon_chip_info adt7411_chip_info = {
	.ops = &adt7411_hwmon_ops,
	.info = adt7411_info,
};

static int adt7411_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct device *dev = &client->dev;
	struct adt7411_data *data;
	struct device *hwmon_dev;
	int ret;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	i2c_set_clientdata(client, data);
	data->client = client;
	mutex_init(&data->device_lock);
	mutex_init(&data->update_lock);

	ret = adt7411_init_device(data);
	if (ret < 0)
		return ret;

	/* force update on first occasion */
	data->next_update = jiffies;

	hwmon_dev = devm_hwmon_device_register_with_info(dev, client->name,
							 data,
							 &adt7411_chip_info,
							 adt7411_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct i2c_device_id adt7411_id[] = {
	{ "adt7411", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, adt7411_id);

static struct i2c_driver adt7411_driver = {
	.driver		= {
		.name		= "adt7411",
	},
	.probe  = adt7411_probe,
	.id_table = adt7411_id,
	.detect = adt7411_detect,
	.address_list = normal_i2c,
	.class = I2C_CLASS_HWMON,
};

module_i2c_driver(adt7411_driver);

MODULE_AUTHOR("Sascha Hauer <s.hauer@pengutronix.de> and "
	"Wolfram Sang <w.sang@pengutronix.de>");
MODULE_DESCRIPTION("ADT7411 driver");
MODULE_LICENSE("GPL v2");
