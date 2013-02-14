/*
 * AD714X CapTouch Programmable Controller driver (SPI bus)
 *
 * Copyright 2009-2011 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_SPI */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/pm.h>
#include <linux/types.h>
#include "ad714x.h"

#define AD714x_SPI_CMD_PREFIX      0xE000   /* bits 15:11 */
#define AD714x_SPI_READ            BIT(10)

#ifdef CONFIG_PM
static int ad714x_spi_suspend(struct device *dev)
{
	return ad714x_disable(spi_get_drvdata(to_spi_device(dev)));
}

static int ad714x_spi_resume(struct device *dev)
{
	return ad714x_enable(spi_get_drvdata(to_spi_device(dev)));
}
#endif

static SIMPLE_DEV_PM_OPS(ad714x_spi_pm, ad714x_spi_suspend, ad714x_spi_resume);

static int ad714x_spi_read(struct ad714x_chip *chip,
			   unsigned short reg, unsigned short *data, size_t len)
{
	struct spi_device *spi = to_spi_device(chip->dev);
	struct spi_message message;
	struct spi_transfer xfer[2];
	int i;
	int error;

	spi_message_init(&message);
	memset(xfer, 0, sizeof(xfer));

	chip->xfer_buf[0] = cpu_to_be16(AD714x_SPI_CMD_PREFIX |
					AD714x_SPI_READ | reg);
	xfer[0].tx_buf = &chip->xfer_buf[0];
	xfer[0].len = sizeof(chip->xfer_buf[0]);
	spi_message_add_tail(&xfer[0], &message);

	xfer[1].rx_buf = &chip->xfer_buf[1];
	xfer[1].len = sizeof(chip->xfer_buf[1]) * len;
	spi_message_add_tail(&xfer[1], &message);

	error = spi_sync(spi, &message);
	if (unlikely(error)) {
		dev_err(chip->dev, "SPI read error: %d\n", error);
		return error;
	}

	for (i = 0; i < len; i++)
		data[i] = be16_to_cpu(chip->xfer_buf[i + 1]);

	return 0;
}

static int ad714x_spi_write(struct ad714x_chip *chip,
			    unsigned short reg, unsigned short data)
{
	struct spi_device *spi = to_spi_device(chip->dev);
	int error;

	chip->xfer_buf[0] = cpu_to_be16(AD714x_SPI_CMD_PREFIX | reg);
	chip->xfer_buf[1] = cpu_to_be16(data);

	error = spi_write(spi, (u8 *)chip->xfer_buf,
			  2 * sizeof(*chip->xfer_buf));
	if (unlikely(error)) {
		dev_err(chip->dev, "SPI write error: %d\n", error);
		return error;
	}

	return 0;
}

static int ad714x_spi_probe(struct spi_device *spi)
{
	struct ad714x_chip *chip;
	int err;

	spi->bits_per_word = 8;
	err = spi_setup(spi);
	if (err < 0)
		return err;

	chip = ad714x_probe(&spi->dev, BUS_SPI, spi->irq,
			    ad714x_spi_read, ad714x_spi_write);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	spi_set_drvdata(spi, chip);

	return 0;
}

static int ad714x_spi_remove(struct spi_device *spi)
{
	struct ad714x_chip *chip = spi_get_drvdata(spi);

	ad714x_remove(chip);
	spi_set_drvdata(spi, NULL);

	return 0;
}

static struct spi_driver ad714x_spi_driver = {
	.driver = {
		.name	= "ad714x_captouch",
		.owner	= THIS_MODULE,
		.pm	= &ad714x_spi_pm,
	},
	.probe		= ad714x_spi_probe,
	.remove		= ad714x_spi_remove,
};

module_spi_driver(ad714x_spi_driver);

MODULE_DESCRIPTION("Analog Devices AD714X Capacitance Touch Sensor SPI Bus Driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
