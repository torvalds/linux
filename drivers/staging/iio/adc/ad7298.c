/*
 * AD7298 digital temperature sensor driver supporting AD7298
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/list.h>
#include <linux/spi/spi.h>
#include <linux/rtc.h>

#include "../iio.h"
#include "../sysfs.h"

/*
 * AD7298 command
 */
#define AD7298_PD			0x1
#define AD7298_T_AVG_MASK		0x2
#define AD7298_EXT_REF			0x4
#define AD7298_T_SENSE_MASK		0x20
#define AD7298_VOLTAGE_MASK		0x3fc0
#define AD7298_VOLTAGE_OFFSET		0x6
#define AD7298_VOLTAGE_LIMIT_COUNT	8
#define AD7298_REPEAT			0x40
#define AD7298_WRITE			0x80

/*
 * AD7298 value masks
 */
#define AD7298_CHANNEL_MASK		0xf000
#define AD7298_VALUE_MASK		0xfff
#define AD7298_T_VALUE_SIGN		0x400
#define AD7298_T_VALUE_FLOAT_OFFSET	2
#define AD7298_T_VALUE_FLOAT_MASK	0x2

/*
 * struct ad7298_chip_info - chip specifc information
 */

struct ad7298_chip_info {
	const char *name;
	struct spi_device *spi_dev;
	struct iio_dev *indio_dev;
	u16 command;
	u16 busy_pin;
	u8  channels;	/* Active voltage channels */
};

/*
 * ad7298 register access by SPI
 */
static int ad7298_spi_write(struct ad7298_chip_info *chip, u16 data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	int ret = 0;

	data |= AD7298_WRITE;
	data = cpu_to_be16(data);
	ret = spi_write(spi_dev, (u8 *)&data, sizeof(data));
	if (ret < 0)
		dev_err(&spi_dev->dev, "SPI write error\n");

	return ret;
}

static int ad7298_spi_read(struct ad7298_chip_info *chip, u16 mask, u16 *data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	int ret = 0;
	u8 count = chip->channels;
	u16 command;
	int i;

	if (mask & AD7298_T_SENSE_MASK) {
		command = chip->command & ~(AD7298_T_AVG_MASK | AD7298_VOLTAGE_MASK);
		command |= AD7298_T_SENSE_MASK;
		count = 1;
	} else if (mask & AD7298_T_AVG_MASK) {
		command = chip->command & ~AD7298_VOLTAGE_MASK;
		command |= AD7298_T_SENSE_MASK | AD7298_T_AVG_MASK;
		count = 2;
	} else if (mask & AD7298_VOLTAGE_MASK) {
		command = chip->command & ~(AD7298_T_AVG_MASK | AD7298_T_SENSE_MASK);
		count = chip->channels;
	}

	ret = ad7298_spi_write(chip, chip->command);
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI write command error\n");
		return ret;
	}

	ret = spi_read(spi_dev, (u8 *)&command, sizeof(command));
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI read error\n");
		return ret;
	}

	i = 10000;
	while (i && gpio_get_value(chip->busy_pin)) {
		cpu_relax();
		i--;
	}
	if (!i) {
		dev_err(&spi_dev->dev, "Always in busy convertion.\n");
		return -EBUSY;
	}

	for (i = 0; i < count; i++) {
		ret = spi_read(spi_dev, (u8 *)&data[i], sizeof(data[i]));
		if (ret < 0) {
			dev_err(&spi_dev->dev, "SPI read error\n");
			return ret;
		}
		*data = be16_to_cpu(data[i]);
	}

	return 0;
}

static ssize_t ad7298_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;

	if (chip->command & AD7298_REPEAT)
		return sprintf(buf, "repeat\n");
	else
		return sprintf(buf, "normal\n");
}

static ssize_t ad7298_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;

	if (strcmp(buf, "repeat"))
		chip->command |= AD7298_REPEAT;
	else
		chip->command &= (~AD7298_REPEAT);

	return 1;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		ad7298_show_mode,
		ad7298_store_mode,
		0);

static ssize_t ad7298_show_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "normal\nrepeat\n");
}

