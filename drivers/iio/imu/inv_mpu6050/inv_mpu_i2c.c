/*
* Copyright (C) 2012 Invensense, Inc.
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

#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/i2c.h>
#include <linux/i2c-mux.h>
#include <linux/iio/iio.h>
#include <linux/module.h>
#include "inv_mpu_iio.h"

static const struct regmap_config inv_mpu_regmap_config = {
	.reg_bits = 8,
	.val_bits = 8,
};

/*
 * The i2c read/write needs to happen in unlocked mode. As the parent
 * adapter is common. If we use locked versions, it will fail as
 * the mux adapter will lock the parent i2c adapter, while calling
 * select/deselect functions.
 */
static int inv_mpu6050_write_reg_unlocked(struct i2c_client *client,
					  u8 reg, u8 d)
{
	int ret;
	u8 buf[2] = {reg, d};
	struct i2c_msg msg[1] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = sizeof(buf),
			.buf = buf,
		}
	};

	ret = __i2c_transfer(client->adapter, msg, 1);
	if (ret != 1)
		return ret;

	return 0;
}

static int inv_mpu6050_select_bypass(struct i2c_adapter *adap, void *mux_priv,
				     u32 chan_id)
{
	struct i2c_client *client = mux_priv;
	struct iio_dev *indio_dev = dev_get_drvdata(&client->dev);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);
	int ret = 0;

	/* Use the same mutex which was used everywhere to protect power-op */
	mutex_lock(&indio_dev->mlock);
	if (!st->powerup_count) {
		ret = inv_mpu6050_write_reg_unlocked(client,
						     st->reg->pwr_mgmt_1, 0);
		if (ret)
			goto write_error;

		usleep_range(INV_MPU6050_REG_UP_TIME_MIN,
			     INV_MPU6050_REG_UP_TIME_MAX);
	}
	if (!ret) {
		st->powerup_count++;
		ret = inv_mpu6050_write_reg_unlocked(client,
						     st->reg->int_pin_cfg,
						     INV_MPU6050_INT_PIN_CFG |
						     INV_MPU6050_BIT_BYPASS_EN);
	}
write_error:
	mutex_unlock(&indio_dev->mlock);

	return ret;
}

static int inv_mpu6050_deselect_bypass(struct i2c_adapter *adap,
				       void *mux_priv, u32 chan_id)
{
	struct i2c_client *client = mux_priv;
	struct iio_dev *indio_dev = dev_get_drvdata(&client->dev);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	mutex_lock(&indio_dev->mlock);
	/* It doesn't really mattter, if any of the calls fails */
	inv_mpu6050_write_reg_unlocked(client, st->reg->int_pin_cfg,
				       INV_MPU6050_INT_PIN_CFG);
	st->powerup_count--;
	if (!st->powerup_count)
		inv_mpu6050_write_reg_unlocked(client, st->reg->pwr_mgmt_1,
					       INV_MPU6050_BIT_SLEEP);
	mutex_unlock(&indio_dev->mlock);

	return 0;
}

static const char *inv_mpu_match_acpi_device(struct device *dev, int *chip_id)
{
	const struct acpi_device_id *id;

	id = acpi_match_device(dev->driver->acpi_match_table, dev);
	if (!id)
		return NULL;

	*chip_id = (int)id->driver_data;

	return dev_name(dev);
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
	struct inv_mpu6050_state *st;
	int result, chip_type;
	struct regmap *regmap;
	const char *name;

	if (!i2c_check_functionality(client->adapter,
				     I2C_FUNC_SMBUS_I2C_BLOCK))
		return -EOPNOTSUPP;

	if (id) {
		chip_type = (int)id->driver_data;
		name = id->name;
	} else if (ACPI_HANDLE(&client->dev)) {
		name = inv_mpu_match_acpi_device(&client->dev, &chip_type);
		if (!name)
			return -ENODEV;
	} else {
		return -ENOSYS;
	}

	regmap = devm_regmap_init_i2c(client, &inv_mpu_regmap_config);
	if (IS_ERR(regmap)) {
		dev_err(&client->dev, "Failed to register i2c regmap %d\n",
			(int)PTR_ERR(regmap));
		return PTR_ERR(regmap);
	}

	result = inv_mpu_core_probe(regmap, client->irq, name,
				    NULL, chip_type);
	if (result < 0)
		return result;

	st = iio_priv(dev_get_drvdata(&client->dev));
	st->mux_adapter = i2c_add_mux_adapter(client->adapter,
					      &client->dev,
					      client,
					      0, 0, 0,
					      inv_mpu6050_select_bypass,
					      inv_mpu6050_deselect_bypass);
	if (!st->mux_adapter) {
		result = -ENODEV;
		goto out_unreg_device;
	}

	result = inv_mpu_acpi_create_mux_client(client);
	if (result)
		goto out_del_mux;

	return 0;

out_del_mux:
	i2c_del_mux_adapter(st->mux_adapter);
out_unreg_device:
	inv_mpu_core_remove(&client->dev);
	return result;
}

static int inv_mpu_remove(struct i2c_client *client)
{
	struct iio_dev *indio_dev = i2c_get_clientdata(client);
	struct inv_mpu6050_state *st = iio_priv(indio_dev);

	inv_mpu_acpi_delete_mux_client(client);
	i2c_del_mux_adapter(st->mux_adapter);

	return inv_mpu_core_remove(&client->dev);
}

/*
 * device id table is used to identify what device can be
 * supported by this driver
 */
static const struct i2c_device_id inv_mpu_id[] = {
	{"mpu6050", INV_MPU6050},
	{"mpu6500", INV_MPU6500},
	{}
};

MODULE_DEVICE_TABLE(i2c, inv_mpu_id);

static const struct acpi_device_id inv_acpi_match[] = {
	{"INVN6500", 0},
	{ },
};

MODULE_DEVICE_TABLE(acpi, inv_acpi_match);

static struct i2c_driver inv_mpu_driver = {
	.probe		=	inv_mpu_probe,
	.remove		=	inv_mpu_remove,
	.id_table	=	inv_mpu_id,
	.driver = {
		.acpi_match_table = ACPI_PTR(inv_acpi_match),
		.name	=	"inv-mpu6050-i2c",
		.pm     =       &inv_mpu_pmops,
	},
};

module_i2c_driver(inv_mpu_driver);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Invensense device MPU6050 driver");
MODULE_LICENSE("GPL");
