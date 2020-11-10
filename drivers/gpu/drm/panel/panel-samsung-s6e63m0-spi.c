// SPDX-License-Identifier: GPL-2.0

#include <linux/device.h>
#include <linux/module.h>
#include <linux/spi/spi.h>
#include <linux/delay.h>

#include <drm/drm_print.h>

#include "panel-samsung-s6e63m0.h"

#define DATA_MASK	0x100

static int s6e63m0_spi_dcs_read(struct device *dev, const u8 cmd, u8 *data)
{
	struct spi_device *spi = to_spi_device(dev);
	u16 buf[1];
	u16 rbuf[1];
	int ret;

	/* SPI buffers are always in CPU order */
	buf[0] = (u16)cmd;
	ret = spi_write_then_read(spi, buf, 2, rbuf, 2);
	dev_dbg(dev, "READ CMD: %04x RET: %04x\n", buf[0], rbuf[0]);
	if (!ret)
		/* These high 8 bits of the 9 contains the readout */
		*data = (rbuf[0] & 0x1ff) >> 1;

	return ret;
}

static int s6e63m0_spi_write_word(struct device *dev, u16 data)
{
	struct spi_device *spi = to_spi_device(dev);

	/* SPI buffers are always in CPU order */
	return spi_write(spi, &data, 2);
}

static int s6e63m0_spi_dcs_write(struct device *dev, const u8 *data, size_t len)
{
	int ret = 0;

	dev_dbg(dev, "SPI writing dcs seq: %*ph\n", (int)len, data);

	/*
	 * This sends 9 bits with the first bit (bit 8) set to 0
	 * This indicates that this is a command. Anything after the
	 * command is data.
	 */
	ret = s6e63m0_spi_write_word(dev, *data);

	while (!ret && --len) {
		++data;
		/* This sends 9 bits with the first bit (bit 8) set to 1 */
		ret = s6e63m0_spi_write_word(dev, *data | DATA_MASK);
	}

	if (ret) {
		dev_err(dev, "SPI error %d writing dcs seq: %*ph\n", ret,
			(int)len, data);
	}

	usleep_range(300, 310);

	return ret;
}

static int s6e63m0_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	int ret;

	spi->bits_per_word = 9;
	/* Preserve e.g. SPI_3WIRE setting */
	spi->mode |= SPI_MODE_3;
	ret = spi_setup(spi);
	if (ret < 0) {
		dev_err(dev, "spi setup failed.\n");
		return ret;
	}
	return s6e63m0_probe(dev, s6e63m0_spi_dcs_read, s6e63m0_spi_dcs_write,
			     false);
}

static int s6e63m0_spi_remove(struct spi_device *spi)
{
	return s6e63m0_remove(&spi->dev);
}

static const struct of_device_id s6e63m0_spi_of_match[] = {
	{ .compatible = "samsung,s6e63m0" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, s6e63m0_spi_of_match);

static struct spi_driver s6e63m0_spi_driver = {
	.probe			= s6e63m0_spi_probe,
	.remove			= s6e63m0_spi_remove,
	.driver			= {
		.name		= "panel-samsung-s6e63m0",
		.of_match_table = s6e63m0_spi_of_match,
	},
};
module_spi_driver(s6e63m0_spi_driver);

MODULE_AUTHOR("Paweł Chmiel <pawel.mikolaj.chmiel@gmail.com>");
MODULE_DESCRIPTION("s6e63m0 LCD SPI Driver");
MODULE_LICENSE("GPL v2");
