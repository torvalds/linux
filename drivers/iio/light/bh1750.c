/*
 * ROHM BH1710/BH1715/BH1721/BH1750/BH1751 ambient light sensor driver
 *
 * Copyright (c) Tomasz Duszynski <tduszyns@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Data sheets:
 *  http://rohmfs.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1710fvc-e.pdf
 *  http://rohmfs.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1715fvc-e.pdf
 *  http://rohmfs.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1721fvc-e.pdf
 *  http://rohmfs.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1750fvi-e.pdf
 *  http://rohmfs.rohm.com/en/products/databook/datasheet/ic/sensor/light/bh1751fvi-e.pdf
 *
 * 7-bit I2C slave addresses:
 *  0x23 (ADDR pin low)
 *  0x5C (ADDR pin high)
 *
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/module.h>

#define BH1750_POWER_DOWN		0x00
#define BH1750_ONE_TIME_H_RES_MODE	0x20 /* auto-mode for BH1721 */
#define BH1750_CHANGE_INT_TIME_H_BIT	0x40
#define BH1750_CHANGE_INT_TIME_L_BIT	0x60

enum {
	BH1710,
	BH1721,
	BH1750,
};

struct bh1750_chip_info;
struct bh1750_data {
	struct i2c_client *client;
	struct mutex lock;
	const struct bh1750_chip_info *chip_info;
	u16 mtreg;
};

struct bh1750_chip_info {
	u16 mtreg_min;
	u16 mtreg_max;
	u16 mtreg_default;
	int mtreg_to_usec;
	int mtreg_to_scale;

	/*
	 * For BH1710/BH1721 all possible integration time values won't fit
	 * into one page so displaying is limited to every second one.
	 * Note, that user can still write proper values which were not
	 * listed.
	 */
	int inc;

	u16 int_time_low_mask;
	u16 int_time_high_mask;
}

static const bh1750_chip_info_tbl[] = {
	[BH1710] = { 140, 1022, 300, 400,  250000000, 2, 0x001F, 0x03E0 },
	[BH1721] = { 140, 1020, 300, 400,  250000000, 2, 0x0010, 0x03E0 },
	[BH1750] = { 31,  254,  69,  1740, 57500000,  1, 0x001F, 0x00E0 },
};

static int bh1750_change_int_time(struct bh1750_data *data, int usec)
{
	int ret;
	u16 val;
	u8 regval;
	const struct bh1750_chip_info *chip_info = data->chip_info;

	if ((usec % chip_info->mtreg_to_usec) != 0)
		return -EINVAL;

	val = usec / chip_info->mtreg_to_usec;
	if (val < chip_info->mtreg_min || val > chip_info->mtreg_max)
		return -EINVAL;

	ret = i2c_smbus_write_byte(data->client, BH1750_POWER_DOWN);
	if (ret < 0)
		return ret;

	regval = (val & chip_info->int_time_high_mask) >> 5;
	ret = i2c_smbus_write_byte(data->client,
				   BH1750_CHANGE_INT_TIME_H_BIT | regval);
	if (ret < 0)
		return ret;

	regval = val & chip_info->int_time_low_mask;
	ret = i2c_smbus_write_byte(data->client,
				   BH1750_CHANGE_INT_TIME_L_BIT | regval);
	if (ret < 0)
		return ret;

	data->mtreg = val;

	return 0;
}

static int bh1750_read(struct bh1750_data *data, int *val)
{
	int ret;
	__be16 result;
	const struct bh1750_chip_info *chip_info = data->chip_info;
	unsigned long delay = chip_info->mtreg_to_usec * data->mtreg;

	/*
	 * BH1721 will enter continuous mode on receiving this command.
	 * Note, that this eliminates need for bh1750_resume().
	 */
	ret = i2c_smbus_write_byte(data->client, BH1750_ONE_TIME_H_RES_MODE);
	if (ret < 0)
		return ret;

	usleep_range(delay + 15000, delay + 40000);

	ret = i2c_master_recv(data->client, (char *)&result, 2);
	if (ret < 0)
		return ret;

	*val = be16_to_cpu(result);

	return 0;
}

static int bh1750_read_raw(struct iio_dev *indio_dev,
			   struct iio_chan_spec const *chan,
			   int *val, int *val2, long mask)
{
	int ret, tmp;
	struct bh1750_data *data = iio_priv(indio_dev);
	const struct bh1750_chip_info *chip_info = data->chip_info;

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		switch (chan->type) {
		case IIO_LIGHT:
			mutex_lock(&data->lock);
			ret = bh1750_read(data, val);
			mutex_unlock(&data->lock);
			if (ret < 0)
				return ret;

			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SCALE:
		tmp = chip_info->mtreg_to_scale / data->mtreg;
		*val = tmp / 1000000;
		*val2 = tmp % 1000000;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_INT_TIME:
		*val = 0;
		*val2 = chip_info->mtreg_to_usec * data->mtreg;
		return IIO_VAL_INT_PLUS_MICRO;
	default:
		return -EINVAL;
	}
}

