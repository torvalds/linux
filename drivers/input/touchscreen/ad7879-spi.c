/*
 * AD7879/AD7889 touchscreen (SPI bus)
 *
 * Copyright (C) 2008-2010 Michael Hennerich, Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_SPI */
#include <linux/pm.h>
#include <linux/spi/spi.h>
#include <linux/module.h>

#include "ad7879.h"

#define AD7879_DEVID		0x7A	/* AD7879/AD7889 */

#define MAX_SPI_FREQ_HZ      5000000
#define AD7879_CMD_MAGIC     0xE000
#define AD7879_CMD_READ      (1 << 10)
#define AD7879_CMD(reg)      (AD7879_CMD_MAGIC | ((reg) & 0xF))
#define AD7879_WRITECMD(reg) (AD7879_CMD(reg))
#define AD7879_READCMD(reg)  (AD7879_CMD(reg) | AD7879_CMD_READ)

/*
 * ad7879_read/write are only used for initial setup and for sysfs controls.
 * The main traffic is done in ad7879_collect().
 */

static int ad7879_spi_xfer(struct spi_device *spi,
			   u16 cmd, u8 count, u16 *tx_buf, u16 *rx_buf)
{
	struct spi_message msg;
	struct spi_transfer *xfers;
	void *spi_data;
	u16 *command;
	u16 *_rx_buf = _rx_buf; /* shut gcc up */
	u8 idx;
	int ret;

	xfers = spi_data = kzalloc(sizeof(*xfers) * (count + 2), GFP_KERNEL);
	if (!spi_data)
		return -ENOMEM;

	spi_message_init(&msg);

	command = spi_data;
	command[0] = cmd;
	if (count == 1) {
		/* ad7879_spi_{read,write} gave us buf on stack */
		command[1] = *tx_buf;
		tx_buf = &command[1];
		_rx_buf = rx_buf;
		rx_buf = &command[2];
	}

	++xfers;
	xfers[0].tx_buf = command;
	xfers[0].len = 2;
	spi_message_add_tail(&xfers[0], &msg);
	++xfers;

	for (idx = 0; idx < count; ++idx) {
		if (rx_buf)
			xfers[idx].rx_buf = &rx_buf[idx];
		if (tx_buf)
			xfers[idx].tx_buf = &tx_buf[idx];
		xfers[idx].len = 2;
		spi_message_add_tail(&xfers[idx], &msg);
	}

	ret = spi_sync(spi, &msg);

	if (count == 1)
		_rx_buf[0] = command[2];

	kfree(spi_data);

	return ret;
}

static int ad7879_spi_multi_read(struct device *dev,
				 u8 first_reg, u8 count, u16 *buf)
{
	struct spi_device *spi = to_spi_device(dev);

	return ad7879_spi_xfer(spi, AD7879_READCMD(first_reg), count, NULL, buf);
}

static int ad7879_spi_read(struct device *dev, u8 reg)
{
	struct spi_device *spi = to_spi_device(dev);
	u16 ret, dummy;

	return ad7879_spi_xfer(spi, AD7879_READCMD(reg), 1, &dummy, &ret) ? : ret;
}

static int ad7879_spi_write(struct device *dev, u8 reg, u16 val)
{
	struct spi_device *spi = to_spi_device(dev);
	u16 dummy;

	return ad7879_spi_xfer(spi, AD7879_WRITECMD(reg), 1, &val, &dummy);
}

static const struct ad7879_bus_ops ad7879_spi_bus_ops = {
	.bustype	= BUS_SPI,
	.read		= ad7879_spi_read,
	.multi_read	= ad7879_spi_multi_read,
	.write		= ad7879_spi_write,
};

static int __devinit ad7879_spi_probe(struct spi_device *spi)
{
	struct ad7879 *ts;
	int err;

	/* don't exceed max specified SPI CLK frequency */
	if (spi->max_speed_hz > MAX_SPI_FREQ_HZ) {
		dev_err(&spi->dev, "SPI CLK %d Hz?\n", spi->max_speed_hz);
		return -EINVAL;
	}

	spi->bits_per_word = 16;
	err = spi_setup(spi);
	if (err) {
	        dev_dbg(&spi->dev, "spi master doesn't support 16 bits/word\n");
	        return err;
	}

	ts = ad7879_probe(&spi->dev, AD7879_DEVID, spi->irq, &ad7879_spi_bus_ops);
	if (IS_ERR(ts))
		return PTR_ERR(ts);

	spi_set_drvdata(spi, ts);

	return 0;
}

static int __devexit ad7879_spi_remove(struct spi_device *spi)
{
	struct ad7879 *ts = spi_get_drvdata(spi);

	ad7879_remove(ts);
	spi_set_drvdata(spi, NULL);

	return 0;
}

static struct spi_driver ad7879_spi_driver = {
	.driver = {
		.name	= "ad7879",
		.owner	= THIS_MODULE,
		.pm	= &ad7879_pm_ops,
	},
	.probe		= ad7879_spi_probe,
	.remove		= ad7879_spi_remove,
};

module_spi_driver(ad7879_spi_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("AD7879(-1) touchscreen SPI bus driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:ad7879");
