/*
 * AD7314 digital temperature sensor driver for AD7314, ADT7301 and ADT7302
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>

#include "../iio.h"
#include "../sysfs.h"

/*
 * AD7314 power mode
 */
#define AD7314_PD		0x2000

/*
 * AD7314 temperature masks
 */
#define AD7314_TEMP_SIGN		0x200
#define AD7314_TEMP_MASK		0x7FE0
#define AD7314_TEMP_OFFSET		5
#define AD7314_TEMP_FLOAT_OFFSET	2
#define AD7314_TEMP_FLOAT_MASK		0x3

/*
 * ADT7301 and ADT7302 temperature masks
 */
#define ADT7301_TEMP_SIGN		0x2000
#define ADT7301_TEMP_MASK		0x2FFF
#define ADT7301_TEMP_FLOAT_OFFSET	5
#define ADT7301_TEMP_FLOAT_MASK		0x1F

/*
 * struct ad7314_chip_info - chip specifc information
 */

struct ad7314_chip_info {
	struct spi_device *spi_dev;
	s64 last_timestamp;
	u8  mode;
};

/*
 * ad7314 register access by SPI
 */

static int ad7314_spi_read(struct ad7314_chip_info *chip, u16 *data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	int ret = 0;
	u16 value;

	ret = spi_read(spi_dev, (u8 *)&value, sizeof(value));
	if (ret < 0) {
		dev_err(&spi_dev->dev, "SPI read error\n");
		return ret;
	}

	*data = be16_to_cpu((u16)value);

	return ret;
}

static int ad7314_spi_write(struct ad7314_chip_info *chip, u16 data)
{
	struct spi_device *spi_dev = chip->spi_dev;
	int ret = 0;
	u16 value = cpu_to_be16(data);

	ret = spi_write(spi_dev, (u8 *)&value, sizeof(value));
	if (ret < 0)
		dev_err(&spi_dev->dev, "SPI write error\n");

	return ret;
}

static ssize_t ad7314_show_mode(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7314_chip_info *chip = iio_priv(dev_info);

	if (chip->mode)
		return sprintf(buf, "power-save\n");
	else
		return sprintf(buf, "full\n");
}

static ssize_t ad7314_store_mode(struct device *dev,
		struct device_attribute *attr,
		const char *buf,
		size_t len)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7314_chip_info *chip = iio_priv(dev_info);
	u16 mode = 0;
	int ret;

	if (!strcmp(buf, "full"))
		mode = AD7314_PD;

	ret = ad7314_spi_write(chip, mode);
	if (ret)
		return -EIO;

	chip->mode = mode;

	return len;
}

static IIO_DEVICE_ATTR(mode, S_IRUGO | S_IWUSR,
		ad7314_show_mode,
		ad7314_store_mode,
		0);

static ssize_t ad7314_show_available_modes(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	return sprintf(buf, "full\npower-save\n");
}

static IIO_DEVICE_ATTR(available_modes, S_IRUGO, ad7314_show_available_modes, NULL, 0);

static ssize_t ad7314_show_temperature(struct device *dev,
		struct device_attribute *attr,
		char *buf)
{
	struct iio_dev *dev_info = dev_get_drvdata(dev);
	struct ad7314_chip_info *chip = iio_priv(dev_info);
	u16 data;
	char sign = ' ';
	int ret;

	if (chip->mode) {
		ret = ad7314_spi_write(chip, 0);
		if (ret)
			return -EIO;
	}

	ret = ad7314_spi_read(chip, &data);
	if (ret)
		return -EIO;

	if (chip->mode)
		ad7314_spi_write(chip, chip->mode);

	if (strcmp(dev_info->name, "ad7314")) {
		data = (data & AD7314_TEMP_MASK) >>
			AD7314_TEMP_OFFSET;
		if (data & AD7314_TEMP_SIGN) {
			data = (AD7314_TEMP_SIGN << 1) - data;
			sign = '-';
		}

		return sprintf(buf, "%c%d.%.2d\n", sign,
				data >> AD7314_TEMP_FLOAT_OFFSET,
				(data & AD7314_TEMP_FLOAT_MASK) * 25);
	} else {
		data &= ADT7301_TEMP_MASK;
		if (data & ADT7301_TEMP_SIGN) {
			data = (ADT7301_TEMP_SIGN << 1) - data;
			sign = '-';
		}

		return sprintf(buf, "%c%d.%.5d\n", sign,
				data >> ADT7301_TEMP_FLOAT_OFFSET,
				(data & ADT7301_TEMP_FLOAT_MASK) * 3125);
	}
}

static IIO_DEVICE_ATTR(temperature, S_IRUGO, ad7314_show_temperature, NULL, 0);

static struct attribute *ad7314_attributes[] = {
	&iio_dev_attr_available_modes.dev_attr.attr,
	&iio_dev_attr_mode.dev_attr.attr,
	&iio_dev_attr_temperature.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad7314_attribute_group = {
	.attrs = ad7314_attributes,
};

static const struct iio_info ad7314_info = {
	.attrs = &ad7314_attribute_group,
	.driver_module = THIS_MODULE,
};
/*
 * device probe and remove
 */

static int __devinit ad7314_probe(struct spi_device *spi_dev)
{
	struct ad7314_chip_info *chip;
	struct iio_dev *indio_dev;
	int ret = 0;

	indio_dev = iio_allocate_device(sizeof(*chip));
	if (indio_dev == NULL) {
		ret = -ENOMEM;
		goto error_ret;
	}
	chip = iio_priv(indio_dev);
	/* this is only used for device removal purposes */
	dev_set_drvdata(&spi_dev->dev, chip);

	chip->spi_dev = spi_dev;

	indio_dev->name = spi_get_device_id(spi_dev)->name;
	indio_dev->dev.parent = &spi_dev->dev;
	indio_dev->info = &ad7314_info;

	ret = iio_device_register(indio_dev);
	if (ret)
		goto error_free_dev;

	dev_info(&spi_dev->dev, "%s temperature sensor registered.\n",
			 indio_dev->name);

	return 0;
error_free_dev:
	iio_free_device(indio_dev);
error_ret:
	return ret;
}

static int __devexit ad7314_remove(struct spi_device *spi_dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(&spi_dev->dev);

	dev_set_drvdata(&spi_dev->dev, NULL);
	iio_device_unregister(indio_dev);
	iio_free_device(indio_dev);

	return 0;
}

static const struct spi_device_id ad7314_id[] = {
	{ "adt7301", 0 },
	{ "adt7302", 0 },
	{ "ad7314", 0 },
	{}
};

static struct spi_driver ad7314_driver = {
	.driver = {
		.name = "ad7314",
		.bus = &spi_bus_type,
		.owner = THIS_MODULE,
	},
	.probe = ad7314_probe,
	.remove = __devexit_p(ad7314_remove),
	.id_table = ad7314_id,
};

static __init int ad7314_init(void)
{
	return spi_register_driver(&ad7314_driver);
}

static __exit void ad7314_exit(void)
{
	spi_unregister_driver(&ad7314_driver);
}

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7314, ADT7301 and ADT7302 digital"
			" temperature sensor driver");
MODULE_LICENSE("GPL v2");

module_init(ad7314_init);
module_exit(ad7314_exit);
