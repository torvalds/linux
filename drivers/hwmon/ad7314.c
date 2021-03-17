// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * AD7314 digital temperature sensor driver for AD7314, ADT7301 and ADT7302
 *
 * Copyright 2010 Analog Devices Inc.
 *
 * Conversion to hwmon from IIO done by Jonathan Cameron <jic23@cam.ac.uk>
 */
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/sysfs.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/hwmon-sysfs.h>
#include <linux/bitops.h>

/*
 * AD7314 temperature masks
 */
#define AD7314_TEMP_MASK		0x7FE0
#define AD7314_TEMP_SHIFT		5

/*
 * ADT7301 and ADT7302 temperature masks
 */
#define ADT7301_TEMP_MASK		0x3FFF

enum ad7314_variant {
	adt7301,
	adt7302,
	ad7314,
};

struct ad7314_data {
	struct spi_device	*spi_dev;
	u16 rx ____cacheline_aligned;
};

static int ad7314_spi_read(struct ad7314_data *chip)
{
	int ret;

	ret = spi_read(chip->spi_dev, (u8 *)&chip->rx, sizeof(chip->rx));
	if (ret < 0) {
		dev_err(&chip->spi_dev->dev, "SPI read error\n");
		return ret;
	}

	return be16_to_cpu(chip->rx);
}

static ssize_t ad7314_temperature_show(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct ad7314_data *chip = dev_get_drvdata(dev);
	s16 data;
	int ret;

	ret = ad7314_spi_read(chip);
	if (ret < 0)
		return ret;
	switch (spi_get_device_id(chip->spi_dev)->driver_data) {
	case ad7314:
		data = (ret & AD7314_TEMP_MASK) >> AD7314_TEMP_SHIFT;
		data = sign_extend32(data, 9);

		return sprintf(buf, "%d\n", 250 * data);
	case adt7301:
	case adt7302:
		/*
		 * Documented as a 13 bit twos complement register
		 * with a sign bit - which is a 14 bit 2's complement
		 * register.  1lsb - 31.25 milli degrees centigrade
		 */
		data = ret & ADT7301_TEMP_MASK;
		data = sign_extend32(data, 13);

		return sprintf(buf, "%d\n",
			       DIV_ROUND_CLOSEST(data * 3125, 100));
	default:
		return -EINVAL;
	}
}

static SENSOR_DEVICE_ATTR_RO(temp1_input, ad7314_temperature, 0);

static struct attribute *ad7314_attrs[] = {
	&sensor_dev_attr_temp1_input.dev_attr.attr,
	NULL,
};

ATTRIBUTE_GROUPS(ad7314);

static int ad7314_probe(struct spi_device *spi_dev)
{
	struct ad7314_data *chip;
	struct device *hwmon_dev;

	chip = devm_kzalloc(&spi_dev->dev, sizeof(*chip), GFP_KERNEL);
	if (chip == NULL)
		return -ENOMEM;

	chip->spi_dev = spi_dev;
	hwmon_dev = devm_hwmon_device_register_with_groups(&spi_dev->dev,
							   spi_dev->modalias,
							   chip, ad7314_groups);
	return PTR_ERR_OR_ZERO(hwmon_dev);
}

static const struct spi_device_id ad7314_id[] = {
	{ "adt7301", adt7301 },
	{ "adt7302", adt7302 },
	{ "ad7314", ad7314 },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad7314_id);

static struct spi_driver ad7314_driver = {
	.driver = {
		.name = "ad7314",
	},
	.probe = ad7314_probe,
	.id_table = ad7314_id,
};

module_spi_driver(ad7314_driver);

MODULE_AUTHOR("Sonic Zhang <sonic.zhang@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7314, ADT7301 and ADT7302 digital temperature sensor driver");
MODULE_LICENSE("GPL v2");