static IIO_DEVICE_ATTR(available_modes, S_IRUGO, ad7298_show_available_modes, NULL, 0);

static ssize_t ad7298_store_reset(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	u16 command;
	int ret;

	command = chip->command & ~AD7298_PD;

	ret = ad7298_spi_write(chip, command);
	if (ret)
		return -EIO;

	command = chip->command | AD7298_PD;

	ret = ad7298_spi_write(chip, command);
	if (ret)
		return -EIO;

	return len;
}

static IIO_DEVICE_ATTR(reset, S_IWUSR,
		NULL,
		ad7298_store_reset,
		0);

static ssize_t ad7298_show_ext_ref(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "%d\n", !!(chip->command & AD7298_EXT_REF));
}

static ssize_t ad7298_store_ext_ref(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	u16 command;
	int ret;

	command = chip->command & (~AD7298_EXT_REF);
	if (strcmp(buf, "1"))
		command |= AD7298_EXT_REF;

	ret = ad7298_spi_write(chip, command);
	if (ret)
		return -EIO;

	chip->command = command;

	return len;
}

static IIO_DEVICE_ATTR(ext_ref, S_IRUGO | S_IWUSR,
		ad7298_show_ext_ref,
		ad7298_store_ext_ref,
		0);

static ssize_t ad7298_show_t_sense(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	u16 data;
	char sign = ' ';
	int ret;

	ret = ad7298_spi_read(chip, AD7298_T_SENSE_MASK, &data);
	if (ret)
		return -EIO;

	if (data & AD7298_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data = (AD7298_T_VALUE_SIGN << 1) - data;
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
		(data >> AD7298_T_VALUE_FLOAT_OFFSET),
		(data & AD7298_T_VALUE_FLOAT_MASK) * 25);
}

static IIO_DEVICE_ATTR(t_sense, S_IRUGO, ad7298_show_t_sense, NULL, 0);

static ssize_t ad7298_show_t_average(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	u16 data[2];
	char sign = ' ';
	int ret;

	ret = ad7298_spi_read(chip, AD7298_T_AVG_MASK, data);
	if (ret)
		return -EIO;

	if (data[1] & AD7298_T_VALUE_SIGN) {
		/* convert supplement to positive value */
		data[1] = (AD7298_T_VALUE_SIGN << 1) - data[1];
		sign = '-';
	}

	return sprintf(buf, "%c%d.%.2d\n", sign,
		(data[1] >> AD7298_T_VALUE_FLOAT_OFFSET),
		(data[1] & AD7298_T_VALUE_FLOAT_MASK) * 25);
}

static IIO_DEVICE_ATTR(t_average, S_IRUGO, ad7298_show_t_average, NULL, 0);

static ssize_t ad7298_show_voltage(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	u16 data[AD7298_VOLTAGE_LIMIT_COUNT];
	int i, size, ret;

	ret = ad7298_spi_read(chip, AD7298_VOLTAGE_MASK, data);
	if (ret)
		return -EIO;

	for (i = 0; i < AD7298_VOLTAGE_LIMIT_COUNT; i++) {
		if (chip->command & (AD7298_T_SENSE_MASK << i)) {
			ret = sprintf(buf, "channel[%d]=%d\n", i,
					data[i] & AD7298_VALUE_MASK);
			if (ret < 0)
				break;
			buf += ret;
			size += ret;
		}
	}

	return size;
}

static IIO_DEVICE_ATTR(voltage, S_IRUGO, ad7298_show_voltage, NULL, 0);

static ssize_t ad7298_show_channel_mask(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;

	return sprintf(buf, "0x%x\n", (chip->command & AD7298_VOLTAGE_MASK) >>
			AD7298_VOLTAGE_OFFSET);
}

static ssize_t ad7298_store_channel_mask(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	unsigned long data;
	int i, ret;

	ret = strict_strtoul(buf, 16, &data);
	if (ret || data > 0xff)
		return -EINVAL;

	chip->command &= (~AD7298_VOLTAGE_MASK);
	chip->command |= data << AD7298_VOLTAGE_OFFSET;

	for (i = 0, chip->channels = 0; i < AD7298_VOLTAGE_LIMIT_COUNT; i++) {
		if (chip->command & (AD7298_T_SENSE_MASK << i))
			chip->channels++;
	}

	return ret;
}

