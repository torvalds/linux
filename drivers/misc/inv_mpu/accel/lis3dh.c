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
 *      @file   lis3dh.c
 *      @brief  Accelerometer setup and handling methods for ST LIS3DH.
 */

/* -------------------------------------------------------------------------- */

#undef MPL_LOG_NDEBUG
#define MPL_LOG_NDEBUG 0

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

/* full scale setting - register & mask */
#define LIS3DH_CTRL_REG1         (0x20)
#define LIS3DH_CTRL_REG2         (0x21)
#define LIS3DH_CTRL_REG3         (0x22)
#define LIS3DH_CTRL_REG4         (0x23)
#define LIS3DH_CTRL_REG5         (0x24)
#define LIS3DH_CTRL_REG6         (0x25)
#define LIS3DH_REFERENCE         (0x26)
#define LIS3DH_STATUS_REG        (0x27)
#define LIS3DH_OUT_X_L           (0x28)
#define LIS3DH_OUT_X_H           (0x29)
#define LIS3DH_OUT_Y_L           (0x2a)
#define LIS3DH_OUT_Y_H           (0x2b)
#define LIS3DH_OUT_Z_L           (0x2c)
#define LIS3DH_OUT_Z_H           (0x2d)

#define LIS3DH_INT1_CFG          (0x30)
#define LIS3DH_INT1_SRC          (0x31)
#define LIS3DH_INT1_THS          (0x32)
#define LIS3DH_INT1_DURATION     (0x33)

#define LIS3DH_MAX_DUR (0x7F)

/* -------------------------------------------------------------------------- */

struct lis3dh_config {
	unsigned long odr;
	unsigned int fsr;	/* full scale range mg */
	unsigned int ths;	/* Motion no-motion thseshold mg */
	unsigned int dur;	/* Motion no-motion duration ms */
	unsigned char reg_ths;
	unsigned char reg_dur;
	unsigned char ctrl_reg1;
	unsigned char irq_type;
	unsigned char mot_int1_cfg;
};

struct lis3dh_private_data {
	struct lis3dh_config suspend;
	struct lis3dh_config resume;
};

/* -------------------------------------------------------------------------- */

static int lis3dh_set_ths(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct lis3dh_config *config, int apply, long ths)
{
	int result = INV_SUCCESS;
	if ((unsigned int)ths > 1000 * config->fsr)
		ths = (long)1000 * config->fsr;

	if (ths < 0)
		ths = 0;

	config->ths = ths;
	config->reg_ths = (unsigned char)(long)((ths * 128L) / (config->fsr));
	MPL_LOGV("THS: %d, 0x%02x\n", config->ths, (int)config->reg_ths);
	if (apply)
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 LIS3DH_INT1_THS,
						 config->reg_ths);
	return result;
}

static int lis3dh_set_dur(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct lis3dh_config *config, int apply, long dur)
{
	int result = INV_SUCCESS;
	long reg_dur = (dur * config->odr) / 1000000L;
	config->dur = dur;

	if (reg_dur > LIS3DH_MAX_DUR)
		reg_dur = LIS3DH_MAX_DUR;

	config->reg_dur = (unsigned char)reg_dur;
	MPL_LOGV("DUR: %d, 0x%02x\n", config->dur, (int)config->reg_dur);
	if (apply)
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 LIS3DH_INT1_DURATION,
						 (unsigned char)reg_dur);
	return result;
}

/**
 * Sets the IRQ to fire when one of the IRQ events occur.  Threshold and
 * duration will not be used uless the type is MOT or NMOT.
 *
 * @param config configuration to apply to, suspend or resume
 * @param irq_type The type of IRQ.  Valid values are
 * - MPU_SLAVE_IRQ_TYPE_NONE
 * - MPU_SLAVE_IRQ_TYPE_MOTION
 * - MPU_SLAVE_IRQ_TYPE_DATA_READY
 */
static int lis3dh_set_irq(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct lis3dh_config *config,
			  int apply, long irq_type)
{
	int result = INV_SUCCESS;
	unsigned char reg1;
	unsigned char reg2;

	config->irq_type = (unsigned char)irq_type;
	if (irq_type == MPU_SLAVE_IRQ_TYPE_DATA_READY) {
		reg1 = 0x10;
		reg2 = 0x00;
	} else if (irq_type == MPU_SLAVE_IRQ_TYPE_MOTION) {
		reg1 = 0x40;
		reg2 = config->mot_int1_cfg;
	} else {
		reg1 = 0x00;
		reg2 = 0x00;
	}

	if (apply) {
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 LIS3DH_CTRL_REG3, reg1);
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 LIS3DH_INT1_CFG, reg2);
	}

	return result;
}

