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
 *  @addtogroup ACCELDL
 *  @brief      Accelerometer setup and handling methods for Kionix KXSD9.
 *
 *  @{
 *      @file   kxsd9.c
 *      @brief  Accelerometer setup and handling methods for Kionix KXSD9.
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

static int kxsd9_suspend(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	/* CTRL_REGB: low-power standby mode */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address, 0x0d, 0x0);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

/* full scale setting - register and mask */
#define ACCEL_KIONIX_CTRL_REG      (0x0C)
#define ACCEL_KIONIX_CTRL_MASK     (0x3)

static int kxsd9_resume(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	unsigned char reg;

	/* Full Scale */
	reg = 0x0;
	reg &= ~ACCEL_KIONIX_CTRL_MASK;
	reg |= 0x00;
	if (slave->range.mantissa == 4) {		/* 4g scale = 4.9951 */
		reg |= 0x2;
		slave->range.fraction = 9951;
	} else if (slave->range.mantissa == 7) {	/* 6g scale = 7.5018 */
		reg |= 0x1;
		slave->range.fraction = 5018;
	} else if (slave->range.mantissa == 9) {	/* 8g scale = 9.9902 */
		reg |= 0x0;
		slave->range.fraction = 9902;
	} else {
		slave->range.mantissa = 2;		/* 2g scale = 2.5006 */
		slave->range.fraction = 5006;
		reg |= 0x3;
	}
	reg |= 0xC0;		/* 100Hz LPF */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    ACCEL_KIONIX_CTRL_REG, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* normal operation */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address, 0x0d, 0x40);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return INV_SUCCESS;
}

static int kxsd9_read(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata,
		      unsigned char *data)
{
	int result;
	result = inv_serial_read(mlsl_handle, pdata->address,
				 slave->read_reg, slave->read_len, data);
	return result;
}

static struct ext_slave_descr kxsd9_descr = {
	.init             = NULL,
	.exit             = NULL,
	.suspend          = kxsd9_suspend,
	.resume           = kxsd9_resume,
	.read             = kxsd9_read,
	.config           = NULL,
	.get_config       = NULL,
	.name             = "kxsd9",
	.type             = EXT_SLAVE_TYPE_ACCEL,
	.id               = ACCEL_ID_KXSD9,
	.read_reg         = 0x00,
	.read_len         = 6,
	.endian           = EXT_SLAVE_BIG_ENDIAN,
	.range            = {2, 5006},
	.trigger          = NULL,
};

static
struct ext_slave_descr *kxsd9_get_slave_descr(void)
{
	return &kxsd9_descr;
}

/* -------------------------------------------------------------------------- */
struct kxsd9_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int kxsd9_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct kxsd9_mod_private_data *private_data;
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
					kxsd9_get_slave_descr);
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

static int kxsd9_mod_remove(struct i2c_client *client)
{
	struct kxsd9_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				kxsd9_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id kxsd9_mod_id[] = {
	{ "kxsd9", ACCEL_ID_KXSD9 },
	{}
};

MODULE_DEVICE_TABLE(i2c, kxsd9_mod_id);

static struct i2c_driver kxsd9_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = kxsd9_mod_probe,
	.remove = kxsd9_mod_remove,
	.id_table = kxsd9_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "kxsd9_mod",
		   },
	.address_list = normal_i2c,
};

static int __init kxsd9_mod_init(void)
{
	int res = i2c_add_driver(&kxsd9_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "kxsd9_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit kxsd9_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&kxsd9_mod_driver);
}

module_init(kxsd9_mod_init);
module_exit(kxsd9_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate KXSD9 sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("kxsd9_mod");

/**
 *  @}
 */
