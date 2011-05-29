/*
 * AD714X CapTouch Programmable Controller driver (SPI bus)
 *
 * Copyright 2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/input.h>	/* BUS_I2C */
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

static int ad714x_spi_read(struct device *dev, unsigned short reg,
		unsigned short *data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned short tx = AD714x_SPI_CMD_PREFIX | AD714x_SPI_READ | reg;

	return spi_write_then_read(spi, (u8 *)&tx, 2, (u8 *)data, 2);
}

static int ad714x_spi_write(struct device *dev, unsigned short reg,
		unsigned short data)
{
	struct spi_device *spi = to_spi_device(dev);
	unsigned short tx[2] = {
		AD714x_SPI_CMD_PREFIX | reg,
		data
	};

	return spi_write(spi, (u8 *)tx, 4);
}

static int __devinit ad714x_spi_probe(struct spi_device *spi)
{
	struct ad714x_chip *chip;

	chip = ad714x_probe(&spi->dev, BUS_SPI, spi->irq,
			    ad714x_spi_read, ad714x_spi_write);
	if (IS_ERR(chip))
		return PTR_ERR(chip);

	spi_set_drvdata(spi, chip);

	return 0;
}

static int __devexit ad714x_spi_remove(struct spi_device *spi)
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
	.remove		= __devexit_p(ad714x_spi_remove),
};

static __init int ad714x_spi_init(void)
{
	return spi_register_driver(&ad714x_spi_driver);
}
module_init(ad714x_spi_init);

static __exit void ad714x_spi_exit(void)
{
	spi_unregister_driver(&ad714x_spi_driver);
}
module_exit(ad714x_spi_exit);

MODULE_DESCRIPTION("Analog Devices AD714X Capacitance Touch Sensor SPI Bus Driver");
MODULE_AUTHOR("Barry Song <21cnbao@gmail.com>");
MODULE_LICENSE("GPL");
