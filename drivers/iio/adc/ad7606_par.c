// SPDX-License-Identifier: GPL-2.0
/*
 * AD7606 Parallel Interface ADC driver
 *
 * Copyright 2011 - 2024 Analog Devices Inc.
 * Copyright 2024 BayLibre SAS.
 */

#include <linux/err.h>
#include <linux/gpio/consumer.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/property.h>
#include <linux/types.h>

#include <linux/iio/backend.h>
#include <linux/iio/iio.h>

#include "ad7606.h"
#include "ad7606_bus_iface.h"

static int ad7606_par_bus_update_scan_mode(struct iio_dev *indio_dev,
					   const unsigned long *scan_mask)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	unsigned int c, ret;

	for (c = 0; c < indio_dev->num_channels; c++) {
		if (test_bit(c, scan_mask))
			ret = iio_backend_chan_enable(st->back, c);
		else
			ret = iio_backend_chan_disable(st->back, c);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad7606_par_bus_setup_iio_backend(struct device *dev,
					    struct iio_dev *indio_dev)
{
	struct ad7606_state *st = iio_priv(indio_dev);
	unsigned int ret, c;
	struct iio_backend_data_fmt data = {
		.sign_extend = true,
		.enable = true,
	};

	st->back = devm_iio_backend_get(dev, NULL);
	if (IS_ERR(st->back))
		return PTR_ERR(st->back);

	/* If the device is iio_backend powered the PWM is mandatory */
	if (!st->cnvst_pwm)
		return dev_err_probe(st->dev, -EINVAL,
				     "A PWM is mandatory when using backend.\n");

	ret = devm_iio_backend_request_buffer(dev, st->back, indio_dev);
	if (ret)
		return ret;

	ret = devm_iio_backend_enable(dev, st->back);
	if (ret)
		return ret;

	for (c = 0; c < indio_dev->num_channels; c++) {
		ret = iio_backend_data_format_set(st->back, c, &data);
		if (ret)
			return ret;
	}

	return 0;
}

static int ad7606_par_bus_reg_read(struct ad7606_state *st, unsigned int addr)
{
	struct ad7606_platform_data *pdata = st->dev->platform_data;
	int val, ret;

	ret = pdata->bus_reg_read(st->back, addr, &val);
	if (ret)
		return ret;

	return val;
}

static int ad7606_par_bus_reg_write(struct ad7606_state *st, unsigned int addr,
				    unsigned int val)
{
	struct ad7606_platform_data *pdata = st->dev->platform_data;

	return pdata->bus_reg_write(st->back, addr, val);
}

static const struct ad7606_bus_ops ad7606_bi_bops = {
	.iio_backend_config = ad7606_par_bus_setup_iio_backend,
	.update_scan_mode = ad7606_par_bus_update_scan_mode,
	.reg_read = ad7606_par_bus_reg_read,
	.reg_write = ad7606_par_bus_reg_write,
};

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
	const struct ad7606_chip_info *chip_info;
	const struct platform_device_id *id;
	struct resource *res;
	void __iomem *addr;
	resource_size_t remap_size;
	int irq;

	/*
	 * If a firmware node is available (ACPI or DT), platform_device_id is null
	 * and we must use get_match_data.
	 */
	if (dev_fwnode(&pdev->dev)) {
		chip_info = device_get_match_data(&pdev->dev);
		if (device_property_present(&pdev->dev, "io-backends"))
			/*
			 * If a backend is available ,call the core probe with backend
			 * bops, otherwise use the former bops.
			 */
			return ad7606_probe(&pdev->dev, 0, NULL,
					    chip_info,
					    &ad7606_bi_bops);
	} else {
		id = platform_get_device_id(pdev);
		chip_info = (const struct ad7606_chip_info *)id->driver_data;
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	addr = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(addr))
		return PTR_ERR(addr);

	remap_size = resource_size(res);

	return ad7606_probe(&pdev->dev, irq, addr, chip_info,
			    remap_size > 1 ? &ad7606_par16_bops :
			    &ad7606_par8_bops);
}

static const struct platform_device_id ad7606_driver_ids[] = {
	{ .name	= "ad7605-4", .driver_data = (kernel_ulong_t)&ad7605_4_info, },
	{ .name	= "ad7606-4", .driver_data = (kernel_ulong_t)&ad7606_4_info, },
	{ .name	= "ad7606-6", .driver_data = (kernel_ulong_t)&ad7606_6_info, },
	{ .name	= "ad7606-8", .driver_data = (kernel_ulong_t)&ad7606_8_info, },
	{ .name	= "ad7606b", .driver_data = (kernel_ulong_t)&ad7606b_info, },
	{ .name = "ad7606c-16", .driver_data = (kernel_ulong_t)&ad7606c_16_info },
	{ .name = "ad7606c-18", .driver_data = (kernel_ulong_t)&ad7606c_18_info },
	{ .name	= "ad7607", .driver_data = (kernel_ulong_t)&ad7607_info, },
	{ .name	= "ad7608", .driver_data = (kernel_ulong_t)&ad7608_info, },
	{ .name	= "ad7609", .driver_data = (kernel_ulong_t)&ad7609_info, },
	{ }
};
MODULE_DEVICE_TABLE(platform, ad7606_driver_ids);

static const struct of_device_id ad7606_of_match[] = {
	{ .compatible = "adi,ad7605-4", .data = &ad7605_4_info },
	{ .compatible = "adi,ad7606-4", .data = &ad7606_4_info },
	{ .compatible = "adi,ad7606-6", .data = &ad7606_6_info },
	{ .compatible = "adi,ad7606-8", .data = &ad7606_8_info },
	{ .compatible = "adi,ad7606b", .data = &ad7606b_info },
	{ .compatible = "adi,ad7606c-16", .data = &ad7606c_16_info },
	{ .compatible = "adi,ad7606c-18", .data = &ad7606c_18_info },
	{ .compatible = "adi,ad7607", .data = &ad7607_info },
	{ .compatible = "adi,ad7608", .data = &ad7608_info },
	{ .compatible = "adi,ad7609", .data = &ad7609_info },
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
MODULE_IMPORT_NS("IIO_AD7606");
MODULE_IMPORT_NS("IIO_BACKEND");
