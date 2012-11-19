/*
 *  isl29003.c - Linux kernel module for
 * 	Intersil ISL29003 ambient light sensor
 *
 *  See file:Documentation/misc-devices/isl29003
 *
 *  Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
 *
 *  Based on code written by
 *  	Rodolfo Giometti <giometti@linux.it>
 *  	Eurotech S.p.A. <info@eurotech.it>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/i2c.h>
#include <linux/mutex.h>
#include <linux/delay.h>

#define ISL29003_DRV_NAME	"isl29003"
#define DRIVER_VERSION		"1.0"

#define ISL29003_REG_COMMAND		0x00
#define ISL29003_ADC_ENABLED		(1 << 7)
#define ISL29003_ADC_PD			(1 << 6)
#define ISL29003_TIMING_INT		(1 << 5)
#define ISL29003_MODE_SHIFT		(2)
#define ISL29003_MODE_MASK		(0x3 << ISL29003_MODE_SHIFT)
#define ISL29003_RES_SHIFT		(0)
#define ISL29003_RES_MASK		(0x3 << ISL29003_RES_SHIFT)

#define ISL29003_REG_CONTROL		0x01
#define ISL29003_INT_FLG		(1 << 5)
#define ISL29003_RANGE_SHIFT		(2)
#define ISL29003_RANGE_MASK		(0x3 << ISL29003_RANGE_SHIFT)
#define ISL29003_INT_PERSISTS_SHIFT	(0)
#define ISL29003_INT_PERSISTS_MASK	(0xf << ISL29003_INT_PERSISTS_SHIFT)

#define ISL29003_REG_IRQ_THRESH_HI	0x02
#define ISL29003_REG_IRQ_THRESH_LO	0x03
#define ISL29003_REG_LSB_SENSOR		0x04
#define ISL29003_REG_MSB_SENSOR		0x05
#define ISL29003_REG_LSB_TIMER		0x06
#define ISL29003_REG_MSB_TIMER		0x07

#define ISL29003_NUM_CACHABLE_REGS	4

struct isl29003_data {
	struct i2c_client *client;
	struct mutex lock;
	u8 reg_cache[ISL29003_NUM_CACHABLE_REGS];
	u8 power_state_before_suspend;
};

static int gain_range[] = {
	1000, 4000, 16000, 64000
};

/*
 * register access helpers
 */

static int __isl29003_read_reg(struct i2c_client *client,
			       u32 reg, u8 mask, u8 shift)
{
	struct isl29003_data *data = i2c_get_clientdata(client);
	return (data->reg_cache[reg] & mask) >> shift;
}

static int __isl29003_write_reg(struct i2c_client *client,
				u32 reg, u8 mask, u8 shift, u8 val)
{
	struct isl29003_data *data = i2c_get_clientdata(client);
	int ret = 0;
	u8 tmp;

	if (reg >= ISL29003_NUM_CACHABLE_REGS)
		return -EINVAL;

	mutex_lock(&data->lock);

	tmp = data->reg_cache[reg];
	tmp &= ~mask;
	tmp |= val << shift;

	ret = i2c_smbus_write_byte_data(client, reg, tmp);
	if (!ret)
		data->reg_cache[reg] = tmp;

	mutex_unlock(&data->lock);
	return ret;
}

/*
 * internally used functions
 */

/* range */
static int isl29003_get_range(struct i2c_client *client)
{
	return __isl29003_read_reg(client, ISL29003_REG_CONTROL,
		ISL29003_RANGE_MASK, ISL29003_RANGE_SHIFT);
}

static int isl29003_set_range(struct i2c_client *client, int range)
{
	return __isl29003_write_reg(client, ISL29003_REG_CONTROL,
		ISL29003_RANGE_MASK, ISL29003_RANGE_SHIFT, range);
}

/* resolution */
static int isl29003_get_resolution(struct i2c_client *client)
{
	return __isl29003_read_reg(client, ISL29003_REG_COMMAND,
		ISL29003_RES_MASK, ISL29003_RES_SHIFT);
}

static int isl29003_set_resolution(struct i2c_client *client, int res)
{
	return __isl29003_write_reg(client, ISL29003_REG_COMMAND,
		ISL29003_RES_MASK, ISL29003_RES_SHIFT, res);
}

