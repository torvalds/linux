// SPDX-License-Identifier: GPL-2.0-only
/*
 * DRM driver for Solomon SSD130X OLED displays (SPI bus)
 *
 * Copyright 2022 Red Hat Inc.
 * Authors: Javier Martinez Canillas <javierm@redhat.com>
 */
#include <linux/spi/spi.h>
#include <linux/module.h>

#include "ssd130x.h"

#define DRIVER_NAME	"ssd130x-spi"
#define DRIVER_DESC	"DRM driver for Solomon SSD130X OLED displays (SPI)"

struct ssd130x_spi_transport {
	struct spi_device *spi;
	struct gpio_desc *dc;
};

static const struct regmap_config ssd130x_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/*
 * The regmap bus .write handler, it is just a wrapper around spi_write()
 * but toggling the Data/Command control pin (D/C#). Since for 4-wire SPI
 * a D/C# pin is used, in contrast with I2C where a control byte is sent,
 * prior to every data byte, that contains a bit with the D/C# value.
 *
 * These control bytes are considered registers by the ssd130x core driver
 * and can be used by the ssd130x SPI driver to determine if the data sent
 * is for a command register or for the Graphic Display Data RAM (GDDRAM).
 */
static int ssd130x_spi_write(void *context, const void *data, size_t count)
{
	struct ssd130x_spi_transport *t = context;
	struct spi_device *spi = t->spi;
	const u8 *reg = data;

	if (*reg == SSD130X_COMMAND)
		gpiod_set_value_cansleep(t->dc, 0);

	if (*reg == SSD130X_DATA)
		gpiod_set_value_cansleep(t->dc, 1);

	/* Remove control byte since is not used in a 4-wire SPI interface */
	return spi_write(spi, reg + 1, count - 1);
}

/* The ssd130x driver does not read registers but regmap expects a .read */
static int ssd130x_spi_read(void *context, const void *reg, size_t reg_size,
			    void *val, size_t val_size)
{
	return -EOPNOTSUPP;
}

/*
 * A custom bus is needed due the special write that toggles a D/C# pin,
 * another option could be to just have a .reg_write() callback but that
 * will prevent to do data writes in bulk.
 *
 * Once the regmap API is extended to support defining a bulk write handler
 * in the struct regmap_config, this can be simplified and the bus dropped.
 */
static struct regmap_bus regmap_ssd130x_spi_bus = {
	.write = ssd130x_spi_write,
	.read = ssd130x_spi_read,
};

static int ssd130x_spi_probe(struct spi_device *spi)
{
	struct ssd130x_spi_transport *t;
	struct ssd130x_device *ssd130x;
	struct regmap *regmap;
	struct gpio_desc *dc;
	struct device *dev = &spi->dev;

	dc = devm_gpiod_get(dev, "dc", GPIOD_OUT_LOW);
	if (IS_ERR(dc))
		return dev_err_probe(dev, PTR_ERR(dc),
				     "Failed to get dc gpio\n");

	t = devm_kzalloc(dev, sizeof(*t), GFP_KERNEL);
	if (!t)
		return dev_err_probe(dev, -ENOMEM,
				     "Failed to allocate SPI transport data\n");

	t->spi = spi;
	t->dc = dc;

	regmap = devm_regmap_init(dev, &regmap_ssd130x_spi_bus, t,
				  &ssd130x_spi_regmap_config);
	if (IS_ERR(regmap))
		return PTR_ERR(regmap);

	ssd130x = ssd130x_probe(dev, regmap);
	if (IS_ERR(ssd130x))
		return PTR_ERR(ssd130x);

	spi_set_drvdata(spi, ssd130x);

	return 0;
}

static void ssd130x_spi_remove(struct spi_device *spi)
{
	struct ssd130x_device *ssd130x = spi_get_drvdata(spi);

	ssd130x_remove(ssd130x);
}

static void ssd130x_spi_shutdown(struct spi_device *spi)
{
	struct ssd130x_device *ssd130x = spi_get_drvdata(spi);

	ssd130x_shutdown(ssd130x);
}

static const struct of_device_id ssd130x_of_match[] = {
	{
		.compatible = "sinowealth,sh1106",
		.data = &ssd130x_variants[SH1106_ID],
	},
	{
		.compatible = "solomon,ssd1305",
		.data = &ssd130x_variants[SSD1305_ID],
	},
	{
		.compatible = "solomon,ssd1306",
		.data = &ssd130x_variants[SSD1306_ID],
	},
	{
		.compatible = "solomon,ssd1307",
		.data = &ssd130x_variants[SSD1307_ID],
	},
	{
		.compatible = "solomon,ssd1309",
		.data = &ssd130x_variants[SSD1309_ID],
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ssd130x_of_match);

/*
 * The SPI core always reports a MODALIAS uevent of the form "spi:<dev>", even
 * if the device was registered via OF. This means that the module will not be
 * auto loaded, unless it contains an alias that matches the MODALIAS reported.
 *
 * To workaround this issue, add a SPI device ID table. Even when this should
 * not be needed for this driver to match the registered SPI devices.
 */
static const struct spi_device_id ssd130x_spi_table[] = {
	{ "sh1106",  SH1106_ID },
	{ "ssd1305", SSD1305_ID },
	{ "ssd1306", SSD1306_ID },
	{ "ssd1307", SSD1307_ID },
	{ "ssd1309", SSD1309_ID },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, ssd130x_spi_table);

static struct spi_driver ssd130x_spi_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.of_match_table = ssd130x_of_match,
	},
	.probe = ssd130x_spi_probe,
	.remove = ssd130x_spi_remove,
	.shutdown = ssd130x_spi_shutdown,
};
module_spi_driver(ssd130x_spi_driver);

MODULE_DESCRIPTION(DRIVER_DESC);
MODULE_AUTHOR("Javier Martinez Canillas <javierm@redhat.com>");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(DRM_SSD130X);
