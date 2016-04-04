/*
* Copyright (C) 2015 Intel Corporation Inc.
*
* This software is licensed under the terms of the GNU General Public
* License version 2, as published by the Free Software Foundation, and
* may be copied, distributed, and modified under those terms.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*/
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/spi/spi.h>
#include <linux/regmap.h>
#include <linux/iio/iio.h>
#include "inv_mpu_iio.h"

static const struct regmap_config inv_mpu_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int inv_mpu_i2c_disable(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int ret = 0;

	ret = inv_mpu6050_set_power_itg(st, true);
	if (ret)
		return ret;

	ret = regmap_write(st->map, INV_MPU6050_REG_USER_CTRL,
			   INV_MPU6050_BIT_I2C_IF_DIS);
	if (ret) {
		inv_mpu6050_set_power_itg(st, false);
		return ret;
	}

	return inv_mpu6050_set_power_itg(st, false);
}

static int inv_mpu_probe(struct spi_device *spi)
{
	struct regmap *regmap;
	const struct spi_device_id *id = spi_get_device_id(spi);
	const char *name = id ? id->name : NULL;
	const int chip_type = id ? id->driver_data : 0;

	regmap = devm_regmap_init_spi(spi, &inv_mpu_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	return inv_mpu_core_probe(regmap, spi->irq, name,
				  inv_mpu_i2c_disable, chip_type);
}

static int inv_mpu_remove(struct spi_device *spi)
{
	return inv_mpu_core_remove(&spi->dev);
}

/*
 * device id table is used to identify what device can be
 * supported by this driver
 */
static const struct spi_device_id inv_mpu_id[] = {
	{"mpu6000", INV_MPU6000},
	{}
};

MODULE_DEVICE_TABLE(spi, inv_mpu_id);

static const struct acpi_device_id inv_acpi_match[] = {
	{"INVN6000", 0},
	{ },
};
MODULE_DEVICE_TABLE(acpi, inv_acpi_match);

static struct spi_driver inv_mpu_driver = {
	.probe		=	inv_mpu_probe,
	.remove		=	inv_mpu_remove,
	.id_table	=	inv_mpu_id,
	.driver = {
		.acpi_match_table = ACPI_PTR(inv_acpi_match),
		.name	=	"inv-mpu6000-spi",
		.pm     =       &inv_mpu_pmops,
	},
};

module_spi_driver(inv_mpu_driver);

MODULE_AUTHOR("Adriana Reus <adriana.reus@intel.com>");
MODULE_DESCRIPTION("Invensense device MPU6000 driver");
MODULE_LICENSE("GPL");
