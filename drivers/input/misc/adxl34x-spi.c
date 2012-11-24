/*
 * ADLX345/346 Three-Axis Digital Accelerometers (SPI Interface)
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2009 Michael Hennerich, Analog Devices Inc.
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_SPI */
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/pm.h>
#include <linux/types.h>
#include "adxl34x.h"

#define MAX_SPI_FREQ_HZ		5000000
#define MAX_FREQ_NO_FIFODELAY	1500000
#define ADXL34X_CMD_MULTB	(1 << 6)
#define ADXL34X_CMD_READ	(1 << 7)
#define ADXL34X_WRITECMD(reg)	(reg & 0x3F)
#define ADXL34X_READCMD(reg)	(ADXL34X_CMD_READ | (reg & 0x3F))
#define ADXL34X_READMB_CMD(reg) (ADXL34X_CMD_READ | ADXL34X_CMD_MULTB \
					| (reg & 0x3F))

static int adxl34x_spi_read(struct device *dev, unsigned char reg)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char cmd;

	cmd = ADXL34X_READCMD(reg);

	return spi_w8r8(spi, cmd);
}

static int adxl34x_spi_write(struct device *dev,
			     unsigned char reg, unsigned char val)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned char buf[2];

	buf[0] = ADXL34X_WRITECMD(reg);
	buf[1] = val;

	return spi_write(spi, buf, sizeof(buf));
}

static int adxl34x_spi_read_block(struct device *dev,
				  unsigned char reg, int count,
				  void *buf)
{
	struct spi_device *spi = to_spi_device(dev);
	ssize_t status;

	reg = ADXL34X_READMB_CMD(reg);
	status = spi_write_then_read(spi, &reg, 1, buf, count);

	return (status < 0) ? status : 0;
}

static const struct adxl34x_bus_ops adxl34x_spi_bops = {
	.bustype	= BUS_SPI,
	.write		= adxl34x_spi_write,
	.read		= adxl34x_spi_read,
	.read_block	= adxl34x_spi_read_block,
};

static int __devinit adxl34x_spi_probe(struct spi_device *spi)
{
	struct adxl34x *ac;

	/* don't exceed max specified SPI CLK frequency */
	if (spi->max_speed_hz > MAX_SPI_FREQ_HZ) {
		dev_err(&spi->dev, "SPI CLK %d Hz too fast\n", spi->max_speed_hz);
		return -EINVAL;
	}

	ac = adxl34x_probe(&spi->dev, spi->irq,
			   spi->max_speed_hz > MAX_FREQ_NO_FIFODELAY,
			   &adxl34x_spi_bops);

	if (IS_ERR(ac))
		return PTR_ERR(ac);

	spi_set_drvdata(spi, ac);

	return 0;
}

static int __devexit adxl34x_spi_remove(struct spi_device *spi)
{
	struct adxl34x *ac = dev_get_drvdata(&spi->dev);

	return adxl34x_remove(ac);
}

#ifdef CONFIG_PM
static int adxl34x_spi_suspend(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct adxl34x *ac = dev_get_drvdata(&spi->dev);

	adxl34x_suspend(ac);

	return 0;
}

static int adxl34x_spi_resume(struct device *dev)
{
	struct spi_device *spi = to_spi_device(dev);
	struct adxl34x *ac = dev_get_drvdata(&spi->dev);

	adxl34x_resume(ac);

	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(adxl34x_spi_pm, adxl34x_spi_suspend,
			 adxl34x_spi_resume);

static struct spi_driver adxl34x_driver = {
	.driver = {
		.name = "adxl34x",
		.owner = THIS_MODULE,
		.pm = &adxl34x_spi_pm,
	},
	.probe   = adxl34x_spi_probe,
	.remove  = adxl34x_spi_remove,
};

module_spi_driver(adxl34x_driver);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADXL345/346 Three-Axis Digital Accelerometer SPI Bus Driver");
MODULE_LICENSE("GPL");
