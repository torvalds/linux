/*
 *  Copyright (C) 2014 Altera Corporation
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * SPI access for Altera MAX5 Arria10 System Control
 * Adapted from DA9052
 */

#include <linux/device.h>
#include <linux/err.h>
#include <linux/input.h>
#include <linux/mfd/a10sycon.h>
#include <linux/mfd/core.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/spi/spi.h>

static const struct of_device_id a10sycon_spi_of_match[];
static int a10sycon_spi_probe(struct spi_device *spi)
{
	int ret;
	struct a10sycon *a10sc;

	a10sc = devm_kzalloc(&spi->dev, sizeof(*a10sc),
			     GFP_KERNEL);
	if (!a10sc)
		return -ENOMEM;

	spi->mode = SPI_MODE_3;
	spi->bits_per_word = 8;
	spi_setup(spi);

	a10sc->dev = &spi->dev;
	a10sc->chip_irq = spi->irq;

	spi_set_drvdata(spi, a10sc);

	a10sycon_regmap_config.read_flag_mask = 1;
	a10sycon_regmap_config.write_flag_mask = 0;

	ret = of_platform_populate(spi->dev.of_node, a10sycon_spi_of_match,
				   NULL, &spi->dev);

	a10sc->regmap = devm_regmap_init_spi(spi, &a10sycon_regmap_config);
	if (IS_ERR(a10sc->regmap)) {
		ret = PTR_ERR(a10sc->regmap);
		dev_err(&spi->dev, "Allocate register map Failed: %d\n", ret);
		return ret;
	}

	a10sycon_device_init(a10sc);

	return 0;
}

static int a10sycon_spi_remove(struct spi_device *spi)
{
	struct a10sycon *a10sc = spi_get_drvdata(spi);

	a10sycon_device_exit(a10sc);
	return 0;
}

static const struct of_device_id a10sycon_spi_of_match[] = {
	{ .compatible = "altr,a10sycon"	},
	{ },
};
MODULE_DEVICE_TABLE(of, a10sycon_spi_of_match);

static struct spi_driver a10sycon_spi_driver = {
	.probe = a10sycon_spi_probe,
	.remove = a10sycon_spi_remove,
	.driver = {
		.name = "a10sycon",
		.of_match_table = a10sycon_spi_of_match,
	},
};

module_spi_driver(a10sycon_spi_driver);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Thor Thayer");
MODULE_DESCRIPTION("Altera Arria10 System Control Chip SPI Driver");