/**
 * Set the Output data rate for the particular configuration
 *
 * @param config Config to modify with new ODR
 * @param odr Output data rate in units of 1/1000Hz
 */
static int lis3dh_set_odr(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct lis3dh_config *config, int apply, long odr)
{
	unsigned char bits;
	int result = INV_SUCCESS;

	if (odr > 400000L) {
		config->odr = 1250000L;
		bits = 0x90;
	} else if (odr > 200000L) {
		config->odr = 400000L;
		bits = 0x70;
	} else if (odr > 100000L) {
		config->odr = 200000L;
		bits = 0x60;
	} else if (odr > 50000) {
		config->odr = 100000L;
		bits = 0x50;
	} else if (odr > 25000) {
		config->odr = 50000;
		bits = 0x40;
	} else if (odr > 10000) {
		config->odr = 25000;
		bits = 0x30;
	} else if (odr > 1000) {
		config->odr = 10000;
		bits = 0x20;
	} else if (odr > 500) {
		config->odr = 1000;
		bits = 0x10;
	} else {
		config->odr = 0;
		bits = 0;
	}

	config->ctrl_reg1 = bits | (config->ctrl_reg1 & 0xf);
	lis3dh_set_dur(mlsl_handle, pdata, config, apply, config->dur);
	MPL_LOGV("ODR: %ld, 0x%02x\n", config->odr, (int)config->ctrl_reg1);
	if (apply)
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 LIS3DH_CTRL_REG1,
						 config->ctrl_reg1);
	return result;
}

/**
 * Set the full scale range of the accels
 *
 * @param config pointer to configuration
 * @param fsr requested full scale range
 */
static int lis3dh_set_fsr(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct lis3dh_config *config, int apply, long fsr)
{
	int result = INV_SUCCESS;
	unsigned char reg1 = 0x48;

	if (fsr <= 2048) {
		config->fsr = 2048;
	} else if (fsr <= 4096) {
		reg1 |= 0x10;
		config->fsr = 4096;
	} else if (fsr <= 8192) {
		reg1 |= 0x20;
		config->fsr = 8192;
	} else {
		reg1 |= 0x30;
		config->fsr = 16348;
	}

	lis3dh_set_ths(mlsl_handle, pdata, config, apply, config->ths);
	MPL_LOGV("FSR: %d\n", config->fsr);
	if (apply)
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 LIS3DH_CTRL_REG4, reg1);

	return result;
}

static int lis3dh_suspend(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result = INV_SUCCESS;
	unsigned char reg1;
	unsigned char reg2;
	struct lis3dh_private_data *private_data = pdata->private_data;

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG1,
					 private_data->suspend.ctrl_reg1);

	reg1 = 0x48;
	if (private_data->suspend.fsr == 16384)
		reg1 |= 0x30;
	else if (private_data->suspend.fsr == 8192)
		reg1 |= 0x20;
	else if (private_data->suspend.fsr == 4096)
		reg1 |= 0x10;
	else if (private_data->suspend.fsr == 2048)
		reg1 |= 0x00;

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG4, reg1);
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_INT1_THS,
					 private_data->suspend.reg_ths);
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_INT1_DURATION,
					 private_data->suspend.reg_dur);

	if (private_data->suspend.irq_type == MPU_SLAVE_IRQ_TYPE_DATA_READY) {
		reg1 = 0x10;
		reg2 = 0x00;
	} else if (private_data->suspend.irq_type ==
		   MPU_SLAVE_IRQ_TYPE_MOTION) {
		reg1 = 0x40;
		reg2 = private_data->suspend.mot_int1_cfg;
	} else {
		reg1 = 0x00;
		reg2 = 0x00;
	}
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG3, reg1);
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_INT1_CFG, reg2);
	result = inv_serial_read(mlsl_handle, pdata->address,
				 LIS3DH_CTRL_REG6, 1, &reg1);

	return result;
}

