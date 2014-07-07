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
 *      @file   mpu6050.c
 *      @brief  Accelerometer setup and handling methods for Invensense MPU6050
 */

/* -------------------------------------------------------------------------- */

#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "mpu-dev.h"

#include <log.h>
#include <linux/mpu.h>
#include "mpu6050b1.h"
#include "mlsl.h"
#include "mldl_cfg.h"
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

/* -------------------------------------------------------------------------- */

struct mpu6050_config {
	unsigned int odr;		/**< output data rate 1/1000 Hz */
	unsigned int fsr;		/**< full scale range mg */
	unsigned int ths;		/**< mot/no-mot thseshold mg */
	unsigned int dur;		/**< mot/no-mot duration ms */
	unsigned int irq_type;		/**< irq type */
};

struct mpu6050_private_data {
	struct mpu6050_config suspend;
	struct mpu6050_config resume;
	struct mldl_cfg *mldl_cfg_ref;
};

/* -------------------------------------------------------------------------- */

static int mpu6050_set_mldl_cfg_ref(void *mlsl_handle,
			struct ext_slave_platform_data *pdata,
			struct mldl_cfg *mldl_cfg_ref)
{
	struct mpu6050_private_data *private_data =
			(struct mpu6050_private_data *)pdata->private_data;
	private_data->mldl_cfg_ref = mldl_cfg_ref;
	return 0;
}
static int mpu6050_set_lp_mode(void *mlsl_handle,
			struct ext_slave_platform_data *pdata,
			unsigned char lpa_freq)
{
	unsigned char b = 0;
	/* Reducing the duration setting for lp mode */
	b = 1;
	inv_serial_single_write(mlsl_handle, pdata->address,
				MPUREG_ACCEL_MOT_DUR, b);
	/* Setting the cycle bit and LPA wake up freq */
	inv_serial_read(mlsl_handle, pdata->address, MPUREG_PWR_MGMT_1, 1,
			&b);
	b |= BIT_CYCLE | BIT_PD_PTAT;
	inv_serial_single_write(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_1,
				b);
	inv_serial_read(mlsl_handle, pdata->address,
			MPUREG_PWR_MGMT_2, 1, &b);
	b |= lpa_freq & BITS_LPA_WAKE_CTRL;
	inv_serial_single_write(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_2, b);

	return INV_SUCCESS;
}

static int mpu6050_set_fp_mode(void *mlsl_handle,
				struct ext_slave_platform_data *pdata)
{
	unsigned char b;
	struct mpu6050_private_data *private_data =
			(struct mpu6050_private_data *)pdata->private_data;
	/* Resetting the cycle bit and LPA wake up freq */
	inv_serial_read(mlsl_handle, pdata->address,
			MPUREG_PWR_MGMT_1, 1, &b);
	b &= ~BIT_CYCLE & ~BIT_PD_PTAT;
	inv_serial_single_write(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_1, b);
	inv_serial_read(mlsl_handle, pdata->address,
			MPUREG_PWR_MGMT_2, 1, &b);
	b &= ~BITS_LPA_WAKE_CTRL;
	inv_serial_single_write(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_2, b);
	/* Resetting the duration setting for fp mode */
	b = (unsigned char)private_data->suspend.ths / ACCEL_MOT_DUR_LSB;
	inv_serial_single_write(mlsl_handle, pdata->address,
				MPUREG_ACCEL_MOT_DUR, b);

	return INV_SUCCESS;
}
/**
 * Record the odr for use in computing duration values.
 *
 * @param config Config to set, suspend or resume structure
 * @param odr output data rate in 1/1000 hz
 */
