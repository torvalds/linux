// SPDX-License-Identifier: GPL-2.0+
/* SC16IS7xx SPI interface driver */

#include <linux/dev_printk.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>
#include <linux/string.h>
#include <linux/units.h>

#include "sc16is7xx.h"

/* SPI definitions */
#define SC16IS7XX_SPI_READ_BIT	BIT(7)

static int sc16is7xx_spi_probe(struct spi_device *spi)
{
	const struct sc16is7xx_devtype *devtype;
	struct regmap *regmaps[SC16IS7XX_MAX_PORTS];
	struct regmap_config regcfg;
	unsigned int i;
	int ret;

	/* Setup SPI bus */
	spi->bits_per_word	= 8;
	/* For all variants, only mode 0 is supported */
	if ((spi->mode & SPI_MODE_X_MASK) != SPI_MODE_0)
		return dev_err_probe(&spi->dev, -EINVAL, "Unsupported SPI mode\n");

	spi->mode		= spi->mode ? : SPI_MODE_0;
	spi->max_speed_hz	= spi->max_speed_hz ? : 4 * HZ_PER_MHZ;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	devtype = spi_get_device_match_data(spi);
	if (!devtype)
		return dev_err_probe(&spi->dev, -ENODEV, "Failed to match device\n");

	memcpy(&regcfg, &sc16is7xx_regcfg, sizeof(struct regmap_config));

	for (i = 0; i < devtype->nr_uart; i++) {
		regcfg.name = sc16is7xx_regmap_name(i);
		/*
		 * If read_flag_mask is 0, the regmap code sets it to a default
		 * of 0x80. Since we specify our own mask, we must add the READ
		 * bit ourselves:
		 */
		regcfg.read_flag_mask = sc16is7xx_regmap_port_mask(i) |
			SC16IS7XX_SPI_READ_BIT;
		regcfg.write_flag_mask = sc16is7xx_regmap_port_mask(i);
		regmaps[i] = devm_regmap_init_spi(spi, &regcfg);
	}

	return sc16is7xx_probe(&spi->dev, devtype, regmaps, spi->irq);
}

static void sc16is7xx_spi_remove(struct spi_device *spi)
{
	sc16is7xx_remove(&spi->dev);
}

static const struct spi_device_id sc16is7xx_spi_id_table[] = {
	{ "sc16is74x",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is740",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is741",	(kernel_ulong_t)&sc16is74x_devtype, },
	{ "sc16is750",	(kernel_ulong_t)&sc16is750_devtype, },
	{ "sc16is752",	(kernel_ulong_t)&sc16is752_devtype, },
	{ "sc16is760",	(kernel_ulong_t)&sc16is760_devtype, },
	{ "sc16is762",	(kernel_ulong_t)&sc16is762_devtype, },
	{ }
};
MODULE_DEVICE_TABLE(spi, sc16is7xx_spi_id_table);

static struct spi_driver sc16is7xx_spi_driver = {
	.driver = {
		.name		= SC16IS7XX_NAME,
		.of_match_table	= sc16is7xx_dt_ids,
	},
	.probe		= sc16is7xx_spi_probe,
	.remove		= sc16is7xx_spi_remove,
	.id_table	= sc16is7xx_spi_id_table,
};

module_spi_driver(sc16is7xx_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("SC16IS7xx SPI interface driver");
MODULE_IMPORT_NS("SERIAL_NXP_SC16IS7XX");
