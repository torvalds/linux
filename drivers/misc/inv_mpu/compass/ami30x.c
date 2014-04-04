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
 *      @file   ami30x.c
 *      @brief  Magnetometer setup and handling methods for Aichi AMI304
 *              and AMI305 compass devices.
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
#define AMI30X_REG_DATAX (0x10)
#define AMI30X_REG_STAT1 (0x18)
#define AMI30X_REG_CNTL1 (0x1B)
#define AMI30X_REG_CNTL2 (0x1C)
#define AMI30X_REG_CNTL3 (0x1D)

#define AMI30X_BIT_CNTL1_PC1  (0x80)
#define AMI30X_BIT_CNTL1_ODR1 (0x10)
#define AMI30X_BIT_CNTL1_FS1  (0x02)

#define AMI30X_BIT_CNTL2_IEN  (0x10)
#define AMI30X_BIT_CNTL2_DREN (0x08)
#define AMI30X_BIT_CNTL2_DRP  (0x04)
#define AMI30X_BIT_CNTL3_F0RCE (0x40)

/* -------------------------------------------------------------------------- */
static int ami30x_suspend(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    inv_serial_read(mlsl_handle, pdata->address, AMI30X_REG_CNTL1,
			    1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	reg &= ~(AMI30X_BIT_CNTL1_PC1 | AMI30X_BIT_CNTL1_FS1);
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AMI30X_REG_CNTL1, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return result;
}

static int ami30x_resume(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;

	/* Set CNTL1 reg to power model active */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AMI30X_REG_CNTL1,
				    AMI30X_BIT_CNTL1_PC1 |
				    AMI30X_BIT_CNTL1_FS1);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Set CNTL2 reg to DRDY active high and enabled */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AMI30X_REG_CNTL2,
				    AMI30X_BIT_CNTL2_DREN |
				    AMI30X_BIT_CNTL2_DRP);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Set CNTL3 reg to forced measurement period */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    AMI30X_REG_CNTL3, AMI30X_BIT_CNTL3_F0RCE);

	return result;
}

static int ami30x_read(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       unsigned char *data)
{
	unsigned char stat;
	int result = INV_SUCCESS;

	/* Read status reg and check if data ready (DRDY) */
	result =
	    inv_serial_read(mlsl_handle, pdata->address, AMI30X_REG_STAT1,
			    1, &stat);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (stat & 0x40) {
		result =
		    inv_serial_read(mlsl_handle, pdata->address,
				    AMI30X_REG_DATAX, 6, (unsigned char *)data);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		/* start another measurement */
		result =
		    inv_serial_single_write(mlsl_handle, pdata->address,
					    AMI30X_REG_CNTL3,
					    AMI30X_BIT_CNTL3_F0RCE);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}

		return INV_SUCCESS;
	}

	return INV_ERROR_COMPASS_DATA_NOT_READY;
}


/* For AMI305,the range field needs to be modified to {9830.4f} */
static struct ext_slave_descr ami30x_descr = {
	.init             = NULL,
	.exit             = NULL,
	.suspend          = ami30x_suspend,
	.resume           = ami30x_resume,
	.read             = ami30x_read,
	.config           = NULL,
	.get_config       = NULL,
	.name             = "ami30x",
	.type             = EXT_SLAVE_TYPE_COMPASS,
	.id               = COMPASS_ID_AMI30X,
	.read_reg         = 0x06,
	.read_len         = 6,
	.endian           = EXT_SLAVE_LITTLE_ENDIAN,
	.range            = {5461, 3333},
	.trigger          = NULL,
};

static
struct ext_slave_descr *ami30x_get_slave_descr(void)
{
	return &ami30x_descr;
}

/* -------------------------------------------------------------------------- */
struct ami30x_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int ami30x_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct ami30x_mod_private_data *private_data;
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
					ami30x_get_slave_descr);
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

static int ami30x_mod_remove(struct i2c_client *client)
{
	struct ami30x_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				ami30x_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id ami30x_mod_id[] = {
	{ "ami30x", COMPASS_ID_AMI30X },
	{}
};

MODULE_DEVICE_TABLE(i2c, ami30x_mod_id);

static struct i2c_driver ami30x_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = ami30x_mod_probe,
	.remove = ami30x_mod_remove,
	.id_table = ami30x_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "ami30x_mod",
		   },
	.address_list = normal_i2c,
};

static int __init ami30x_mod_init(void)
{
	int res = i2c_add_driver(&ami30x_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "ami30x_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit ami30x_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&ami30x_mod_driver);
}

module_init(ami30x_mod_init);
module_exit(ami30x_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate AMI30X sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("ami30x_mod");

/**
 *  @}
 */