static int mpu6050_set_odr(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct mpu6050_config *config, long apply, long odr)
{
	int result;
	unsigned char b;
	unsigned char lpa_freq = 1; /* Default value */
	long base;
	int total_divider;
	struct mpu6050_private_data *private_data =
			(struct mpu6050_private_data *)pdata->private_data;
	struct mldl_cfg *mldl_cfg_ref =
			(struct mldl_cfg *)private_data->mldl_cfg_ref;

	if (mldl_cfg_ref) {
		base = 1000 *
			inv_mpu_get_sampling_rate_hz(mldl_cfg_ref->mpu_gyro_cfg)
			* (mldl_cfg_ref->mpu_gyro_cfg->divider + 1);
	} else {
		/* have no reference to mldl_cfg => assume base rate is 1000 */
		base = 1000000L;
	}

	if (odr != 0) {
		total_divider = (base / odr) - 1;
		/* final odr MAY be different from requested odr due to
		   integer truncation */
		config->odr = base / (total_divider + 1);
	} else {
		config->odr = 0;
		return 0;
	}

	/* if the DMP and/or gyros are on, don't set the ODR =>
	   the DMP/gyro mldl_cfg->divider setting will handle it */
	if (apply
	    && (mldl_cfg_ref &&
	    !(mldl_cfg_ref->inv_mpu_cfg->requested_sensors &
		    INV_DMP_PROCESSOR))) {
		result = inv_serial_single_write(mlsl_handle, pdata->address,
					MPUREG_SMPLRT_DIV,
					(unsigned char)total_divider);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		MPL_LOGI("ODR : %d mHz\n", config->odr);
	}
	/* Decide whether to put accel in LP mode or pull out of LP mode
	   based on the odr. */
	switch (odr) {
	case 1000:
		lpa_freq = BITS_LPA_WAKE_1HZ;
		break;
	case 2000:
		lpa_freq = BITS_LPA_WAKE_2HZ;
		break;
	case 10000:
		lpa_freq = BITS_LPA_WAKE_10HZ;
		break;
	case 40000:
		lpa_freq = BITS_LPA_WAKE_40HZ;
		break;
	default:
		inv_serial_read(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_1, 1, &b);
		b &= BIT_CYCLE;
		if (b == BIT_CYCLE) {
			MPL_LOGI(" Accel LP - > FP mode. \n ");
			mpu6050_set_fp_mode(mlsl_handle, pdata);
		}
	}
	/* If lpa_freq default value was changed, set into LP mode */
	if (lpa_freq != 1) {
		MPL_LOGI(" Accel FP - > LP mode. \n ");
		mpu6050_set_lp_mode(mlsl_handle, pdata, lpa_freq);
	}
	return 0;
}

static int mpu6050_set_fsr(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct mpu6050_config *config, long apply, long fsr)
{
	unsigned char fsr_mask;
	int result;

	if (fsr <= 2000) {
		config->fsr = 2000;
		fsr_mask = 0x00;
	} else if (fsr <= 4000) {
		config->fsr = 4000;
		fsr_mask = 0x08;
	} else if (fsr <= 8000) {
		config->fsr = 8000;
		fsr_mask = 0x10;
	} else { /* fsr = [8001, oo) */
		config->fsr = 16000;
		fsr_mask = 0x18;
	}

	if (apply) {
		unsigned char reg;
		result = inv_serial_read(mlsl_handle, pdata->address,
					 MPUREG_ACCEL_CONFIG, 1, &reg);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 MPUREG_ACCEL_CONFIG,
						 reg | fsr_mask);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		MPL_LOGV("FSR: %d\n", config->fsr);
	}
	return 0;
}

static int mpu6050_set_irq(void *mlsl_handle,
			  struct ext_slave_platform_data *pdata,
			  struct mpu6050_config *config, long apply,
			  long irq_type)
{

	/* HACK, no need for interrupts for MPU6050 accel
		- use of soft interrupt is required */
#if 0
	switch (irq_type) {
	case MPU_SLAVE_IRQ_TYPE_DATA_READY:
		config->irq_type = irq_type;
		reg_int_cfg = BIT_RAW_RDY_EN;
		break;
	/* todo: add MOTION, NO_MOTION, and FREEFALL */
	case MPU_SLAVE_IRQ_TYPE_NONE:
		/* Do nothing, not even set the interrupt because it is
		   shared with the gyro */
		config->irq_type = irq_type;
		return 0;
	default:
		return INV_ERROR_INVALID_PARAMETER;
	}

	if (apply) {
		result = inv_serial_single_write(mlsl_handle, pdata->address,
						 MPUREG_INT_ENABLE,
						 reg_int_cfg);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
		MPL_LOGV("irq_type: %d\n", config->irq_type);
	}
#endif

	return 0;
}