static IIO_DEVICE_ATTR(channel_mask, S_IRUGO | S_IWUSR,
		ad7298_show_channel_mask,
		ad7298_store_channel_mask,
		0);

static ssize_t ad7298_show_name(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7298_chip_info *chip = dev_info->dev_data;
	return sprintf(buf, "%s\n", chip->name);
}

static IIO_DEVICE_ATTR(name, S_IRUGO, ad7298_show_name, NULL, 0);

static struct attribute *ad7298_attributes[] = {
	&iio_dev_attr_available_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_reset.dev_attr.attr,
	&iio_dev_attr_ext_ref.dev_attr.attr,
	&iio_dev_attr_t_sense.dev_attr.attr,
	&iio_dev_attr_t_average.dev_attr.attr,
	&iio_dev_attr_voltage.dev_attr.attr,
	&iio_dev_attr_channel_mask.dev_attr.attr,
	&iio_dev_attr_name.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7298_attribute_group = {
	.attrs = ad7298_attributes,
};

/*
 * device probe and remove
 */
static int __devinit ad7298_probe(struct spi_device *spi_dev)
{
	struct ad7298_chip_info *chip;
	unsigned short *pins = spi_dev->dev.platform_data;
	int ret = 0;

	chip = kzalloc(sizeof(struct ad7298_chip_info), GFP_KERNEL);

	if (chip == NULL)
		return -ENOMEM;

	/* this is only used for device removal purposes */
	dev_set_drvdata(&spi_dev->dev, chip);

	chip->spi_dev = spi_dev;
	chip->name = spi_dev->modalias;
	chip->busy_pin = pins[0];

	ret = gpio_request(chip->busy_pin, chip->name);
	if (ret) {
		dev_err(&spi_dev->dev, "Fail to request busy gpio PIN %d.\n",
			chip->busy_pin);
		goto error_free_chip;
	}
	gpio_direction_input(chip->busy_pin);

	chip->indio_dev = iio_allocate_device();
	if (chip->indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_free_gpio;
	}

	chip->indio_dev->dev.parent = &spi_dev->dev;
	chip->indio_dev->attrs = &ad7298_attribute_group;
	chip->indio_dev->dev_data = (void *)chip;
	chip->indio_dev->driver_module = THIS_MODULE;
	chip->indio_dev->modes = INDIO_DIRECT_MODE;

	ret = iio_device_register(chip->indio_dev);
	if (ret)
		goto error_free_dev;

	dev_info(&spi_dev->dev, "%s temperature sensor and ADC registered.\n",
			 chip->name);

	return 0;

error_free_dev:
	iio_free_device(chip->indio_dev);
error_free_gpio:
	gpio_free(chip->busy_pin);
error_free_chip:
	kfree(chip);

	return ret;
}

static int __devexit ad7298_remove(struct spi_device *spi_dev)
{
	struct ad7298_chip_info *chip = dev_get_drvdata(&spi_dev->dev);
	struct iio_dev *indio_dev = chip->indio_dev;

	dev_set_drvdata(&spi_dev->dev, NULL);
	iio_device_unregister(indio_dev);
	iio_free_device(chip->indio_dev);
	gpio_free(chip->busy_pin);
	kfree(chip);

	return 0;
}

static const struct spi_device_id ad7298_id[] = {
	{ "ad7298", 0 },
	{}
};

MODULE_DEVICE_TABLE(spi, ad7298_id);

static struct spi_driver ad7298_driver = {
	.driver = {
		.name = "ad7298",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = ad7298_probe,
	.remove = __devexit_p(ad7298_remove),
	.id_table = ad7298_id,
};

static __init int ad7298_init(void)
{
	return spi_register_driver(&ad7298_driver);
}

static __exit void ad7298_exit(void)
{
	spi_unregister_driver(&ad7298_driver);
}

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7298 digital"
			" temperature sensor and ADC driver");
MODULE_LICENSE("GPL v2");

module_init(ad7298_init);
module_exit(ad7298_exit);
