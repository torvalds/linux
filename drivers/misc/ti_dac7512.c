/*
 *  dac7512.c - Linux kernel module for
 * 	Texas Instruments DAC7512
 *
 *  Copyright (c) 2009 Daniel Mack <daniel@caiaq.de>
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
#include <linux/spi/spi.h>

#define DAC7512_DRV_NAME	"dac7512"
#define DRIVER_VERSION		"1.0"

static ssize_t dac7512_store_val(struct device *dev,
				 struct device_attribute *attr,
				 const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char tmp[2];
	unsigned long val;
	int ret;

	ret = kstrtoul(buf, 10, &val);
	if (ret)
		return ret;

	tmp[0] = val >> 8;
	tmp[1] = val & 0xff;
	spi_write(spi, tmp, sizeof(tmp));
	return count;
}

static DEVICE_ATTR(value, S_IWUSR, NULL, dac7512_store_val);

static struct attribute *dac7512_attributes[] = {
	&dev_attr_value.attr,
	NULL
};

static const struct attribute_group dac7512_attr_group = {
	.attrs = dac7512_attributes,
};

static int dac7512_probe(struct spi_device *spi)
{
	int ret;

	spi->bits_per_word = 8;
	spi->mode = SPI_MODE_0;
	ret = spi_setup(spi);
	if (ret < 0)
		return ret;

	return sysfs_create_group(&spi->dev.kobj, &dac7512_attr_group);
}

static int dac7512_remove(struct spi_device *spi)
{
	sysfs_remove_group(&spi->dev.kobj, &dac7512_attr_group);
	return 0;
}

static struct spi_driver dac7512_driver = {
	.driver = {
		.name	= DAC7512_DRV_NAME,
		.owner	= THIS_MODULE,
	},
	.probe	= dac7512_probe,
	.remove	= dac7512_remove,
};

module_spi_driver(dac7512_driver);

MODULE_AUTHOR("Daniel Mack <daniel@caiaq.de>");
MODULE_DESCRIPTION("DAC7512 16-bit DAC");
MODULE_LICENSE("GPL v2");
MODULE_VERSION(DRIVER_VERSION);
