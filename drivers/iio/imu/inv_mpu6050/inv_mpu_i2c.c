// SPDX-License-Identifier: GPL-2.0-only
/*
* Copyright (C) 2012 Invensense, Inc.
*/

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/property.h>
#include "inv_mpu_iio.h"

static const struct regmap_config inv_mpu_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

static int inv_mpu6050_select_bypass(struct i2c_mux_core *muxc, u32 chan_id)
{
	return 0;
}

static bool inv_mpu_i2c_aux_bus(struct device *dev)
{
	struct inv_mpu6050_state *st = iio_priv(dev_get_drvdata(dev));

	switch (st->chip_type) {
	case INV_ICM20608:
	case INV_ICM20609:
	case INV_ICM20689:
	case INV_ICM20602:
	case INV_IAM20680:
		/* no i2c auxiliary bus on the chip */
		return false;
	case INV_MPU9150:
	case INV_MPU9250:
	case INV_MPU9255:
		if (st->magn_disabled)
			return true;
		else
			return false;
	default:
		return true;
	}
}

static int inv_mpu_i2c_aux_setup(struct iio_dev *indio_dev)
{
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	struct device *dev = indio_dev->dev.parent;
	struct device_node *mux_node;
	int ret;

	/*
	 * MPU9xxx magnetometer support requires to disable i2c auxiliary bus.
	 * To ensure backward compatibility with existing setups, do not disable
	 * i2c auxiliary bus if it used.
	 * Check for i2c-gate node in devicetree and set magnetometer disabled.
	 * Only MPU6500 is supported by ACPI, no need to check.
	 */
	switch (st->chip_type) {
	case INV_MPU9150:
	case INV_MPU9250:
	case INV_MPU9255:
		mux_node = of_get_child_by_name(dev->of_node, "i2c-gate");
		if (mux_node != NULL) {
			st->magn_disabled = true;
			dev_warn(dev, "disable internal use of magnetometer\n");
		}
		of_node_put(mux_node);
		break;
	default:
		break;
	}

	/* enable i2c bypass when using i2c auxiliary bus */
	if (inv_mpu_i2c_aux_bus(dev)) {
		ret = regmap_write(st->map, st->reg->int_pin_cfg,
				   st->irq_mask | INV_MPU6050_BIT_BYPASS_EN);
		if (ret)
			return ret;
	}

	return 0;
}

/**
 *  inv_mpu_probe() - probe function.
 *  @client:          i2c client.
 *  @id:              i2c device id.
 *
 *  Returns 0 on success, a negative error code otherwise.
 */
static int inv_mpu_probe(struct i2c_client *client,
			 const struct i2c_device_id *id)
{
	const void *match;
	struct inv_mpu6050_state *st;
	int result;
	enum inv_devices chip_type;
	struct regmap *regmap;
	const char *name;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EOPNOTSUPP;

	match = device_get_match_data(&client->dev);
	if (match) {
		chip_type = (enum inv_devices)match;
		name = client->name;
	} else if (id) {
		chip_type = (enum inv_devices)
			id->driver_data;
		name = id->name;
	} else {
		return -ENOSYS;
	}

	regmap = devm_regmap_init_i2c(client, &inv_mpu_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap: %pe\n",
			regmap);
		return PTR_ERR(regmap);
	}

	result = inv_mpu_core_probe(regmap, client->irq, name,
				    inv_mpu_i2c_aux_setup, chip_type);
	if (result < 0)
		return result;

	st = iio_priv(dev_get_drvdata(&client->dev));
	if (inv_mpu_i2c_aux_bus(&client->dev)) {
		/* declare i2c auxiliary bus */
		st->muxc = i2c_mux_alloc(client->adapter, &client->dev,
					 1, 0, I2C_MUX_LOCKED | I2C_MUX_GATE,
					 inv_mpu6050_select_bypass, NULL);
		if (!st->muxc)
			return -ENOMEM;
		st->muxc->priv = dev_get_drvdata(&client->dev);
		result = i2c_mux_add_adapter(st->muxc, 0, 0, 0);
		if (result)
			return result;
		result = inv_mpu_acpi_create_mux_client(client);
		if (result)
			goto out_del_mux;
	}

	return 0;

out_del_mux:
	i2c_mux_del_adapters(st->muxc);
	return result;
}

static int inv_mpu_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	if (st->muxc) {
		inv_mpu_acpi_delete_mux_client(client);
		i2c_mux_del_adapters(st->muxc);
	}

	return 0;
}

/*
 * device id table is used to identify what device can be
 * supported by this driver
 */
static const struct i2c_device_id inv_mpu_id[] = {
	{"mpu6050", INV_MPU6050},
	{"mpu6500", INV_MPU6500},
	{"mpu6515", INV_MPU6515},
	{"mpu6880", INV_MPU6880},
	{"mpu9150", INV_MPU9150},
	{"mpu9250", INV_MPU9250},
	{"mpu9255", INV_MPU9255},
	{"icm20608", INV_ICM20608},
	{"icm20609", INV_ICM20609},
	{"icm20689", INV_ICM20689},
	{"icm20602", INV_ICM20602},
	{"icm20690", INV_ICM20690},
	{"iam20680", INV_IAM20680},
	{}
};

MODULE_DEVICE_TABLE(i2c, inv_mpu_id);

static const struct of_device_id inv_of_match[] = {
	{
		.compatible = "invensense,mpu6050",
		.data = (void *)INV_MPU6050
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
		.compatible = "invensense,mpu9150",
		.data = (void *)INV_MPU9150
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

static const struct acpi_device_id __maybe_unused inv_acpi_match[] = {
	{"INVN6500", INV_MPU6500},
	{ },
};

MODULE_DEVICE_TABLE(acpi, inv_acpi_match);

static struct i2c_driver inv_mpu_driver = {
	.probe		=	inv_mpu_probe,
	.remove		=	inv_mpu_remove,
	.id_table	=	inv_mpu_id,
	.driver = {
		.of_match_table = inv_of_match,
		.acpi_match_table = ACPI_PTR(inv_acpi_match),
		.name	=	"inv-mpu6050-i2c",
		.pm     =       &inv_mpu_pmops,
	},
};

module_i2c_driver(inv_mpu_driver);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Invensense device MPU6050 driver");
MODULE_LICENSE("GPL");
