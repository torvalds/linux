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
 *  @brief      Provides the interface to setup and handle an accelerometer.
 *
 *  @{
 *      @file   adxl34x.c
 *      @brief  Accelerometer setup and handling methods for AD adxl345 and
 *              adxl346.
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

/* registers */
#define ADXL34X_ODR_REG			(0x2C)
#define ADXL34X_PWR_REG			(0x2D)
#define ADXL34X_DATAFORMAT_REG		(0x31)

/* masks */
#define ADXL34X_ODR_MASK		(0x0F)
#define ADXL34X_PWR_SLEEP_MASK		(0x04)
#define ADXL34X_PWR_MEAS_MASK		(0x08)
#define ADXL34X_DATAFORMAT_JUSTIFY_MASK	(0x04)
#define ADXL34X_DATAFORMAT_FSR_MASK	(0x03)

/* -------------------------------------------------------------------------- */

struct adxl34x_config {
	unsigned int odr;		/** < output data rate in mHz */
	unsigned int fsr;		/** < full scale range mg */
	unsigned int fsr_reg_mask;	/** < register setting for fsr */
};

struct adxl34x_private_data {
	struct adxl34x_config suspend;	/** < suspend configuration */
	struct adxl34x_config resume;	/** < resume configuration */
};

/**
 *  @brief Set the output data rate for the particular configuration.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param pdata
 *             a pointer to the slave platform data.
 *  @param config
 *             Config to modify with new ODR.
 *  @param apply
 *             whether to apply immediately or save the settings to be applied
 *             at the next resume.
 *  @param odr
 *             Output data rate in units of 1/1000Hz (mHz).
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_set_odr(void *mlsl_handle,
			   struct ext_slave_platform_data *pdata,
			   struct adxl34x_config *config,
			   int apply,
			   long odr)
{
	int result = INV_SUCCESS;
	unsigned char new_odr_mask;

	/* ADXL346 (Rev. A) pages 13, 24 */
	if (odr >= 3200000) {
		new_odr_mask = 0x0F;
		config->odr = 3200000;
	} else if (odr >= 1600000) {
		new_odr_mask = 0x0E;
		config->odr = 1600000;
	} else if (odr >= 800000) {
		new_odr_mask = 0x0D;
		config->odr = 800000;
	} else if (odr >= 400000) {
		new_odr_mask = 0x0C;
		config->odr = 400000;
	} else if (odr >= 200000) {
		new_odr_mask = 0x0B;
		config->odr = 200000;
	} else if (odr >= 100000) {
		new_odr_mask = 0x0A;
		config->odr = 100000;
	} else if (odr >= 50000) {
		new_odr_mask = 0x09;
		config->odr = 50000;
	} else if (odr >= 25000) {
		new_odr_mask = 0x08;
		config->odr = 25000;
	} else if (odr >= 12500) {
		new_odr_mask = 0x07;
		config->odr = 12500;
	} else if (odr >= 6250) {
		new_odr_mask = 0x06;
		config->odr = 6250;
	} else if (odr >= 3130) {
		new_odr_mask = 0x05;
		config->odr = 3130;
	} else if (odr >= 1560) {
		new_odr_mask = 0x04;
		config->odr = 1560;
	} else if (odr >= 780) {
		new_odr_mask = 0x03;
		config->odr = 780;
	} else if (odr >= 390) {
		new_odr_mask = 0x02;
		config->odr = 390;
	} else if (odr >= 200) {
		new_odr_mask = 0x01;
		config->odr = 200;
	} else {
		new_odr_mask = 0x00;
		config->odr = 100;
	}

	if (apply) {
		unsigned char reg_odr;
		result = inv_serial_read(mlsl_handle, pdata->address,
				ADXL34X_ODR_REG, 1, &reg_odr);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		reg_odr &= ~ADXL34X_ODR_MASK;
		reg_odr |= new_odr_mask;
		result = inv_serial_single_write(mlsl_handle, pdata->address,
				ADXL34X_ODR_REG, reg_odr);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		MPL_LOGV("ODR: %d mHz\n", config->odr);
	}
	return result;
}