static int mpu6050_set_ths(void *mlsl_handle,
			  struct ext_slave_platform_data *slave,
			  struct mpu6050_config *config, long apply, long ths)
{
	if (ths < 0)
		ths = 0;

	config->ths = ths;
	MPL_LOGV("THS: %d\n", config->ths);
	return 0;
}

static int mpu6050_set_dur(void *mlsl_handle,
			  struct ext_slave_platform_data *slave,
			  struct mpu6050_config *config, long apply, long dur)
{
	if (dur < 0)
		dur = 0;

	config->dur = dur;
	MPL_LOGV("DUR: %d\n", config->dur);
	return 0;
}


static int mpu6050_init(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result;
	struct mpu6050_private_data *private_data;


	private_data = kzalloc(sizeof(*private_data), GFP_KERNEL);

	if (!private_data)
		return INV_ERROR_MEMORY_EXAUSTED;

	pdata->private_data = private_data;

	result = mpu6050_set_odr(mlsl_handle, pdata, &private_data->suspend,
				 false, 0);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_odr(mlsl_handle, pdata, &private_data->resume,
				 false, 200000);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_fsr(mlsl_handle, pdata, &private_data->suspend,
				 false, 2000);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_fsr(mlsl_handle, pdata, &private_data->resume,
				 false, 2000);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = mpu6050_set_irq(mlsl_handle, pdata, &private_data->suspend,
				 false, MPU_SLAVE_IRQ_TYPE_NONE);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_irq(mlsl_handle, pdata, &private_data->resume,
				 false, MPU_SLAVE_IRQ_TYPE_NONE);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = mpu6050_set_ths(mlsl_handle, pdata, &private_data->suspend,
				 false, 80);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_ths(mlsl_handle, pdata, &private_data->resume,
				 false, 40);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_dur(mlsl_handle, pdata, &private_data->suspend,
				 false, 1000);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_dur(mlsl_handle, pdata, &private_data->resume,
				 false, 2540);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return 0;
}

static int mpu6050_exit(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	kfree(pdata->private_data);
	pdata->private_data = NULL;
	return 0;
}

static int mpu6050_suspend(void *mlsl_handle,
			   struct ext_slave_descr *slave,
			   struct ext_slave_platform_data *pdata)
{
	unsigned char reg;
	int result;
	struct mpu6050_private_data *private_data =
			(struct mpu6050_private_data *)pdata->private_data;