/* mode */
static int isl29003_get_mode(struct i2c_client *client)
{
	return __isl29003_read_reg(client, ISL29003_REG_COMMAND,
		ISL29003_RES_MASK, ISL29003_RES_SHIFT);
}

static int isl29003_set_mode(struct i2c_client *client, int mode)
{
	return __isl29003_write_reg(client, ISL29003_REG_COMMAND,
		ISL29003_RES_MASK, ISL29003_RES_SHIFT, mode);
}

/* power_state */
static int isl29003_set_power_state(struct i2c_client *client, int state)
{
	return __isl29003_write_reg(client, ISL29003_REG_COMMAND,
				ISL29003_ADC_ENABLED | ISL29003_ADC_PD, 0,
				state ? ISL29003_ADC_ENABLED : ISL29003_ADC_PD);
}

static int isl29003_get_power_state(struct i2c_client *client)
{
	struct isl29003_data *data = i2c_get_clientdata(client);
	u8 cmdreg = data->reg_cache[ISL29003_REG_COMMAND];
	return ~cmdreg & ISL29003_ADC_PD;
}

static int isl29003_get_adc_value(struct i2c_client *client)
{
	struct isl29003_data *data = i2c_get_clientdata(client);
	int lsb, msb, range, bitdepth;

	mutex_lock(&data->lock);
	lsb = i2c_smbus_read_byte_data(client, ISL29003_REG_LSB_SENSOR);

	if (lsb < 0) {
		mutex_unlock(&data->lock);
		return lsb;
	}

	msb = i2c_smbus_read_byte_data(client, ISL29003_REG_MSB_SENSOR);
	mutex_unlock(&data->lock);

	if (msb < 0)
		return msb;

	range = isl29003_get_range(client);
	bitdepth = (4 - isl29003_get_resolution(client)) * 4;
	return (((msb << 8) | lsb) * gain_range[range]) >> bitdepth;
}

/*
 * sysfs layer
 */

/* range */
static ssize_t isl29003_show_range(struct device *dev,
				   struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%i\n", isl29003_get_range(client));
}

