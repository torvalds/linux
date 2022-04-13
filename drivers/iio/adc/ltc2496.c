// SPDX-License-Identifier: GPL-2.0-only
/*
 * ltc2496.c - Driver for Analog Devices/Linear Technology LTC2496 ADC
 *
 * Based on ltc2497.c which has
 * Copyright (C) 2017 Analog Devices Inc.
 *
 * Licensed under the GPL-2.
 *
 * Datasheet: https://www.analog.com/media/en/technical-documentation/data-sheets/2496fc.pdf
 */

#include <linux/spi/spi.h>
#include <linux/iio/iio.h>
#include <linux/iio/driver.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>

#include "ltc2497.h"

struct ltc2496_driverdata {
	/* this must be the first member */
	struct ltc2497core_driverdata common_ddata;
	struct spi_device *spi;

	/*
	 * DMA (thus cache coherency maintenance) requires the
	 * transfer buffers to live in their own cache lines.
	 */
	unsigned char rxbuf[3] ____cacheline_aligned;
	unsigned char txbuf[3];
};

static int ltc2496_result_and_measure(struct ltc2497core_driverdata *ddata,
				      u8 address, int *val)
{
	struct ltc2496_driverdata *st =
		container_of(ddata, struct ltc2496_driverdata, common_ddata);
	struct spi_transfer t = {
		.tx_buf = st->txbuf,
		.rx_buf = st->rxbuf,
		.len = sizeof(st->txbuf),
	};
	int ret;

	st->txbuf[0] = LTC2497_ENABLE | address;

	ret = spi_sync_transfer(st->spi, &t, 1);
	if (ret < 0)  {
		dev_err(&st->spi->dev, "spi_sync_transfer failed: %pe\n",
			ERR_PTR(ret));
		return ret;
	}

	if (val)
		*val = ((st->rxbuf[0] & 0x3f) << 12 |
			st->rxbuf[1] << 4 | st->rxbuf[2] >> 4) -
			(1 << 17);

	return 0;
}

static int ltc2496_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ltc2496_driverdata *st;
	struct device *dev = &spi->dev;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	spi_set_drvdata(spi, indio_dev);
	st->spi = spi;
	st->common_ddata.result_and_measure = ltc2496_result_and_measure;

	return ltc2497core_probe(dev, indio_dev);
}

static void ltc2496_remove(struct spi_device *spi)
{
	struct iio_dev *indio_dev = spi_get_drvdata(spi);

	ltc2497core_remove(indio_dev);
}

static const struct of_device_id ltc2496_of_match[] = {
	{ .compatible = "lltc,ltc2496", },
	{},
};
MODULE_DEVICE_TABLE(of, ltc2496_of_match);

static struct spi_driver ltc2496_driver = {
	.driver = {
		.name = "ltc2496",
		.of_match_table = ltc2496_of_match,
	},
	.probe = ltc2496_probe,
	.remove = ltc2496_remove,
};
module_spi_driver(ltc2496_driver);

MODULE_AUTHOR("Uwe Kleine-König <u.kleine-könig@pengutronix.de>");
MODULE_DESCRIPTION("Linear Technology LTC2496 ADC driver");
MODULE_LICENSE("GPL v2");