	result = mpu6050_set_odr(mlsl_handle, pdata, &private_data->suspend,
				true, private_data->suspend.odr);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = mpu6050_set_irq(mlsl_handle, pdata, &private_data->suspend,
				true, private_data->suspend.irq_type);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	result = inv_serial_read(mlsl_handle, pdata->address,
				 MPUREG_PWR_MGMT_2, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg |= (BIT_STBY_XA | BIT_STBY_YA | BIT_STBY_ZA);

	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 MPUREG_PWR_MGMT_2, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	return 0;
}

static int mpu6050_resume(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	struct mpu6050_private_data *private_data =
		(struct mpu6050_private_data *)pdata->private_data;

	result = inv_serial_read(mlsl_handle, pdata->address,
				 MPUREG_PWR_MGMT_1, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	if (reg & BIT_SLEEP) {
		result = inv_serial_single_write(mlsl_handle, pdata->address,
					MPUREG_PWR_MGMT_1, reg & ~BIT_SLEEP);
		if (result) {
			LOG_RESULT_LOCATION(result);
			return result;
		}
	}
	msleep(2);

	result = inv_serial_read(mlsl_handle, pdata->address,
			MPUREG_PWR_MGMT_2, 1, &reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg &= ~(BIT_STBY_XA | BIT_STBY_YA | BIT_STBY_ZA);
	result = inv_serial_single_write(mlsl_handle, pdata->address,
				       MPUREG_PWR_MGMT_2, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}

	/* settings */

	result = mpu6050_set_fsr(mlsl_handle, pdata, &private_data->resume,
				 true, private_data->resume.fsr);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_odr(mlsl_handle, pdata, &private_data->resume,
				 true, private_data->resume.odr);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	result = mpu6050_set_irq(mlsl_handle, pdata, &private_data->resume,
				 true, private_data->resume.irq_type);

	/* motion, no_motion */
	/* TODO : port these in their respective _set_thrs and _set_dur
		  functions and use the APPLY paremeter to apply just like
		  _set_odr, _set_irq, and _set_fsr. */
	reg = (unsigned char)private_data->suspend.ths / ACCEL_MOT_THR_LSB;
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 MPUREG_ACCEL_MOT_THR, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg = (unsigned char)
	    ACCEL_ZRMOT_THR_LSB_CONVERSION(private_data->resume.ths);
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 MPUREG_ACCEL_ZRMOT_THR, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg = (unsigned char)private_data->suspend.ths / ACCEL_MOT_DUR_LSB;
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 MPUREG_ACCEL_MOT_DUR, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	reg = (unsigned char)private_data->resume.ths / ACCEL_ZRMOT_DUR_LSB;
	result = inv_serial_single_write(mlsl_handle, pdata->address,
					 MPUREG_ACCEL_ZRMOT_DUR, reg);
	if (result) {
		LOG_RESULT_LOCATION(result);
		return result;
	}
	return 0;
}

static int mpu6050_read(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			unsigned char *data)
{
	int result;
	result = inv_serial_read(mlsl_handle, pdata->address,
				 slave->read_reg, slave->read_len, data);
	return result;
}

static int mpu6050_config(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata,
			 struct ext_slave_config *data)
{
	struct mpu6050_private_data *private_data =
		(struct mpu6050_private_data *)pdata->private_data;
	if (!data->data)
		return INV_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		return mpu6050_set_odr(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		return mpu6050_set_odr(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		return mpu6050_set_fsr(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		return mpu6050_set_fsr(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_MOT_THS:
		return mpu6050_set_ths(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_NMOT_THS:
		return mpu6050_set_ths(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_MOT_DUR:
		return mpu6050_set_dur(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_NMOT_DUR:
		return mpu6050_set_dur(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
		return mpu6050_set_irq(mlsl_handle, pdata,
				      &private_data->suspend,
				      data->apply, *((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
		return mpu6050_set_irq(mlsl_handle, pdata,
				      &private_data->resume,
				      data->apply, *((long *)data->data));
	case MPU_SLAVE_CONFIG_INTERNAL_REFERENCE:
		return mpu6050_set_mldl_cfg_ref(mlsl_handle, pdata,
					       (struct mldl_cfg *)data->data);
		break;

	default:
		return INV_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return 0;
}

static int mpu6050_get_config(void *mlsl_handle,
			     struct ext_slave_descr *slave,
			     struct ext_slave_platform_data *pdata,
			     struct ext_slave_config *data)
{
	struct mpu6050_private_data *private_data =
		(struct mpu6050_private_data *)pdata->private_data;
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

	return 0;
}

static struct ext_slave_descr mpu6050_descr = {
	.init             = mpu6050_init,
	.exit             = mpu6050_exit,
	.suspend          = mpu6050_suspend,
	.resume           = mpu6050_resume,
	.read             = mpu6050_read,
	.config           = mpu6050_config,
	.get_config       = mpu6050_get_config,
	.name             = "mpu6050",
	.type             = EXT_SLAVE_TYPE_ACCEL,
	.id               = ACCEL_ID_MPU6050,
	.read_reg         = 0x3B,
	.read_len         = 6,
	.endian           = EXT_SLAVE_BIG_ENDIAN,
	.range            = {2, 0},
	.trigger          = NULL,
};

struct ext_slave_descr *mpu6050_get_slave_descr(void)
{
	return &mpu6050_descr;
}

/**
 *  @}
 */