static ssize_t isl29003_store_range(struct device *dev,
				    struct device_attribute *attr,
				    const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val;
	int ret;

	if ((strict_strtoul(buf, 10, &val) < 0) || (val > 3))
		return -EINVAL;

	ret = isl29003_set_range(client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(range, S_IWUSR | S_IRUGO,
		   isl29003_show_range, isl29003_store_range);


/* resolution */
static ssize_t isl29003_show_resolution(struct device *dev,
					struct device_attribute *attr,
					char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%d\n", isl29003_get_resolution(client));
}

static ssize_t isl29003_store_resolution(struct device *dev,
					 struct device_attribute *attr,
					 const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val;
	int ret;

	if ((strict_strtoul(buf, 10, &val) < 0) || (val > 3))
		return -EINVAL;

	ret = isl29003_set_resolution(client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(resolution, S_IWUSR | S_IRUGO,
		   isl29003_show_resolution, isl29003_store_resolution);

/* mode */
static ssize_t isl29003_show_mode(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%d\n", isl29003_get_mode(client));
}

static ssize_t isl29003_store_mode(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val;
	int ret;

	if ((strict_strtoul(buf, 10, &val) < 0) || (val > 2))
		return -EINVAL;

	ret = isl29003_set_mode(client, val);
	if (ret < 0)
		return ret;

	return count;
}

static DEVICE_ATTR(mode, S_IWUSR | S_IRUGO,
		   isl29003_show_mode, isl29003_store_mode);


/* power state */
static ssize_t isl29003_show_power_state(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);
	return sprintf(buf, "%d\n", isl29003_get_power_state(client));
}

static ssize_t isl29003_store_power_state(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	struct i2c_client *client = to_i2c_client(dev);
	unsigned long val;
	int ret;

	if ((strict_strtoul(buf, 10, &val) < 0) || (val > 1))
		return -EINVAL;

	ret = isl29003_set_power_state(client, val);
	return ret ? ret : count;
}

static DEVICE_ATTR(power_state, S_IWUSR | S_IRUGO,
		   isl29003_show_power_state, isl29003_store_power_state);


/* lux */
static ssize_t isl29003_show_lux(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct i2c_client *client = to_i2c_client(dev);

	/* No LUX data if not operational */
	if (!isl29003_get_power_state(client))
		return -EBUSY;

	return sprintf(buf, "%d\n", isl29003_get_adc_value(client));
}

static DEVICE_ATTR(lux, S_IRUGO, isl29003_show_lux, NULL);

static struct attribute *isl29003_attributes[] = {
	&dev_attr_range.attr,
	&dev_attr_resolution.attr,
	&dev_attr_mode.attr,
	&dev_attr_power_state.attr,
	&dev_attr_lux.attr,
	NULL
};

static const struct attribute_group isl29003_attr_group = {
	.attrs = isl29003_attributes,
};

static int isl29003_init_client(struct i2c_client *client)
{
	struct isl29003_data *data = i2c_get_clientdata(client);
	int i;

	/* read all the registers once to fill the cache.
	 * if one of the reads fails, we consider the init failed */
	for (i = 0; i < ARRAY_SIZE(data->reg_cache); i++) {
		int v = i2c_smbus_read_byte_data(client, i);
		if (v < 0)
			return -ENODEV;

		data->reg_cache[i] = v;
	}

	/* set defaults */
	isl29003_set_range(client, 0);
	isl29003_set_resolution(client, 0);
	isl29003_set_mode(client, 0);
	isl29003_set_power_state(client, 0);

	return 0;
}

/*
 * I2C layer
 */

static int isl29003_probe(struct i2c_client *client,
				    const struct i2c_device_id *id)
{
	struct i2c_adapter *adapter = to_i2c_adapter(client->dev.parent);
	struct isl29003_data *data;
	int err = 0;

	if (!i2c_check_functionality(adapter, I2C_FUNC_SMBUS_BYTE))
		return -EIO;

	data = kzalloc(sizeof(struct isl29003_data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	i2c_set_clientdata(client, data);
	mutex_init(&data->lock);

	/* initialize the ISL29003 chip */
	err = isl29003_init_client(client);
	if (err)
		goto exit_kfree;

	/* register sysfs hooks */
	err = sysfs_create_group(&client->dev.kobj, &isl29003_attr_group);
	if (err)
		goto exit_kfree;

	dev_info(&client->dev, "driver version %s enabled\n", DRIVER_VERSION);
	return 0;

exit_kfree:
	kfree(data);
	return err;
}

static int __devexit isl29003_remove(struct i2c_client *client)
{
	sysfs_remove_group(&client->dev.kobj, &isl29003_attr_group);
	isl29003_set_power_state(client, 0);
	kfree(i2c_get_clientdata(client));
	return 0;
}

#ifdef CONFIG_PM
static int isl29003_suspend(struct i2c_client *client, pm_message_t mesg)
{
	struct isl29003_data *data = i2c_get_clientdata(client);

	data->power_state_before_suspend = isl29003_get_power_state(client);
	return isl29003_set_power_state(client, 0);
}

static int isl29003_resume(struct i2c_client *client)
{
	int i;
	struct isl29003_data *data = i2c_get_clientdata(client);

	/* restore registers from cache */
	for (i = 0; i < ARRAY_SIZE(data->reg_cache); i++)
		if (i2c_smbus_write_byte_data(client, i, data->reg_cache[i]))
			return -EIO;

	return isl29003_set_power_state(client,
		data->power_state_before_suspend);
}

#else
#define isl29003_suspend	NULL
#define isl29003_resume		NULL
#endif /* CONFIG_PM */

static const struct i2c_device_id isl29003_id[] = {
	{ "isl29003", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, isl29003_id);

static struct i2c_driver isl29003_driver = {
	.driver = {
		.name	= ISL29003_DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.suspend = isl29003_suspend,
	.resume	= isl29003_resume,
	.probe	= isl29003_probe,
	.remove	= isl29003_remove,
	.id_table = isl29003_id,
};

module_i2c_driver(isl29003_driver);

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("ISL29003 ambient light sensor driver");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);
