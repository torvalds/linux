// SPDX-License-Identifier: GPL-2.0
/*
 * Generic sigma delta modulator driver
 *
 * Copyright (C) 2017, STMicroelectronics - All Rights Reserved
 * Author: Arnaud Pouliquen <arnaud.pouliquen@st.com>.
 */

#include <linux/iio/iio.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/module.h>
#include <linux/of_device.h>

static const struct iio_info iio_sd_mod_iio_info;

static const struct iio_chan_spec iio_sd_mod_ch = {
	.type = IIO_VOLTAGE,
	.indexed = 1,
	.scan_type = {
		.sign = 'u',
		.realbits = 1,
		.shift = 0,
	},
};

static int iio_sd_mod_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct iio_dev *iio;

	iio = devm_iio_device_alloc(dev, 0);
	if (!iio)
		return -ENOMEM;

	iio->dev.parent = dev;
	iio->dev.of_node = dev->of_node;
	iio->name = dev_name(dev);
	iio->info = &iio_sd_mod_iio_info;
	iio->modes = INDIO_BUFFER_HARDWARE;

	iio->num_channels = 1;
	iio->channels = &iio_sd_mod_ch;

	platform_set_drvdata(pdev, iio);

	return devm_iio_device_register(&pdev->dev, iio);
}

static const struct of_device_id sd_adc_of_match[] = {
	{ .compatible = "sd-modulator" },
	{ .compatible = "ads1201" },
	{ }
};
MODULE_DEVICE_TABLE(of, sd_adc_of_match);

static struct platform_driver iio_sd_mod_adc = {
	.driver = {
		.name = "iio_sd_adc_mod",
		.of_match_table = of_match_ptr(sd_adc_of_match),
	},
	.probe = iio_sd_mod_probe,
};

module_platform_driver(iio_sd_mod_adc);

MODULE_DESCRIPTION("Basic sigma delta modulator");
MODULE_AUTHOR("Arnaud Pouliquen <arnaud.pouliquen@st.com>");
MODULE_LICENSE("GPL v2");
