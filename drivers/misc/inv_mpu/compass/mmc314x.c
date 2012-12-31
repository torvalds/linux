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
 *      @file   mmc314x.c
 *      @brief  Magnetometer setup and handling methods for the
 *              MEMSIC MMC314x compass.
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

static int reset_int = 1000;
static int read_count = 1;
static char reset_mode;		/* in Z-init section */

/* -------------------------------------------------------------------------- */
#define MMC314X_REG_ST (0x00)
#define MMC314X_REG_X_MSB (0x01)

#define MMC314X_CNTL_MODE_WAKE_UP (0x01)
#define MMC314X_CNTL_MODE_SET (0x02)
#define MMC314X_CNTL_MODE_RESET (0x04)

/* -------------------------------------------------------------------------- */

static int mmc314x_suspend(void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;

	return result;
}

static int mmc314x_resume(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{

	int result;
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    MMC314X_REG_ST, MMC314X_CNTL_MODE_RESET);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	msleep(10);
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    MMC314X_REG_ST, MMC314X_CNTL_MODE_SET);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	msleep(10);
	read_count = 1;
	return INV_SUCCESS;
}

static int mmc314x_read(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			unsigned char *data)
{
	int result, ii;
	short tmp[3];
	unsigned char tmpdata[6];

	if (read_count > 1000)
		read_count = 1;

	result =
	    inv_serial_read(mlsl_handle, pdata->address, MMC314X_REG_X_MSB,
			    6, (unsigned char *)data);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	for (ii = 0; ii < 6; ii++)
		tmpdata[ii] = data[ii];

	for (ii = 0; ii < 3; ii++) {
		tmp[ii] = (short)((tmpdata[2 * ii] << 8) + tmpdata[2 * ii + 1]);
		tmp[ii] = tmp[ii] - 4096;
		tmp[ii] = tmp[ii] * 16;
	}

	for (ii = 0; ii < 3; ii++) {
		data[2 * ii] = (unsigned char)(tmp[ii] >> 8);
		data[2 * ii + 1] = (unsigned char)(tmp[ii]);
	}

	if (read_count % reset_int == 0) {
		if (reset_mode) {
			result =
			    inv_serial_single_write(mlsl_handle,
						    pdata->address,
						    MMC314X_REG_ST,
						    MMC314X_CNTL_MODE_RESET);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			reset_mode = 0;
			return INV_ERROR_COMPASS_DATA_NOT_READY;
		} else {
			result =
			    inv_serial_single_write(mlsl_handle,
						    pdata->address,
						    MMC314X_REG_ST,
						    MMC314X_CNTL_MODE_SET);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			reset_mode = 1;
			read_count++;
			return INV_ERROR_COMPASS_DATA_NOT_READY;
		}
	}
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    MMC314X_REG_ST, MMC314X_CNTL_MODE_WAKE_UP);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	read_count++;

	return INV_SUCCESS;
}

static struct ext_slave_descr mmc314x_descr = {
	.init             = NULL,
	.exit             = NULL,
	.suspend          = mmc314x_suspend,
	.resume           = mmc314x_resume,
	.read             = mmc314x_read,
	.config           = NULL,
	.get_config       = NULL,
	.name             = "mmc314x",
	.type             = EXT_SLAVE_TYPE_COMPASS,
	.id               = COMPASS_ID_MMC314X,
	.read_reg         = 0x01,
	.read_len         = 6,
	.endian           = EXT_SLAVE_BIG_ENDIAN,
	.range            = {400, 0},
	.trigger          = NULL,
};

static
struct ext_slave_descr *mmc314x_get_slave_descr(void)
{
	return &mmc314x_descr;
}

/* -------------------------------------------------------------------------- */
struct mmc314x_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int mmc314x_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct mmc314x_mod_private_data *private_data;
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
					mmc314x_get_slave_descr);
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

static int mmc314x_mod_remove(struct i2c_client *client)
{
	struct mmc314x_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				mmc314x_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id mmc314x_mod_id[] = {
	{ "mmc314x", COMPASS_ID_MMC314X },
	{}
};

MODULE_DEVICE_TABLE(i2c, mmc314x_mod_id);

static struct i2c_driver mmc314x_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = mmc314x_mod_probe,
	.remove = mmc314x_mod_remove,
	.id_table = mmc314x_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "mmc314x_mod",
		   },
	.address_list = normal_i2c,
};

static int __init mmc314x_mod_init(void)
{
	int res = i2c_add_driver(&mmc314x_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "mmc314x_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit mmc314x_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&mmc314x_mod_driver);
}

module_init(mmc314x_mod_init);
module_exit(mmc314x_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate MMC314X sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("mmc314x_mod");

/**
 *  @}
 */
