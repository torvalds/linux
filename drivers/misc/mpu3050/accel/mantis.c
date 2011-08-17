/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.

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
 *  @defgroup   ACCELDL (Motion Library - Accelerometer Driver Layer)
 *  @brief      Provides the interface to setup and handle an accelerometers
 *              connected to the secondary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   lis331.c
 *      @brief  Accelerometer setup and handling methods for Invensense MANTIS
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

struct mantis_config {
	unsigned int odr; /* output data rate 1/1000 Hz*/
	unsigned int fsr; /* full scale range mg */
	unsigned int ths; /* Motion no-motion thseshold mg */
	unsigned int dur; /* Motion no-motion duration ms */
};

struct mantis_private_data {
	struct mantis_config suspend;
	struct mantis_config resume;
};


/*****************************************
 *Accelerometer Initialization Functions
 *****************************************/
/**
 * Record the odr for use in computing duration values.
 *
 * @param config Config to set, suspend or resume structure
 * @param odr output data rate in 1/1000 hz
 */
void mantis_set_odr(struct mantis_config *config,
		long odr)
{
	config->odr = odr;
}

void mantis_set_ths(struct mantis_config *config,
		long ths)
{
	if (ths < 0)
		ths = 0;

	config->ths = ths;
	MPL_LOGV("THS: %d\n", config->ths);
}

void mantis_set_dur(struct mantis_config *config,
		long dur)
{
	if (dur < 0)
		dur = 0;

	config->dur = dur;
	MPL_LOGV("DUR: %d\n", config->dur);
}

static void mantis_set_fsr(
	struct mantis_config *config,
	long fsr)
{
	if (fsr <= 2000)
		config->fsr = 2000;
	else if (fsr <= 4000)
		config->fsr = 4000;
	else
		config->fsr = 8000;

	MPL_LOGV("FSR: %d\n", config->fsr);
}

static int mantis_init(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	struct mantis_private_data *private_data;
	private_data = (struct mantis_private_data *)
		MLOSMalloc(sizeof(struct mantis_private_data));

	if (!private_data)
		return ML_ERROR_MEMORY_EXAUSTED;

	pdata->private_data = private_data;

	mantis_set_odr(&private_data->suspend, 0);
	mantis_set_odr(&private_data->resume, 200000);
	mantis_set_fsr(&private_data->suspend, 2000);
	mantis_set_fsr(&private_data->resume, 2000);
	mantis_set_ths(&private_data->suspend, 80);
	mantis_set_ths(&private_data->resume, 40);
	mantis_set_dur(&private_data->suspend, 1000);
	mantis_set_dur(&private_data->resume,  2540);
	return ML_SUCCESS;
}

static int mantis_exit(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	if (pdata->private_data)
		return MLOSFree(pdata->private_data);
	else
		return ML_SUCCESS;
}

int mantis_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	unsigned char reg;
	int result;

	result = MLSLSerialRead(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_2, 1, &reg);
	ERROR_CHECK(result);
	reg |= (BIT_STBY_XA | BIT_STBY_YA | BIT_STBY_ZA);

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_2, reg);
	ERROR_CHECK(result);

	return ML_SUCCESS;
}

int mantis_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;
	struct mantis_private_data *private_data;

	private_data = (struct mantis_private_data *) pdata->private_data;

	MLSLSerialRead(mlsl_handle, pdata->address,
		MPUREG_PWR_MGMT_2, 1, &reg);

	reg &= ~(BIT_STBY_XA | BIT_STBY_YA | BIT_STBY_ZA);

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				MPUREG_PWR_MGMT_2, reg);
	ERROR_CHECK(result);

	if (slave->range.mantissa == 2)
		reg = 0;
	else if (slave->range.mantissa == 4)
		reg = 1 << 3;
	else if (slave->range.mantissa == 8)
		reg = 2 << 3;
	else if (slave->range.mantissa == 16)
		reg = 3 << 3;
	else
		return ML_ERROR;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       MPUREG_ACCEL_CONFIG, reg);
	ERROR_CHECK(result);

	reg = (unsigned char) private_data->suspend.ths / ACCEL_MOT_THR_LSB;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				MPUREG_ACCEL_MOT_THR, reg);
	ERROR_CHECK(result);

	reg = (unsigned char)
		ACCEL_ZRMOT_THR_LSB_CONVERSION(private_data->resume.ths);
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				MPUREG_ACCEL_ZRMOT_THR, reg);
	ERROR_CHECK(result);

	reg = (unsigned char) private_data->suspend.ths / ACCEL_MOT_DUR_LSB;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				MPUREG_ACCEL_MOT_DUR, reg);
	ERROR_CHECK(result);

	reg = (unsigned char) private_data->resume.ths / ACCEL_ZRMOT_DUR_LSB;
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				MPUREG_ACCEL_ZRMOT_DUR, reg);
	ERROR_CHECK(result);
	return result;
}

int mantis_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	int result;
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, slave->len, data);
	return result;
}

static int mantis_config(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_config *data)
{
	struct mantis_private_data *private_data = pdata->private_data;
	if (!data->data)
		return ML_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
		mantis_set_odr(&private_data->suspend,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_ODR_RESUME:
		mantis_set_odr(&private_data->resume,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
		mantis_set_fsr(&private_data->suspend,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_FSR_RESUME:
		mantis_set_fsr(&private_data->resume,
				*((long *)data->data));
		break;
	case MPU_SLAVE_CONFIG_MOT_THS:
		mantis_set_ths(&private_data->suspend,
			       (*((long *)data->data)));
		break;
	case MPU_SLAVE_CONFIG_NMOT_THS:
		mantis_set_ths(&private_data->resume,
			       (*((long *)data->data)));
		break;
	case MPU_SLAVE_CONFIG_MOT_DUR:
		mantis_set_dur(&private_data->suspend,
			       (*((long *)data->data)));
		break;
	case MPU_SLAVE_CONFIG_NMOT_DUR:
		mantis_set_dur(&private_data->resume,
			       (*((long *)data->data)));
		break;
	default:
		return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return ML_SUCCESS;
}

struct ext_slave_descr mantis_descr = {
	/*.init             = */ mantis_init,
	/*.exit             = */ mantis_exit,
	/*.suspend          = */ mantis_suspend,
	/*.resume           = */ mantis_resume,
	/*.read             = */ mantis_read,
	/*.config           = */ mantis_config,
	/*.get_config       = */ NULL,
	/*.name             = */ "mantis",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_MPU6000,
	/*.reg              = */ 0xA8,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *mantis_get_slave_descr(void)
{
	return &mantis_descr;
}
EXPORT_SYMBOL(mantis_get_slave_descr);

/**
 *  @}
 */

