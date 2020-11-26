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
	/*
	 * FIXME: implement reading DCS commands over SPI so we can
	 * properly identify which physical panel is connected.
	 */
	*data = 0;

	return 0;
}

static int s6e63m0_spi_write_word(struct device *dev, u16 data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_transfer xfer = {
		.len	= 2,
		.tx_buf = &data,
	};
	struct spi_message msg;

	spi_message_init(&msg);
	spi_message_add_tail(&xfer, &msg);

	return spi_sync(spi, &msg);
}

static int s6e63m0_spi_dcs_write(struct device *dev, const u8 *data, size_t len)
{
	int ret = 0;

	dev_dbg(dev, "SPI writing dcs seq: %*ph\n", (int)len, data);
	ret = s6e63m0_spi_write_word(dev, *data);

	while (!ret && --len) {
		++data;
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
	spi->mode = SPI_MODE_3;
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
