/*
 * AD7152 capacitive sensor driver supporting AD7152/3
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/i2c.h>

#include "../iio.h"
#include "../sysfs.h"

/*
 * AD7152 registers definition
 */

#define AD7152_STATUS              0
#define AD7152_STATUS_RDY1         (1 << 0)
#define AD7152_STATUS_RDY2         (1 << 1)
#define AD7152_CH1_DATA_HIGH       1
#define AD7152_CH1_DATA_LOW        2
#define AD7152_CH2_DATA_HIGH       3
#define AD7152_CH2_DATA_LOW        4
#define AD7152_CH1_OFFS_HIGH       5
#define AD7152_CH1_OFFS_LOW        6
#define AD7152_CH2_OFFS_HIGH       7
#define AD7152_CH2_OFFS_LOW        8
#define AD7152_CH1_GAIN_HIGH       9
#define AD7152_CH1_GAIN_LOW        10
#define AD7152_CH1_SETUP           11
#define AD7152_CH2_GAIN_HIGH       12
#define AD7152_CH2_GAIN_LOW        13
#define AD7152_CH2_SETUP           14
#define AD7152_CFG                 15
#define AD7152_RESEVERD            16
#define AD7152_CAPDAC_POS          17
#define AD7152_CAPDAC_NEG          18
#define AD7152_CFG2                26

#define AD7152_MAX_CONV_MODE       6

/*
 * struct ad7152_chip_info - chip specifc information
 */

struct ad7152_chip_info {
	struct i2c_client *client;
	struct iio_dev *indio_dev;
	u16 ch1_offset;     /* Channel 1 offset calibration coefficient */
	u16 ch1_gain;       /* Channel 1 gain coefficient */
	u8  ch1_setup;
	u16 ch2_offset;     /* Channel 2 offset calibration coefficient */
	u16 ch2_gain;       /* Channel 1 gain coefficient */
	u8  ch2_setup;
	u8  filter_rate_setup; /* Capacitive channel digital filter setup; conversion time/update rate setup per channel */
	char *conversion_mode;
};

struct ad7152_conversion_mode {
	char *name;
	u8 reg_cfg;
};

static struct ad7152_conversion_mode
ad7152_conv_mode_table[AD7152_MAX_CONV_MODE] = {
	{ "idle", 0 },
	{ "continuous-conversion", 1 },
	{ "single-conversion", 2 },
	{ "power-down", 3 },
	{ "offset-calibration", 5 },
	{ "gain-calibration", 6 },
};

/*
 * ad7152 register access by I2C
 */

static int ad7152_i2c_read(struct ad7152_chip_info *chip, u8 reg, u8 *data, int len)
{
	struct i2c_client *client = chip->client;
	int ret;

	ret = i2c_master_send(client, &reg, 1);
	if (ret < 0) {
		dev_err(&client->dev, "I2C write error\n");
		return ret;
	}

	ret = i2c_master_recv(client, data, len);
	if (ret < 0) {
		dev_err(&client->dev, "I2C read error\n");
	}

	return ret;
}

static int ad7152_i2c_write(struct ad7152_chip_info *chip, u8 reg, u8 data)
{
	struct i2c_client *client = chip->client;
	int ret;

	u8 tx[2] = {
		reg,
		data,
	};

	ret = i2c_master_send(client, tx, 2);
	if (ret < 0)
		dev_err(&client->dev, "I2C write error\n");

	return ret;
}

/*
 * sysfs nodes
 */

#define IIO_DEV_ATTR_AVAIL_CONVERSION_MODES(_show)				\
	IIO_DEVICE_ATTR(available_conversion_modes, S_IRUGO, _show, NULL, 0)
#define IIO_DEV_ATTR_CONVERSION_MODE(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(conversion_mode, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH1_OFFSET(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(ch1_offset, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH2_OFFSET(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(ch2_offset, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH1_GAIN(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(ch1_gain, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH2_GAIN(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(ch2_gain, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH1_VALUE(_show)		\
	IIO_DEVICE_ATTR(ch1_value, S_IRUGO, _show, NULL, 0)
#define IIO_DEV_ATTR_CH2_VALUE(_show)		\
	IIO_DEVICE_ATTR(ch2_value, S_IRUGO, _show, NULL, 0)