/**
 *  @brief Set the full scale range of the accels
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param pdata
 *             a pointer to the slave platform data.
 *  @param config
 *             pointer to configuration.
 *  @param apply
 *             whether to apply immediately or save the settings to be applied
 *             at the next resume.
 *  @param fsr
 *             requested full scale range in milli gees (mg).
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_set_fsr(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct adxl34x_config *config,
			  int apply,
			  long fsr)
{
	int result = INV_SUCCESS;

	if (fsr <= 2000) {
		config->fsr_reg_mask = 0x00;
		config->fsr = 2000;
	} else if (fsr <= 4000) {
		config->fsr_reg_mask = 0x01;
		config->fsr = 4000;
	} else if (fsr <= 8000) {
		config->fsr_reg_mask = 0x02;
		config->fsr = 8000;
	} else { /* 8001 -> oo */
		config->fsr_reg_mask = 0x03;
		config->fsr = 16000;
	}

	if (apply) {
		unsigned char reg_df;
		result = inv_serial_read(mlsl_handle, pdata->address,
				ADXL34X_DATAFORMAT_REG, 1, &reg_df);
		reg_df &= ~ADXL34X_DATAFORMAT_FSR_MASK;
		result = inv_serial_single_write(mlsl_handle, pdata->address,
				ADXL34X_DATAFORMAT_REG,
				reg_df | config->fsr_reg_mask);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		MPL_LOGV("FSR: %d mg\n", config->fsr);
	}
	return result;
}

/**
 *  @brief facility to retrieve the device configuration.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *  @param data
 *             a pointer to store the returned configuration data structure.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_get_config(void *mlsl_handle,
		    struct ext_slave_descr *slave,
				struct ext_slave_platform_data *pdata,
				struct ext_slave_config *data)
{
	struct adxl34x_private_data *private_data =
			(struct adxl34x_private_data *)(pdata->private_data);

	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		(*(unsigned long *)data->data) =
			(unsigned long) private_data->suspend.odr;
		break;
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		(*(unsigned long *)data->data) =
			(unsigned long) private_data->resume.odr;
		break;
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		(*(unsigned long *)data->data) =
			(unsigned long) private_data->suspend.fsr;
		break;
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		(*(unsigned long *)data->data) =
			(unsigned long) private_data->resume.fsr;
		break;
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}

/**
 *  @brief device configuration facility.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *  @param data
 *             a pointer to the configuration data structure.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_config(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata,
			 struct ext_slave_config *data)
{
	struct adxl34x_private_data *private_data =
			(struct adxl34x_private_data *)(pdata->private_data);

	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		return adxl34x_set_odr(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply,
				      *((long *)data->data));
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		return adxl34x_set_odr(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply,
				      *((long *)data->data));
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		return adxl34x_set_fsr(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply,
				      *((long *)data->data));
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		return adxl34x_set_fsr(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply,
				      *((long *)data->data));
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};
	return INV_SUCCESS;
}

/**
 *  @brief suspends the device to put it in its lowest power mode.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_suspend(void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata)
{
	int result;

	/*
	struct adxl34x_config *suspend_config =
		&((struct adxl34x_private_data *)pdata->private_data)->suspend;

	result = adxl34x_set_odr(mlsl_handle, pdata, suspend_config,
				true, suspend_config->odr);
	if (result) {
	LOG_RESULT_LOCATION(result);
	return result;
}
	result = adxl34x_set_fsr(mlsl_handle, pdata, suspend_config,
				true, suspend_config->fsr);
	if (result) {
	LOG_RESULT_LOCATION(result);
	return result;
}
	*/

	/*
	  Page 25
	  When clearing the sleep bit, it is recommended that the part
	  be placed into standby mode and then set back to measurement mode
	  with a subsequent write.
	  This is done to ensure that the device is properly biased if sleep
	  mode is manually disabled; otherwise, the first few samples of data
	  after the sleep bit is cleared may have additional noise,
	  especially if the device was asleep when the bit was cleared. */

	/* go in standy-by mode (suspends measurements) */
	result = inv_serial_single_write(mlsl_handle, pdata->address,
			ADXL34X_PWR_REG, ADXL34X_PWR_MEAS_MASK);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	/* and then in sleep */
	result = inv_serial_single_write(mlsl_handle, pdata->address,
			ADXL34X_PWR_REG,
			ADXL34X_PWR_MEAS_MASK | ADXL34X_PWR_SLEEP_MASK);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

/**
 *  @brief resume the device in the proper power state given the configuration
 *         chosen.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_resume(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	struct adxl34x_config *resume_config =
		&((struct adxl34x_private_data *)pdata->private_data)->resume;
	unsigned char reg;

	/*
	  Page 25
	  When clearing the sleep bit, it is recommended that the part
	  be placed into standby mode and then set back to measurement mode
	  with a subsequent write.
	  This is done to ensure that the device is properly biased if sleep
	  mode is manually disabled; otherwise, the first few samples of data
	  after the sleep bit is cleared may have additional noise,
	  especially if the device was asleep when the bit was cleared. */

	/* remove sleep, but leave in stand-by */
	result = inv_serial_single_write(mlsl_handle, pdata->address,
			ADXL34X_PWR_REG, ADXL34X_PWR_MEAS_MASK);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = adxl34x_set_odr(mlsl_handle, pdata, resume_config,
				true, resume_config->odr);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/*
	  -> FSR
	  -> Justiy bit for Big endianess
	  -> resulution to 10 bits
	*/
	reg = ADXL34X_DATAFORMAT_JUSTIFY_MASK;
	reg |= resume_config->fsr_reg_mask;
	result = inv_serial_single_write(mlsl_handle, pdata->address,
			ADXL34X_DATAFORMAT_REG, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* go in measurement mode */
	result = inv_serial_single_write(mlsl_handle, pdata->address,
			ADXL34X_PWR_REG, 0x00);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* DATA_FORMAT: full resolution of +/-2g; data is left justified */
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 0x31, reg);

	return result;
}