static int lis3dh_resume(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg1;
	unsigned char reg2;
	struct lis3dh_private_data *private_data = pdata->private_data;

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG1,
					 private_data->resume.ctrl_reg1);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	msleep(6);

	/* Full Scale */
	reg1 = 0x48;
	if (private_data->suspend.fsr == 16384)
		reg1 |= 0x30;
	else if (private_data->suspend.fsr == 8192)
		reg1 |= 0x20;
	else if (private_data->suspend.fsr == 4096)
		reg1 |= 0x10;
	else if (private_data->suspend.fsr == 2048)
		reg1 |= 0x00;

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG4, reg1);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (private_data->resume.irq_type == MPU_SLAVE_IRQ_TYPE_DATA_READY) {
		reg1 = 0x10;
		reg2 = 0x00;
	} else if (private_data->resume.irq_type == MPU_SLAVE_IRQ_TYPE_MOTION) {
		reg1 = 0x40;
		reg2 = private_data->resume.mot_int1_cfg;
	} else {
		reg1 = 0x00;
		reg2 = 0x00;
	}
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG3, reg1);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_INT1_THS,
					 private_data->resume.reg_ths);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_INT1_DURATION,
					 private_data->resume.reg_dur);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_INT1_CFG, reg2);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = inv_serial_read(mlsl_handle, pdata->address,
				 LIS3DH_CTRL_REG6, 1, &reg1);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return result;
}

static int lis3dh_read(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       unsigned char *data)
{
	int result = INV_SUCCESS;
	result = inv_serial_read(mlsl_handle, pdata->address,
				 LIS3DH_STATUS_REG, 1, data);
	if (data[0] & 0x0F) {
		result = inv_serial_read(mlsl_handle, pdata->address,
					 slave->read_reg, slave->read_len,
					 data);
		return result;
	} else
		return INV_ERROR_ACCEL_DATA_NOT_READY;
}

static int lis3dh_init(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result;
	long range;
	struct lis3dh_private_data *private_data;
	private_data = (struct lis3dh_private_data *)
	    kzalloc(sizeof(struct lis3dh_private_data), GFP_KERNEL);

	if (!private_data)
		return INV_ERROR_MEMORY_EXAUSTED;

	pdata->private_data = private_data;

	private_data->resume.ctrl_reg1 = 0x67;
	private_data->suspend.ctrl_reg1 = 0x18;
	private_data->resume.mot_int1_cfg = 0x95;
	private_data->suspend.mot_int1_cfg = 0x2a;

	lis3dh_set_odr(mlsl_handle, pdata, &private_data->suspend, false, 0);
	lis3dh_set_odr(mlsl_handle, pdata, &private_data->resume,
		       false, 200000L);

	range = range_fixedpoint_to_long_mg(slave->range);
	lis3dh_set_fsr(mlsl_handle, pdata, &private_data->suspend,
			false, range);
	lis3dh_set_fsr(mlsl_handle, pdata, &private_data->resume,
			false, range);

	lis3dh_set_ths(mlsl_handle, pdata, &private_data->suspend,
			false, 80);
	lis3dh_set_ths(mlsl_handle, pdata, &private_data->resume,
			false, 40);

	lis3dh_set_dur(mlsl_handle, pdata, &private_data->suspend,
			false, 1000);
	lis3dh_set_dur(mlsl_handle, pdata, &private_data->resume,
			false, 2540);

	lis3dh_set_irq(mlsl_handle, pdata, &private_data->suspend,
			false, MPU_SLAVE_IRQ_TYPE_NONE);
	lis3dh_set_irq(mlsl_handle, pdata, &private_data->resume,
			false, MPU_SLAVE_IRQ_TYPE_NONE);

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 LIS3DH_CTRL_REG1, 0x07);
	msleep(6);

	return INV_SUCCESS;
}

static int lis3dh_exit(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	kfree(pdata->private_data);
	return INV_SUCCESS;
}