#define IIO_DEV_ATTR_CH1_SETUP(_mode, _show, _store)		\
	IIO_DEVICE_ATTR(ch1_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_CH2_SETUP(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(ch2_setup, _mode, _show, _store, 0)
#define IIO_DEV_ATTR_FILTER_RATE_SETUP(_mode, _show, _store)              \
	IIO_DEVICE_ATTR(filter_rate_setup, _mode, _show, _store, 0)

static ssize_t ad7152_show_conversion_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	int i;
	int len = 0;

	for (i = 0; i < AD7152_MAX_CONV_MODE; i++)
		len += sprintf(buf + len, "%s ", ad7152_conv_mode_table[i].name);

	len += sprintf(buf + len, "\n");

	return len;
}

static IIO_DEV_ATTR_AVAIL_CONVERSION_MODES(ad7152_show_conversion_modes);

static ssize_t ad7152_show_ch1_value(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	u8 data[2];

	ad7152_i2c_read(chip, AD7152_CH1_DATA_HIGH, data, 2);
	return sprintf(buf, "%d\n", ((int)data[0] << 8) | data[1]);
}

static IIO_DEV_ATTR_CH1_VALUE(ad7152_show_ch1_value);

static ssize_t ad7152_show_ch2_value(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	u8 data[2];

	ad7152_i2c_read(chip, AD7152_CH2_DATA_HIGH, data, 2);
	return sprintf(buf, "%d\n", ((int)data[0] << 8) | data[1]);
}

static IIO_DEV_ATTR_CH2_VALUE(ad7152_show_ch2_value);

static ssize_t ad7152_show_conversion_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%s\n", chip->conversion_mode);
}

static ssize_t ad7152_store_conversion_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	u8 cfg;
	int i;

	ad7152_i2c_read(chip, AD7152_CFG, &cfg, 1);

	for (i = 0; i < AD7152_MAX_CONV_MODE; i++)
		if (strncmp(buf, ad7152_conv_mode_table[i].name,
				strlen(ad7152_conv_mode_table[i].name) - 1) == 0) {
			chip->conversion_mode = ad7152_conv_mode_table[i].name;
			cfg |= 0x18 | ad7152_conv_mode_table[i].reg_cfg;
			ad7152_i2c_write(chip, AD7152_CFG, cfg);
			return len;
		}

	dev_err(dev, "not supported conversion mode\n");

	return -EINVAL;
}

static IIO_DEV_ATTR_CONVERSION_MODE(S_IRUGO | S_IWUSR,
		ad7152_show_conversion_mode,
		ad7152_store_conversion_mode);

static ssize_t ad7152_show_ch1_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->ch1_offset);
}

static ssize_t ad7152_store_ch1_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad7152_i2c_write(chip, AD7152_CH1_OFFS_HIGH, data >> 8);
		ad7152_i2c_write(chip, AD7152_CH1_OFFS_LOW, data);
		chip->ch1_offset = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CH1_OFFSET(S_IRUGO | S_IWUSR,
		ad7152_show_ch1_offset,
		ad7152_store_ch1_offset);

static ssize_t ad7152_show_ch2_offset(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->ch2_offset);
}

static ssize_t ad7152_store_ch2_offset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad7152_i2c_write(chip, AD7152_CH2_OFFS_HIGH, data >> 8);
		ad7152_i2c_write(chip, AD7152_CH2_OFFS_LOW, data);
		chip->ch2_offset = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CH2_OFFSET(S_IRUGO | S_IWUSR,
		ad7152_show_ch2_offset,
		ad7152_store_ch2_offset);

static ssize_t ad7152_show_ch1_gain(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->ch1_gain);
}

static ssize_t ad7152_store_ch1_gain(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad7152_i2c_write(chip, AD7152_CH1_GAIN_HIGH, data >> 8);
		ad7152_i2c_write(chip, AD7152_CH1_GAIN_LOW, data);
		chip->ch1_gain = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CH1_GAIN(S_IRUGO | S_IWUSR,
		ad7152_show_ch1_gain,
		ad7152_store_ch1_gain);

static ssize_t ad7152_show_ch2_gain(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", chip->ch2_gain);
}

static ssize_t ad7152_store_ch2_gain(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x10000)) {
		ad7152_i2c_write(chip, AD7152_CH2_GAIN_HIGH, data >> 8);
		ad7152_i2c_write(chip, AD7152_CH2_GAIN_LOW, data);
		chip->ch2_gain = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CH2_GAIN(S_IRUGO | S_IWUSR,
		ad7152_show_ch2_gain,
		ad7152_store_ch2_gain);

static ssize_t ad7152_show_ch1_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%02x\n", chip->ch1_setup);
}