/**
 *  @brief one-time device driver initialization function.
 *         If the driver is built as a kernel module, this function will be
 *         called when the module is loaded in the kernel.
 *         If the driver is built-in in the kernel, this function will be
 *         called at boot time.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_init(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata)
{
	int result;
	long range;

	struct adxl34x_private_data *private_data;
	private_data = (struct adxl34x_private_data *)
	    kzalloc(sizeof(struct adxl34x_private_data), GFP_KERNEL);

	if (!private_data)
		return INV_ERROR_MEMORY_EXAUSTED;

	pdata->private_data = private_data;

	result = adxl34x_set_odr(mlsl_handle, pdata, &private_data->suspend,
				false, 0);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = adxl34x_set_odr(mlsl_handle, pdata, &private_data->resume,
				false, 200000);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	range = range_fixedpoint_to_long_mg(slave->range);
	result = adxl34x_set_fsr(mlsl_handle, pdata, &private_data->suspend,
				false, range);
	result = adxl34x_set_fsr(mlsl_handle, pdata, &private_data->resume,
				false, range);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = adxl34x_suspend(mlsl_handle, slave, pdata);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return result;
}

/**
 *  @brief one-time device driver exit function.
 *         If the driver is built as a kernel module, this function will be
 *         called when the module is removed from the kernel.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_exit(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	kfree(pdata->private_data);
	return INV_SUCCESS;
}

/**
 *  @brief read the sensor data from the device.
 *
 *  @param mlsl_handle
 *             the handle to the serial channel the device is connected to.
 *  @param slave
 *             a pointer to the slave descriptor data structure.
 *  @param pdata
 *             a pointer to the slave platform data.
 *  @param data
 *             a buffer to store the data read.
 *
 *  @return INV_SUCCESS if successful or a non-zero error code.
 */
static int adxl34x_read(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			unsigned char *data)
{
	int result;
	result = inv_serial_read(mlsl_handle, pdata->address,
				 slave->read_reg, slave->read_len, data);
	return result;
}

static struct ext_slave_descr adxl34x_descr = {
	.init             = adxl34x_init,
	.exit             = adxl34x_exit,
	.suspend          = adxl34x_suspend,
	.resume           = adxl34x_resume,
	.read             = adxl34x_read,
	.config           = adxl34x_config,
	.get_config       = adxl34x_get_config,
	.name             = "adxl34x",	/* 5 or 6 */
	.type             = EXT_SLAVE_TYPE_ACCEL,
	.id               = ACCEL_ID_ADXL34X,
	.read_reg         = 0x32,
	.read_len         = 6,
	.endian           = EXT_SLAVE_LITTLE_ENDIAN,
	.range            = {2, 0},
	.trigger          = NULL,
};

static
struct ext_slave_descr *adxl34x_get_slave_descr(void)
{
	return &adxl34x_descr;
}

/* -------------------------------------------------------------------------- */
struct adxl34x_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int adxl34x_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct adxl34x_mod_private_data *private_data;
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
					adxl34x_get_slave_descr);
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

static int adxl34x_mod_remove(struct i2c_client *client)
{
	struct adxl34x_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				adxl34x_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id adxl34x_mod_id[] = {
	{ "adxl34x", ACCEL_ID_ADXL34X },
	{}
};

MODULE_DEVICE_TABLE(i2c, adxl34x_mod_id);

static struct i2c_driver adxl34x_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = adxl34x_mod_probe,
	.remove = adxl34x_mod_remove,
	.id_table = adxl34x_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "adxl34x_mod",
		   },
	.address_list = normal_i2c,
};

static int __init adxl34x_mod_init(void)
{
	int res = i2c_add_driver(&adxl34x_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "adxl34x_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit adxl34x_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&adxl34x_mod_driver);
}

module_init(adxl34x_mod_init);
module_exit(adxl34x_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate ADXL34X sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("adxl34x_mod");

/**
 *  @}
 */