static int bh1750_write_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int val, int val2, long mask)
{
	int ret;
	struct bh1750_data *data = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_INT_TIME:
		if (val != 0)
			return -EINVAL;

		mutex_lock(&data->lock);
		ret = bh1750_change_int_time(data, val2);
		mutex_unlock(&data->lock);
		return ret;
	default:
		return -EINVAL;
	}
}

static ssize_t bh1750_show_int_time_available(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i;
	size_t len = 0;
	struct bh1750_data *data = iio_priv(dev_to_iio_dev(dev));
	const struct bh1750_chip_info *chip_info = data->chip_info;

	for (i = chip_info->mtreg_min; i <= chip_info->mtreg_max; i += chip_info->inc)
		len += scnprintf(buf + len, PAGE_SIZE - len, "0.%06d ",
				 chip_info->mtreg_to_usec * i);

	buf[len - 1] = '\n';

	return len;
}

static IIO_DEV_ATTR_INT_TIME_AVAIL(bh1750_show_int_time_available);

static struct attribute *bh1750_attributes[] = {
	&iio_dev_attr_integration_time_available.dev_attr.attr,
	NULL,
};

static struct attribute_group bh1750_attribute_group = {
	.attrs = bh1750_attributes,
};

static const struct iio_info bh1750_info = {
	.driver_module = THIS_MODULE,
	.attrs = &bh1750_attribute_group,
	.read_raw = bh1750_read_raw,
	.write_raw = bh1750_write_raw,
};

static const struct iio_chan_spec bh1750_channels[] = {
	{
		.type = IIO_LIGHT,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_INT_TIME)
	}
};

static int bh1750_probe(struct i2c_client *client,
			const struct i2c_device_id *id)
{
	int ret, usec;
	struct bh1750_data *data;
	struct iio_dev *indio_dev;

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C |
				I2C_FUNC_SMBUS_WRITE_BYTE))
		return -ENODEV;

	indio_dev = devm_iio_device_alloc(&client->dev, sizeof(*data));
	if (!indio_dev)
		return -ENOMEM;

	data = iio_priv(indio_dev);
	i2c_set_clientdata(client, indio_dev);
	data->client = client;
	data->chip_info = &bh1750_chip_info_tbl[id->driver_data];

	usec = data->chip_info->mtreg_to_usec * data->chip_info->mtreg_default;
	ret = bh1750_change_int_time(data, usec);
	if (ret < 0)
		return ret;

	mutex_init(&data->lock);
	indio_dev->dev.parent = &client->dev;
	indio_dev->info = &bh1750_info;
	indio_dev->name = id->name;
	indio_dev->channels = bh1750_channels;
	indio_dev->num_channels = ARRAY_SIZE(bh1750_channels);
	indio_dev->modes = INDIO_DIRECT_MODE;

	return iio_device_register(indio_dev);
}

static int bh1750_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct bh1750_data *data = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

	mutex_lock(&data->lock);
	i2c_smbus_write_byte(client, BH1750_POWER_DOWN);
	mutex_unlock(&data->lock);

	return 0;
}

#ifdef CONFIG_PM_SLEEP
static int bh1750_suspend(struct device *dev)
{
	int ret;
	struct bh1750_data *data =
		iio_priv(i2c_get_clientdata(to_i2c_client(dev)));

	/*
	 * This is mainly for BH1721 which doesn't enter power down
	 * mode automatically.
	 */
	mutex_lock(&data->lock);
	ret = i2c_smbus_write_byte(data->client, BH1750_POWER_DOWN);
	mutex_unlock(&data->lock);

	return ret;
}

static SIMPLE_DEV_PM_OPS(bh1750_pm_ops, bh1750_suspend, NULL);
#define BH1750_PM_OPS (&bh1750_pm_ops)
#else
#define BH1750_PM_OPS NULL
#endif

static const struct i2c_device_id bh1750_id[] = {
	{ "bh1710", BH1710 },
	{ "bh1715", BH1750 },
	{ "bh1721", BH1721 },
	{ "bh1750", BH1750 },
	{ "bh1751", BH1750 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, bh1750_id);

static struct i2c_driver bh1750_driver = {
	.driver = {
		.name = "bh1750",
		.pm = BH1750_PM_OPS,
	},
	.probe = bh1750_probe,
	.remove = bh1750_remove,
	.id_table = bh1750_id,

};
module_i2c_driver(bh1750_driver);

MODULE_AUTHOR("Tomasz Duszynski <tduszyns@gmail.com>");
MODULE_DESCRIPTION("ROHM BH1710/BH1715/BH1721/BH1750/BH1751 als driver");
MODULE_LICENSE("GPL v2");
