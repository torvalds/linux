/*
 * STMicroelectronics hts221 spi driver
 *
 * Copyright 2016 STMicroelectronics Inc.
 *
 * Lorenzo Bianconi <lorenzo.bianconi@st.com>
 *
 * Licensed under the GPL-2.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/slab.h>
#include "hts221.h"

#define SENSORS_SPI_READ	0x80
#define SPI_AUTO_INCREMENT	0x40

static int hts221_spi_read(struct device *dev, u8 addr, int len, u8 *data)
{
	int err;
	struct spi_device *spi = to_spi_device(dev);
	struct iio_dev *iio_dev = spi_get_drvdata(spi);
	struct hts221_hw *hw = iio_priv(iio_dev);

	struct spi_transfer xfers[] = {
		{
			.tx_buf = hw->tb.tx_buf,
			.bits_per_word = 8,
			.len = 1,
		},
		{
			.rx_buf = hw->tb.rx_buf,
			.bits_per_word = 8,
			.len = len,
		}
	};

	if (len > 1)
		addr |= SPI_AUTO_INCREMENT;
	hw->tb.tx_buf[0] = addr | SENSORS_SPI_READ;

	err = spi_sync_transfer(spi, xfers,  ARRAY_SIZE(xfers));
	if (err < 0)
		return err;

	memcpy(data, hw->tb.rx_buf, len * sizeof(u8));

	return len;
}

static int hts221_spi_write(struct device *dev, u8 addr, int len, u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct iio_dev *iio_dev = spi_get_drvdata(spi);
	struct hts221_hw *hw = iio_priv(iio_dev);

	struct spi_transfer xfers = {
		.tx_buf = hw->tb.tx_buf,
		.bits_per_word = 8,
		.len = len + 1,
	};

	if (len >= HTS221_TX_MAX_LENGTH)
		return -ENOMEM;

	if (len > 1)
		addr |= SPI_AUTO_INCREMENT;
	hw->tb.tx_buf[0] = addr;
	memcpy(&hw->tb.tx_buf[1], data, len);

	return spi_sync_transfer(spi, &xfers, 1);
}

static const struct hts221_transfer_function hts221_transfer_fn = {
	.read = hts221_spi_read,
	.write = hts221_spi_write,
};

static int hts221_spi_probe(struct spi_device *spi)
{
	struct hts221_hw *hw;
	struct iio_dev *iio_dev;

	iio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*hw));
	if (!iio_dev)
		return -ENOMEM;

	spi_set_drvdata(spi, iio_dev);

	hw = iio_priv(iio_dev);
	hw->name = spi->modalias;
	hw->dev = &spi->dev;
	hw->irq = spi->irq;
	hw->tf = &hts221_transfer_fn;

	return hts221_probe(iio_dev);
}

static const struct of_device_id hts221_spi_of_match[] = {
	{ .compatible = "st,hts221", },
	{},
};
MODULE_DEVICE_TABLE(of, hts221_spi_of_match);

static const struct spi_device_id hts221_spi_id_table[] = {
	{ HTS221_DEV_NAME },
	{},
};
MODULE_DEVICE_TABLE(spi, hts221_spi_id_table);

static struct spi_driver hts221_driver = {
	.driver = {
		.name = "hts221_spi",
		.pm = &hts221_pm_ops,
		.of_match_table = of_match_ptr(hts221_spi_of_match),
	},
	.probe = hts221_spi_probe,
	.id_table = hts221_spi_id_table,
};
module_spi_driver(hts221_driver);

MODULE_AUTHOR("Lorenzo Bianconi <lorenzo.bianconi@st.com>");
MODULE_DESCRIPTION("STMicroelectronics hts221 spi driver");
MODULE_LICENSE("GPL v2");
