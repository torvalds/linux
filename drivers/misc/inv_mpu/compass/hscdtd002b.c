/*
	$License:
	Copyright (C) 2011 InvenSense Corporation, All Rights Reserved.

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program.  If not, see <http://www.gnu.org/licenses/>.
	$
 */

/**
 *  @addtogroup COMPASSDL
 *
 *  @{
 *      @file   hscdtd002b.c
 *      @brief  Magnetometer setup and handling methods for Alps HSCDTD002B
 *              compass.
 */

/* -------------------------------------------------------------------------- */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include "mpu-dev.h"

#include <log.h>
#include <linux/mpu.h>
#include "mlsl.h"
#include "mldl_cfg.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-compass"

/* -------------------------------------------------------------------------- */
#define COMPASS_HSCDTD002B_STAT          (0x18)
#define COMPASS_HSCDTD002B_CTRL1         (0x1B)
#define COMPASS_HSCDTD002B_CTRL2         (0x1C)
#define COMPASS_HSCDTD002B_CTRL3         (0x1D)
#define COMPASS_HSCDTD002B_DATAX         (0x10)

/* -------------------------------------------------------------------------- */
static int hscdtd002b_suspend(void *mlsl_handle,
			      struct ext_slave_descr *slave,
			      struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;

	/* Power mode: stand-by */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    COMPASS_HSCDTD002B_CTRL1, 0x00);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	msleep(1);		/* turn-off time */

	return result;
}

static int hscdtd002b_resume(void *mlsl_handle,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;

	/* Soft reset */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    COMPASS_HSCDTD002B_CTRL3, 0x80);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Force state; Power mode: active */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    COMPASS_HSCDTD002B_CTRL1, 0x82);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Data ready enable */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    COMPASS_HSCDTD002B_CTRL2, 0x08);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	msleep(1);		/* turn-on time */

	return result;
}

static int hscdtd002b_read(void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata,
			   unsigned char *data)
{
	unsigned char stat;
	int result = INV_SUCCESS;
	int status = INV_SUCCESS;

	/* Read status reg. to check if data is ready */
	result =
	    inv_serial_read(mlsl_handle, pdata->address,
			    COMPASS_HSCDTD002B_STAT, 1, &stat);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	if (stat & 0x40) {
		result =
		    inv_serial_read(mlsl_handle, pdata->address,
				    COMPASS_HSCDTD002B_DATAX, 6,
				    (unsigned char *)data);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		status = INV_SUCCESS;
	} else if (stat & 0x20) {
		status = INV_ERROR_COMPASS_DATA_OVERFLOW;
	} else {
		status = INV_ERROR_COMPASS_DATA_NOT_READY;
	}
	/* trigger next measurement read */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    COMPASS_HSCDTD002B_CTRL3, 0x40);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return status;
}

static struct ext_slave_descr hscdtd002b_descr = {
	.init             = NULL,
	.exit             = NULL,
	.suspend          = hscdtd002b_suspend,
	.resume           = hscdtd002b_resume,
	.read             = hscdtd002b_read,
	.config           = NULL,
	.get_config       = NULL,
	.name             = "hscdtd002b",
	.type             = EXT_SLAVE_TYPE_COMPASS,
	.id               = COMPASS_ID_HSCDTD002B,
	.read_reg         = 0x10,
	.read_len         = 6,
	.endian           = EXT_SLAVE_LITTLE_ENDIAN,
	.range            = {9830, 4000},
	.trigger          = NULL,
};

static
struct ext_slave_descr *hscdtd002b_get_slave_descr(void)
{
	return &hscdtd002b_descr;
}

/* -------------------------------------------------------------------------- */
struct hscdtd002b_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int hscdtd002b_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct hscdtd002b_mod_private_data *private_data;
	int result = 0;

	dev_info(&client->adapter->dev, "%s: %s\n", __func__, devid->name);

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) {
		result = -ENODEV;
		goto out_no_free;
	}

	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->adapter->dev,
			"Missing platform data for slave %s\n", devid->name);
		result = -EFAULT;
		goto out_no_free;
	}

	private_data = kzalloc(sizeof(*private_data), GFP_KERNEL);
	if (!private_data) {
		result = -ENOMEM;
		goto out_no_free;
	}

	i2c_set_clientdata(client, private_data);
	private_data->client = client;
	private_data->pdata = pdata;

	result = inv_mpu_register_slave(THIS_MODULE, client, pdata,
					hscdtd002b_get_slave_descr);
	if (result) {
		dev_err(&client->adapter->dev,
			"Slave registration failed: %s, %d\n",
			devid->name, result);
		goto out_free_memory;
	}

	return result;

out_free_memory:
	kfree(private_data);
out_no_free:
	dev_err(&client->adapter->dev, "%s failed %d\n", __func__, result);
	return result;

}

static int hscdtd002b_mod_remove(struct i2c_client *client)
{
	struct hscdtd002b_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				hscdtd002b_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id hscdtd002b_mod_id[] = {
	{ "hscdtd002b", COMPASS_ID_HSCDTD002B },
	{}
};

MODULE_DEVICE_TABLE(i2c, hscdtd002b_mod_id);

static struct i2c_driver hscdtd002b_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = hscdtd002b_mod_probe,
	.remove = hscdtd002b_mod_remove,
	.id_table = hscdtd002b_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "hscdtd002b_mod",
		   },
	.address_list = normal_i2c,
};

static int __init hscdtd002b_mod_init(void)
{
	int res = i2c_add_driver(&hscdtd002b_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "hscdtd002b_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit hscdtd002b_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&hscdtd002b_mod_driver);
}

module_init(hscdtd002b_mod_init);
module_exit(hscdtd002b_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate HSCDTD002B sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("hscdtd002b_mod");

/**
 *  @}
 */
