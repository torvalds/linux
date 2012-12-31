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
 *      @file   hmc5883.c
 *      @brief  Magnetometer setup and handling methods for Honeywell
 *              HMC5883 compass.
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
enum HMC_REG {
	HMC_REG_CONF_A = 0x0,
	HMC_REG_CONF_B = 0x1,
	HMC_REG_MODE = 0x2,
	HMC_REG_X_M = 0x3,
	HMC_REG_X_L = 0x4,
	HMC_REG_Z_M = 0x5,
	HMC_REG_Z_L = 0x6,
	HMC_REG_Y_M = 0x7,
	HMC_REG_Y_L = 0x8,
	HMC_REG_STATUS = 0x9,
	HMC_REG_ID_A = 0xA,
	HMC_REG_ID_B = 0xB,
	HMC_REG_ID_C = 0xC
};

enum HMC_CONF_A {
	HMC_CONF_A_DRATE_MASK = 0x1C,
	HMC_CONF_A_DRATE_0_75 = 0x00,
	HMC_CONF_A_DRATE_1_5 = 0x04,
	HMC_CONF_A_DRATE_3 = 0x08,
	HMC_CONF_A_DRATE_7_5 = 0x0C,
	HMC_CONF_A_DRATE_15 = 0x10,
	HMC_CONF_A_DRATE_30 = 0x14,
	HMC_CONF_A_DRATE_75 = 0x18,
	HMC_CONF_A_MEAS_MASK = 0x3,
	HMC_CONF_A_MEAS_NORM = 0x0,
	HMC_CONF_A_MEAS_POS = 0x1,
	HMC_CONF_A_MEAS_NEG = 0x2
};

enum HMC_CONF_B {
	HMC_CONF_B_GAIN_MASK = 0xE0,
	HMC_CONF_B_GAIN_0_9 = 0x00,
	HMC_CONF_B_GAIN_1_2 = 0x20,
	HMC_CONF_B_GAIN_1_9 = 0x40,
	HMC_CONF_B_GAIN_2_5 = 0x60,
	HMC_CONF_B_GAIN_4_0 = 0x80,
	HMC_CONF_B_GAIN_4_6 = 0xA0,
	HMC_CONF_B_GAIN_5_5 = 0xC0,
	HMC_CONF_B_GAIN_7_9 = 0xE0
};

enum HMC_MODE {
	HMC_MODE_MASK = 0x3,
	HMC_MODE_CONT = 0x0,
	HMC_MODE_SINGLE = 0x1,
	HMC_MODE_IDLE = 0x2,
	HMC_MODE_SLEEP = 0x3
};

/* -------------------------------------------------------------------------- */
static int hmc5883_suspend(void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;

	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    HMC_REG_MODE, HMC_MODE_SLEEP);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	msleep(3);

	return result;
}

static int hmc5883_resume(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;

	/* Use single measurement mode. Start at sleep state. */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    HMC_REG_MODE, HMC_MODE_SLEEP);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Config normal measurement */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    HMC_REG_CONF_A, 0);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* Adjust gain to 307 LSB/Gauss */
	result =
	    inv_serial_single_write(mlsl_handle, pdata->address,
				    HMC_REG_CONF_B, HMC_CONF_B_GAIN_5_5);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return result;
}

static int hmc5883_read(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			unsigned char *data)
{
	unsigned char stat;
	int result = INV_SUCCESS;
	unsigned char tmp;
	short axisFixed;

	/* Read status reg. to check if data is ready */
	result =
	    inv_serial_read(mlsl_handle, pdata->address, HMC_REG_STATUS, 1,
			    &stat);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	if (stat & 0x01) {
		result =
		    inv_serial_read(mlsl_handle, pdata->address,
				    HMC_REG_X_M, 6, (unsigned char *)data);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}

		/* switch YZ axis to proper position */
		tmp = data[2];
		data[2] = data[4];
		data[4] = tmp;
		tmp = data[3];
		data[3] = data[5];
		data[5] = tmp;

		/*drop data if overflows */
		if ((data[0] == 0xf0) || (data[2] == 0xf0)
		    || (data[4] == 0xf0)) {
			/* trigger next measurement read */
			result =
			    inv_serial_single_write(mlsl_handle,
						    pdata->address,
						    HMC_REG_MODE,
						    HMC_MODE_SINGLE);
			if (result) {
				LOG_RESULT_LOCATION(result);
				return result;
			}
			return INV_ERROR_COMPASS_DATA_OVERFLOW;
		}
		/* convert to fixed point and apply sensitivity correction for
		   Z-axis */
		axisFixed =
		    (short)((unsigned short)data[5] +
			    (unsigned short)data[4] * 256);
		/* scale up by 1.125 (36/32) */
		axisFixed = (short)(axisFixed * 36);
		data[4] = axisFixed >> 8;
		data[5] = axisFixed & 0xFF;

		axisFixed =
		    (short)((unsigned short)data[3] +
			    (unsigned short)data[2] * 256);
		axisFixed = (short)(axisFixed * 32);
		data[2] = axisFixed >> 8;
		data[3] = axisFixed & 0xFF;

		axisFixed =
		    (short)((unsigned short)data[1] +
			    (unsigned short)data[0] * 256);
		axisFixed = (short)(axisFixed * 32);
		data[0] = axisFixed >> 8;
		data[1] = axisFixed & 0xFF;

		/* trigger next measurement read */
		result =
		    inv_serial_single_write(mlsl_handle, pdata->address,
					    HMC_REG_MODE, HMC_MODE_SINGLE);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}

		return INV_SUCCESS;
	} else {
		/* trigger next measurement read */
		result =
		    inv_serial_single_write(mlsl_handle, pdata->address,
					    HMC_REG_MODE, HMC_MODE_SINGLE);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}

		return INV_ERROR_COMPASS_DATA_NOT_READY;
	}
}

static struct ext_slave_descr hmc5883_descr = {
	.init             = NULL,
	.exit             = NULL,
	.suspend          = hmc5883_suspend,
	.resume           = hmc5883_resume,
	.read             = hmc5883_read,
	.config           = NULL,
	.get_config       = NULL,
	.name             = "hmc5883",
	.type             = EXT_SLAVE_TYPE_COMPASS,
	.id               = COMPASS_ID_HMC5883,
	.read_reg         = 0x06,
	.read_len         = 6,
	.endian           = EXT_SLAVE_BIG_ENDIAN,
	.range            = {10673, 6156},
	.trigger          = NULL,
};

static
struct ext_slave_descr *hmc5883_get_slave_descr(void)
{
	return &hmc5883_descr;
}

/* -------------------------------------------------------------------------- */
struct hmc5883_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int hmc5883_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct hmc5883_mod_private_data *private_data;
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
					hmc5883_get_slave_descr);
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

static int hmc5883_mod_remove(struct i2c_client *client)
{
	struct hmc5883_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				hmc5883_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id hmc5883_mod_id[] = {
	{ "hmc5883", COMPASS_ID_HMC5883 },
	{}
};

MODULE_DEVICE_TABLE(i2c, hmc5883_mod_id);

static struct i2c_driver hmc5883_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = hmc5883_mod_probe,
	.remove = hmc5883_mod_remove,
	.id_table = hmc5883_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "hmc5883_mod",
		   },
	.address_list = normal_i2c,
};

static int __init hmc5883_mod_init(void)
{
	int res = i2c_add_driver(&hmc5883_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "hmc5883_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit hmc5883_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&hmc5883_mod_driver);
}

module_init(hmc5883_mod_init);
module_exit(hmc5883_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate HMC5883 sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("hmc5883_mod");

/**
 *  @}
 */
