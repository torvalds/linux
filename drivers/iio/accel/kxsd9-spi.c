// SPDX-License-Identifier: GPL-2.0-only
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/spi/spi.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/regmap.h>

#include "kxsd9.h"

static int kxsd9_spi_probe(struct spi_device *spi)
{
	static const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.max_register = 0x0e,
	};
	struct regmap *regmap;

	spi->mode = SPI_MODE_0;
	regmap = devm_regmap_init_spi(spi, &config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "%s: regmap allocation failed: %ld\n",
			__func__, PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return kxsd9_common_probe(&spi->dev,
				  regmap,
				  spi_get_device_id(spi)->name);
}

static int kxsd9_spi_remove(struct spi_device *spi)
{
	return kxsd9_common_remove(&spi->dev);
}

static const struct spi_device_id kxsd9_spi_id[] = {
	{"kxsd9", 0},
	{ },
};
MODULE_DEVICE_TABLE(spi, kxsd9_spi_id);

static struct spi_driver kxsd9_spi_driver = {
	.driver = {
		.name = "kxsd9",
		.pm = &kxsd9_dev_pm_ops,
	},
	.probe = kxsd9_spi_probe,
	.remove = kxsd9_spi_remove,
	.id_table = kxsd9_spi_id,
};
module_spi_driver(kxsd9_spi_driver);

MODULE_AUTHOR("Jonathan Cameron <jic23@kernel.org>");
MODULE_DESCRIPTION("Kionix KXSD9 SPI driver");
MODULE_LICENSE("GPL v2");
