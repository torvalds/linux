// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2015 Intel Corporation Inc.
*/
#include <linux/mod_devicetable.h>
#include <linux/module.h>
#include <linux/property.h>
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

	if (st->reg->i2c_if) {
		ret = regmap_write(st->map, st->reg->i2c_if,
				   INV_ICM20602_BIT_I2C_IF_DIS);
	} else {
		st->chip_config.user_ctrl |= INV_MPU6050_BIT_I2C_IF_DIS;
		ret = regmap_write(st->map, st->reg->user_ctrl,
				   st->chip_config.user_ctrl);
	}

	return ret;
}

static int inv_mpu_probe(struct spi_device *spi)
{
	const void *match;
	struct regmap *regmap;
	const struct spi_device_id *spi_id;
	const char *name = NULL;
	enum inv_devices chip_type;

	if ((spi_id = spi_get_device_id(spi))) {
		chip_type = (enum inv_devices)spi_id->driver_data;
		name = spi_id->name;
	} else if ((match = device_get_match_data(&spi->dev))) {
		chip_type = (uintptr_t)match;
		name = dev_name(&spi->dev);
	} else {
		return -ENODEV;
	}

	regmap = devm_regmap_init_spi(spi, &inv_mpu_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&spi->dev, "Failed to register spi regmap: %pe\n",
			regmap);
		return PTR_ERR(regmap);
	}

	return inv_mpu_core_probe(regmap, spi->irq, name,
				  inv_mpu_i2c_disable, chip_type);
}

/*
 * device id table is used to identify what device can be
 * supported by this driver
 */
static const struct spi_device_id inv_mpu_id[] = {
	{"mpu6000", INV_MPU6000},
	{"mpu6500", INV_MPU6500},
	{"mpu6515", INV_MPU6515},
	{"mpu6880", INV_MPU6880},
	{"mpu9250", INV_MPU9250},
	{"mpu9255", INV_MPU9255},
	{"icm20608", INV_ICM20608},
	{"icm20608d", INV_ICM20608D},
	{"icm20609", INV_ICM20609},
	{"icm20689", INV_ICM20689},
	{"icm20602", INV_ICM20602},
	{"icm20690", INV_ICM20690},
	{"iam20680", INV_IAM20680},
	{}
};

MODULE_DEVICE_TABLE(spi, inv_mpu_id);

static const struct of_device_id inv_of_match[] = {
	{
		.compatible = "invensense,mpu6000",
		.data = (void *)INV_MPU6000
	},
	{
		.compatible = "invensense,mpu6500",
		.data = (void *)INV_MPU6500
	},
	{
		.compatible = "invensense,mpu6515",
		.data = (void *)INV_MPU6515
	},
	{
		.compatible = "invensense,mpu6880",
		.data = (void *)INV_MPU6880
	},
	{
		.compatible = "invensense,mpu9250",
		.data = (void *)INV_MPU9250
	},
	{
		.compatible = "invensense,mpu9255",
		.data = (void *)INV_MPU9255
	},
	{
		.compatible = "invensense,icm20608",
		.data = (void *)INV_ICM20608
	},
	{
		.compatible = "invensense,icm20608d",
		.data = (void *)INV_ICM20608D
	},
	{
		.compatible = "invensense,icm20609",
		.data = (void *)INV_ICM20609
	},
	{
		.compatible = "invensense,icm20689",
		.data = (void *)INV_ICM20689
	},
	{
		.compatible = "invensense,icm20602",
		.data = (void *)INV_ICM20602
	},
	{
		.compatible = "invensense,icm20690",
		.data = (void *)INV_ICM20690
	},
	{
		.compatible = "invensense,iam20680",
		.data = (void *)INV_IAM20680
	},
	{ }
};
MODULE_DEVICE_TABLE(of, inv_of_match);

static const struct acpi_device_id inv_acpi_match[] = {
	{"INVN6000", INV_MPU6000},
	{ },
};
MODULE_DEVICE_TABLE(acpi, inv_acpi_match);

static struct spi_driver inv_mpu_driver = {
	.probe		=	inv_mpu_probe,
	.id_table	=	inv_mpu_id,
	.driver = {
		.of_match_table = inv_of_match,
		.acpi_match_table = inv_acpi_match,
		.name	=	"inv-mpu6000-spi",
		.pm     =       pm_ptr(&inv_mpu_pmops),
	},
};

module_spi_driver(inv_mpu_driver);

MODULE_AUTHOR("Adriana Reus <adriana.reus@intel.com>");
MODULE_DESCRIPTION("Invensense device MPU6000 driver");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(IIO_MPU6050);