static ssize_t ad7152_store_ch1_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x100)) {
		ad7152_i2c_write(chip, AD7152_CH1_SETUP, data);
		chip->ch1_setup = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CH1_SETUP(S_IRUGO | S_IWUSR,
		ad7152_show_ch1_setup,
		ad7152_store_ch1_setup);

static ssize_t ad7152_show_ch2_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%02x\n", chip->ch2_setup);
}

static ssize_t ad7152_store_ch2_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x100)) {
		ad7152_i2c_write(chip, AD7152_CH2_SETUP, data);
		chip->ch2_setup = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_CH2_SETUP(S_IRUGO | S_IWUSR,
		ad7152_show_ch2_setup,
		ad7152_store_ch2_setup);

static ssize_t ad7152_show_filter_rate_setup(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%02x\n", chip->filter_rate_setup);
}

static ssize_t ad7152_store_filter_rate_setup(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7152_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int ret;

	ret = strict_strtoul(buf, 10, &data);

	if ((!ret) && (data < 0x100)) {
		ad7152_i2c_write(chip, AD7152_CFG2, data);
		chip->filter_rate_setup = data;
		return len;
	}

	return -EINVAL;
}

static IIO_DEV_ATTR_FILTER_RATE_SETUP(S_IRUGO | S_IWUSR,
		ad7152_show_filter_rate_setup,
		ad7152_store_filter_rate_setup);

static struct attribute *ad7152_attributes[] = {
	&iio_dev_attr_available_conversion_modes.dev_attr.attr,
	&iio_dev_attr_conversion_mode.dev_attr.attr,
	&iio_dev_attr_ch1_gain.dev_attr.attr,
	&iio_dev_attr_ch2_gain.dev_attr.attr,
	&iio_dev_attr_ch1_offset.dev_attr.attr,
	&iio_dev_attr_ch2_offset.dev_attr.attr,
	&iio_dev_attr_ch1_value.dev_attr.attr,
	&iio_dev_attr_ch2_value.dev_attr.attr,
	&iio_dev_attr_ch1_setup.dev_attr.attr,
	&iio_dev_attr_ch2_setup.dev_attr.attr,
	&iio_dev_attr_filter_rate_setup.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7152_attribute_group = {
	.attrs = ad7152_attributes,
};

static const struct iio_info ad7152_info = {
	.attrs = &ad7152_attribute_group,
	.driver_module = THIS_MODULE,
};
/*
 * device probe and remove
 */

static int __devinit ad7152_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int ret = 0;
	struct ad7152_chip_info *chip = kzalloc(sizeof(*chip), GFP_KERNEL);
	if (chip == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}

	/* this is only used for device removal purposes */
	i2c_set_clientdata(client, chip);

	chip->client = client;

	chip->indio_dev = iio_allocate_device(0);
	if (chip->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_chip;
	}

	/* Echipabilish that the iio_dev is a child of the i2c device */
	chip->indio_dev->name = id->name;
	chip->indio_dev->dev.parent = &client->dev;
	chip->indio_dev->info = &ad7152_info;
	chip->indio_dev->dev_data = (void *)(chip);
	chip->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(chip->indio_dev);
	if (ret)
		goto error_free_dev;

	dev_err(&client->dev, "%s capacitive sensor registered\n", id->name);

	return 0;

error_free_dev:
	iio_free_device(chip->indio_dev);
error_free_chip:
	kfree(chip);
error_ret:
	return ret;
}

static int __devexit ad7152_remove(struct i2c_client *client)
{
	struct ad7152_chip_info *chip = i2c_get_clientdata(client);
	struct iio_dev *indio_dev = chip->indio_dev;

	iio_device_unregister(indio_dev);
	kfree(chip);

	return 0;
}

static const struct i2c_device_id ad7152_id[] = {
	{ "ad7152", 0 },
	{ "ad7153", 0 },
	{}
};

MODULE_DEVICE_TABLE(i2c, ad7152_id);

static struct i2c_driver ad7152_driver = {
	.driver = {
		.name = "ad7152",
	},
	.probe = ad7152_probe,
	.remove = __devexit_p(ad7152_remove),
	.id_table = ad7152_id,
};

static __init int ad7152_init(void)
{
	return i2c_add_driver(&ad7152_driver);
}

static __exit void ad7152_exit(void)
{
	i2c_del_driver(&ad7152_driver);
}

MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_DESCRIPTION("Analog Devices ad7152/3 capacitive sensor driver");
MODULE_LICENSE("GPL v2");

module_init(ad7152_init);
module_exit(ad7152_exit);
