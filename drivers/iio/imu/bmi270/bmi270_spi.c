// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)

#include <linux/iio/iio.h>
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include "bmi270.h"

/*
 * The following two functions are taken from the BMI323 spi driver code.
 * In section 6.4 of the BMI270 data it specifies that after a read
 * operation the first data byte from the device is a dummy byte
 */
static int bmi270_regmap_spi_read(void *spi, const void *reg_buf,
				  size_t reg_size, void *val_buf,
				  size_t val_size)
{
	return spi_write_then_read(spi, reg_buf, reg_size, val_buf, val_size);
}

static int bmi270_regmap_spi_write(void *spi, const void *data,
				   size_t count)
{
	u8 *data_buff = (u8 *)data;

	/*
	 * Remove the extra pad byte since its only needed for the read
	 * operation
	 */
	data_buff[1] = data_buff[0];
	return spi_write_then_read(spi, data_buff + 1, count - 1, NULL, 0);
}

static const struct regmap_bus bmi270_regmap_bus = {
	.read = bmi270_regmap_spi_read,
	.write = bmi270_regmap_spi_write,
};

static const struct regmap_config bmi270_spi_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
	.pad_bits = 8,
	.read_flag_mask = BIT(7),
};

static int bmi270_spi_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	struct device *dev = &spi->dev;
	const struct bmi270_chip_info *chip_info;

	chip_info = spi_get_device_match_data(spi);
	if (!chip_info)
		return -ENODEV;

	regmap = devm_regmap_init(dev, &bmi270_regmap_bus, dev,
				  &bmi270_spi_regmap_config);
	if (IS_ERR(regmap))
		return dev_err_probe(dev, PTR_ERR(regmap),
				     "Failed to init i2c regmap");

	return bmi270_core_probe(dev, regmap, chip_info);
}

static const struct spi_device_id bmi270_spi_id[] = {
	{ "bmi260", (kernel_ulong_t)&bmi260_chip_info },
	{ "bmi270", (kernel_ulong_t)&bmi270_chip_info },
	{ }
};

static const struct of_device_id bmi270_of_match[] = {
	{ .compatible = "bosch,bmi260", .data = &bmi260_chip_info },
	{ .compatible = "bosch,bmi270", .data = &bmi270_chip_info },
	{ }
};

static struct spi_driver bmi270_spi_driver = {
	.driver = {
		.name = "bmi270",
		.of_match_table = bmi270_of_match,
	},
	.probe = bmi270_spi_probe,
	.id_table = bmi270_spi_id,
};
module_spi_driver(bmi270_spi_driver);

MODULE_AUTHOR("Alex Lanzano");
MODULE_DESCRIPTION("BMI270 driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS("IIO_BMI270");
