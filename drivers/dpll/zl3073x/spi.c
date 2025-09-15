// SPDX-License-Identifier: GPL-2.0-only

#include <linux/dev_printk.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "core.h"

static int zl3073x_spi_probe(struct spi_device *spi)
{
	struct device *dev = &spi->dev;
	struct zl3073x_dev *zldev;

	zldev = zl3073x_devm_alloc(dev);
	if (IS_ERR(zldev))
		return PTR_ERR(zldev);

	zldev->regmap = devm_regmap_init_spi(spi, &zl3073x_regmap_config);
	if (IS_ERR(zldev->regmap))
		return dev_err_probe(dev, PTR_ERR(zldev->regmap),
				     "Failed to initialize regmap\n");

	return zl3073x_dev_probe(zldev, spi_get_device_match_data(spi));
}

static const struct spi_device_id zl3073x_spi_id[] = {
	{
		.name = "zl30731",
		.driver_data = (kernel_ulong_t)&zl30731_chip_info
	},
	{
		.name = "zl30732",
		.driver_data = (kernel_ulong_t)&zl30732_chip_info,
	},
	{
		.name = "zl30733",
		.driver_data = (kernel_ulong_t)&zl30733_chip_info,
	},
	{
		.name = "zl30734",
		.driver_data = (kernel_ulong_t)&zl30734_chip_info,
	},
	{
		.name = "zl30735",
		.driver_data = (kernel_ulong_t)&zl30735_chip_info,
	},
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(spi, zl3073x_spi_id);

static const struct of_device_id zl3073x_spi_of_match[] = {
	{ .compatible = "microchip,zl30731", .data = &zl30731_chip_info },
	{ .compatible = "microchip,zl30732", .data = &zl30732_chip_info },
	{ .compatible = "microchip,zl30733", .data = &zl30733_chip_info },
	{ .compatible = "microchip,zl30734", .data = &zl30734_chip_info },
	{ .compatible = "microchip,zl30735", .data = &zl30735_chip_info },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, zl3073x_spi_of_match);

static struct spi_driver zl3073x_spi_driver = {
	.driver = {
		.name = "zl3073x-spi",
		.of_match_table = zl3073x_spi_of_match,
	},
	.probe = zl3073x_spi_probe,
	.id_table = zl3073x_spi_id,
};
module_spi_driver(zl3073x_spi_driver);

MODULE_AUTHOR("Ivan Vecera <ivecera@redhat.com>");
MODULE_DESCRIPTION("Microchip ZL3073x SPI driver");
MODULE_IMPORT_NS("ZL3073X");
MODULE_LICENSE("GPL");
