// SPDX-License-Identifier: GPL-2.0
/*
 * AD7606 Parallel Interface ADC driver
 *
 * Copyright 2011 Analog Devices Inc.
 */

#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/gpio/consumer.h>
#include <linux/platform_device.h>
#include <linux/types.h>
#include <linux/err.h>
#include <linux/io.h>

#include <linux/iio/iio.h>
#include "ad7606.h"

static int ad7606_par16_read_block(struct device *dev,
				   int count, void *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7606_state *st = iio_priv(indio_dev);


	/*
	 * On the parallel interface, the frstdata signal is set to high while
	 * and after reading the sample of the first channel and low for all
	 * other channels.  This can be used to check that the incoming data is
	 * correctly aligned.  During normal operation the data should never
	 * become unaligned, but some glitch or electrostatic discharge might
	 * cause an extra read or clock cycle.  Monitoring the frstdata signal
	 * allows to recover from such failure situations.
	 */
	int num = count;
	u16 *_buf = buf;

	if (st->gpio_frstdata) {
		insw((unsigned long)st->base_address, _buf, 1);
		if (!gpiod_get_value(st->gpio_frstdata)) {
			ad7606_reset(st);
			return -EIO;
		}
		_buf++;
		num--;
	}
	insw((unsigned long)st->base_address, _buf, num);
	return 0;
}

static const struct ad7606_bus_ops ad7606_par16_bops = {
	.read_block = ad7606_par16_read_block,
};

static int ad7606_par8_read_block(struct device *dev,
				  int count, void *buf)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct ad7606_state *st = iio_priv(indio_dev);
	/*
	 * On the parallel interface, the frstdata signal is set to high while
	 * and after reading the sample of the first channel and low for all
	 * other channels.  This can be used to check that the incoming data is
	 * correctly aligned.  During normal operation the data should never
	 * become unaligned, but some glitch or electrostatic discharge might
	 * cause an extra read or clock cycle.  Monitoring the frstdata signal
	 * allows to recover from such failure situations.
	 */
	int num = count;
	u16 *_buf = buf;

	if (st->gpio_frstdata) {
		insb((unsigned long)st->base_address, _buf, 2);
		if (!gpiod_get_value(st->gpio_frstdata)) {
			ad7606_reset(st);
			return -EIO;
		}
		_buf++;
		num--;
	}
	insb((unsigned long)st->base_address, _buf, num * 2);

	return 0;
}

static const struct ad7606_bus_ops ad7606_par8_bops = {
	.read_block = ad7606_par8_read_block,
};

static int ad7606_par_probe(struct platform_device *pdev)
{
	const struct platform_device_id *id = platform_get_device_id(pdev);
	struct resource *res;
	void __iomem *addr;
	resource_size_t remap_size;
	int irq;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	addr = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	remap_size = resource_size(res);

	return ad7606_probe(&pdev->dev, irq, addr,
			    id->name, id->driver_data,
			    remap_size > 1 ? &ad7606_par16_bops :
			    &ad7606_par8_bops);
}

static const struct platform_device_id ad7606_driver_ids[] = {
	{ .name	= "ad7605-4", .driver_data = ID_AD7605_4, },
	{ .name	= "ad7606-4", .driver_data = ID_AD7606_4, },
	{ .name	= "ad7606-6", .driver_data = ID_AD7606_6, },
	{ .name	= "ad7606-8", .driver_data = ID_AD7606_8, },
	{ }
};
MODULE_DEVICE_TABLE(platform, ad7606_driver_ids);

static const struct of_device_id ad7606_of_match[] = {
	{ .compatible = "adi,ad7605-4" },
	{ .compatible = "adi,ad7606-4" },
	{ .compatible = "adi,ad7606-6" },
	{ .compatible = "adi,ad7606-8" },
	{ }
};
MODULE_DEVICE_TABLE(of, ad7606_of_match);

static struct platform_driver ad7606_driver = {
	.probe = ad7606_par_probe,
	.id_table = ad7606_driver_ids,
	.driver = {
		.name = "ad7606",
		.pm = AD7606_PM_OPS,
		.of_match_table = ad7606_of_match,
	},
};
module_platform_driver(ad7606_driver);

MODULE_AUTHOR("Michael Hennerich <michael.hennerich@analog.com>");
MODULE_DESCRIPTION("Analog Devices AD7606 ADC");
MODULE_LICENSE("GPL v2");
MODULE_IMPORT_NS(IIO_AD7606);