static int lis3dh_config(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata,
			 struct ext_slave_config *data)
{
	struct lis3dh_private_data *private_data = pdata->private_data;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		return lis3dh_set_odr(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		return lis3dh_set_odr(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		return lis3dh_set_fsr(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		return lis3dh_set_fsr(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_MOT_THS:
		return lis3dh_set_ths(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_NMOT_THS:
		return lis3dh_set_ths(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_MOT_DUR:
		return lis3dh_set_dur(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_NMOT_DUR:
		return lis3dh_set_dur(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
		return lis3dh_set_irq(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
		return lis3dh_set_irq(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};
	return INV_SUCCESS;
}

static int lis3dh_get_config(void *mlsl_handle,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata,
			     struct ext_slave_config *data)
{
	struct lis3dh_private_data *private_data = pdata->private_data;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->suspend.odr;
		break;
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->resume.odr;
		break;
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->suspend.fsr;
		break;
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->resume.fsr;
		break;
	case MPU_SLAVE_CONFIG_MOT_THS:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->suspend.ths;
		break;
	case MPU_SLAVE_CONFIG_NMOT_THS:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->resume.ths;
		break;
	case MPU_SLAVE_CONFIG_MOT_DUR:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->suspend.dur;
		break;
	case MPU_SLAVE_CONFIG_NMOT_DUR:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->resume.dur;
		break;
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->suspend.irq_type;
		break;
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
		(*(unsigned long *)data->data) =
		    (unsigned long)private_data->resume.irq_type;
		break;
	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return INV_SUCCESS;
}

static struct ext_slave_descr lis3dh_descr = {
	.init             = lis3dh_init,
	.exit             = lis3dh_exit,
	.suspend          = lis3dh_suspend,
	.resume           = lis3dh_resume,
	.read             = lis3dh_read,
	.config           = lis3dh_config,
	.get_config       = lis3dh_get_config,
	.name             = "lis3dh",
	.type             = EXT_SLAVE_TYPE_ACCEL,
	.id               = ACCEL_ID_LIS3DH,
	.read_reg         = 0x28 | 0x80, /* 0x80 for burst reads */
	.read_len         = 6,
	.endian           = EXT_SLAVE_BIG_ENDIAN,
	.range            = {2, 480},
	.trigger          = NULL,
};

static
struct ext_slave_descr *lis3dh_get_slave_descr(void)
{
	return &lis3dh_descr;
}

/* -------------------------------------------------------------------------- */
struct lis3dh_mod_private_data {
	struct i2c_client *client;
	struct ext_slave_platform_data *pdata;
};

static unsigned short normal_i2c[] = { I2C_CLIENT_END };

static int lis3dh_mod_probe(struct i2c_client *client,
			   const struct i2c_device_id *devid)
{
	struct ext_slave_platform_data *pdata;
	struct lis3dh_mod_private_data *private_data;
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
					lis3dh_get_slave_descr);
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

static int lis3dh_mod_remove(struct i2c_client *client)
{
	struct lis3dh_mod_private_data *private_data =
		i2c_get_clientdata(client);

	dev_dbg(&client->adapter->dev, "%s\n", __func__);

	inv_mpu_unregister_slave(client, private_data->pdata,
				lis3dh_get_slave_descr);

	kfree(private_data);
	return 0;
}

static const struct i2c_device_id lis3dh_mod_id[] = {
	{ "lis3dh", ACCEL_ID_LIS3DH },
	{}
};

MODULE_DEVICE_TABLE(i2c, lis3dh_mod_id);

static struct i2c_driver lis3dh_mod_driver = {
	.class = I2C_CLASS_HWMON,
	.probe = lis3dh_mod_probe,
	.remove = lis3dh_mod_remove,
	.id_table = lis3dh_mod_id,
	.driver = {
		   .owner = THIS_MODULE,
		   .name = "lis3dh_mod",
		   },
	.address_list = normal_i2c,
};

static int __init lis3dh_mod_init(void)
{
	int res = i2c_add_driver(&lis3dh_mod_driver);
	pr_info("%s: Probe name %s\n", __func__, "lis3dh_mod");
	if (res)
		pr_err("%s failed\n", __func__);
	return res;
}

static void __exit lis3dh_mod_exit(void)
{
	pr_info("%s\n", __func__);
	i2c_del_driver(&lis3dh_mod_driver);
}

module_init(lis3dh_mod_init);
module_exit(lis3dh_mod_exit);

MODULE_AUTHOR("Invensense Corporation");
MODULE_DESCRIPTION("Driver to integrate LIS3DH sensor with the MPU");
MODULE_LICENSE("GPL");
MODULE_ALIAS("lis3dh_mod");

/*
 *  @}
 */
