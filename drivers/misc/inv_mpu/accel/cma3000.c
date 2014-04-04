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

/*
 *  @addtogroup ACCELDL
 *  @brief      Accelerometer setup and handling methods for VTI CMA3000.
 *
 *  @{
 *      @file   cma3000.c
 *      @brief  Accelerometer setup and handling methods for VTI CMA3000
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
#define MPL_LOG_TAG "MPL-acc"

/* -------------------------------------------------------------------------- */

static int cma3000_suspend(void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata)
{
	int result;
	/* RAM reset */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address, 0x1d, 0xcd);
	return result;
}

static int cma3000_resume(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{

	return INV_SUCCESS;
}

static int cma3000_read(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			unsigned char *data)
{
	int result;
	result = inv_serial_read(mlsl_handle, pdata->address,
				 slave->reg, slave->len, data);
	return result;
}

static struct ext_slave_descr cma3000_descr = {
	.init             = NULL,
	.exit             = NULL,
	.suspend          = cma3000_suspend,
	.resume           = cma3000_resume,
	.read             = cma3000_read,
	.config           = NULL,
	.get_config       = NULL,
	.name             = "cma3000",
	.type             = EXT_SLAVE_TYPE_ACCEL,
	.id               = ID_INVALID,
	.read_reg         = 0x06,
	.read_len         = 6,
	.endian           = EXT_SLAVE_LITTLE_ENDIAN,
	.range            = {2, 0},
	.trigger          = NULL,

};

static
struct ext_slave_descr *cma3000_get_slave_descr(void)
{
	return &cma3000_descr;
}

/* -------------------------------------------------------------------------- */

struct cma3000_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int cma3000_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct cma3000_mod_private_data *private_data;
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
					cma3000_get_slave_descr);
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

static int cma3000_mod_remove(struct i2c_client *client)
{
	struct cma3000_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				cma3000_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id cma3000_mod_id[] = {
	{ "cma3000", ACCEL_ID_CMA3000 },
	{}
};

MODULE_DEVICE_TABLE(i2c, cma3000_mod_id);

static struct i2c_driver cma3000_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = cma3000_mod_probe,
	.remove = cma3000_mod_remove,
	.id_table = cma3000_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "cma3000_mod",
		   },
	.address_list = normal_i2c,
};

static int __init cma3000_mod_init(void)
{
	int res = i2c_add_driver(&cma3000_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "cma3000_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit cma3000_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&cma3000_mod_driver);
}

module_init(cma3000_mod_init);
module_exit(cma3000_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate CMA3000 sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("cma3000_mod");

/**
 *  @}
 */
