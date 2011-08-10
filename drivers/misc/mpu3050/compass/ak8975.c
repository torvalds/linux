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
 *  @defgroup   COMPASSDL (Motion Library - Accelerometer Driver Layer)
 *  @brief      Provides the interface to setup and handle an accelerometers
 *              connected to the secondary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   AK8975.c
 *      @brief  Magnetometer setup and handling methods for AKM 8975 compass.
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#include <string.h>

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-compass"


#define AK8975_REG_ST1  (0x02)
#define AK8975_REG_HXL  (0x03)
#define AK8975_REG_ST2  (0x09)

#define AK8975_REG_CNTL (0x0A)

#define AK8975_CNTL_MODE_POWER_DOWN         (0x00)
#define AK8975_CNTL_MODE_SINGLE_MEASUREMENT (0x01)

int ak8975_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AK8975_REG_CNTL,
				  AK8975_CNTL_MODE_POWER_DOWN);
	MLOSSleep(1);		/* wait at least 100us */
	ERROR_CHECK(result);
	return result;
}

int ak8975_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AK8975_REG_CNTL,
				  AK8975_CNTL_MODE_SINGLE_MEASUREMENT);
	ERROR_CHECK(result);
	return result;
}

int ak8975_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	unsigned char regs[8];
	unsigned char *stat = &regs[0];
	unsigned char *stat2 = &regs[7];
	int result = ML_SUCCESS;
	int status = ML_SUCCESS;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, AK8975_REG_ST1,
			   8, regs);
	ERROR_CHECK(result);

	/*
	 * ST : data ready -
	 * Measurement has been completed and data is ready to be read.
	 */
	if (*stat & 0x01) {
		memcpy(data, &regs[1], 6);
		status = ML_SUCCESS;
	}

	/*
	 * ST2 : data error -
	 * occurs when data read is started outside of a readable period;
	 * data read would not be correct.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour but we
	 * stil account for it and return an error, since the data would be
	 * corrupted.
	 * DERR bit is self-clearing when ST2 register is read.
	 */
	if (*stat2 & 0x04)
		status = ML_ERROR_COMPASS_DATA_ERROR;
	/*
	 * ST2 : overflow -
	 * the sum of the absolute values of all axis |X|+|Y|+|Z| < 2400uT.
	 * This is likely to happen in presence of an external magnetic
	 * disturbance; it indicates, the sensor data is incorrect and should
	 * be ignored.
	 * An error is returned.
	 * HOFL bit clears when a new measurement starts.
	 */
	if (*stat2 & 0x08)
		status = ML_ERROR_COMPASS_DATA_OVERFLOW;
	/*
	 * ST : overrun -
	 * the previous sample was not fetched and lost.
	 * Valid in continuous measurement mode only.
	 * In single measurement mode this error should not occour and we
	 * don't consider this condition an error.
	 * DOR bit is self-clearing when ST2 or any meas. data register is
	 * read.
	 */
	if (*stat & 0x02) {
		/* status = ML_ERROR_COMPASS_DATA_UNDERFLOW; */
		status = ML_SUCCESS;
	}

	/*
	 * trigger next measurement if:
	 *    - stat is non zero;
	 *    - if stat is zero and stat2 is non zero.
	 * Won't trigger if data is not ready and there was no error.
	 */
	if (*stat != 0x00 || *stat2 != 0x00) {
		result =
		    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					  AK8975_REG_CNTL,
					  AK8975_CNTL_MODE_SINGLE_MEASUREMENT);
		ERROR_CHECK(result);
	}

	return status;
}

static int ak8975_config(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata,
			struct ext_slave_config *data)
{
	int result;
	if (!data->data)
		return ML_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_WRITE_REGISTERS:
		result = MLSLSerialWrite(mlsl_handle, pdata->address,
					data->len,
					(unsigned char *)data->data);
		ERROR_CHECK(result);
		break;
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
	case MPU_SLAVE_CONFIG_ODR_RESUME:
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
	case MPU_SLAVE_CONFIG_FSR_RESUME:
	case MPU_SLAVE_CONFIG_MOT_THS:
	case MPU_SLAVE_CONFIG_NMOT_THS:
	case MPU_SLAVE_CONFIG_MOT_DUR:
	case MPU_SLAVE_CONFIG_NMOT_DUR:
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return ML_SUCCESS;
}

static int ak8975_get_config(void *mlsl_handle,
				struct ext_slave_descr *slave,
				struct ext_slave_platform_data *pdata,
				struct ext_slave_config *data)
{
	int result;
	if (!data->data)
		return ML_ERROR_INVALID_PARAMETER;

	switch (data->key) {
	case MPU_SLAVE_READ_REGISTERS:
	{
		unsigned char *serial_data = (unsigned char *)data->data;
		result = MLSLSerialRead(mlsl_handle, pdata->address,
					serial_data[0],
					data->len - 1,
					&serial_data[1]);
		ERROR_CHECK(result);
		break;
	}
	case MPU_SLAVE_CONFIG_ODR_SUSPEND:
	case MPU_SLAVE_CONFIG_ODR_RESUME:
	case MPU_SLAVE_CONFIG_FSR_SUSPEND:
	case MPU_SLAVE_CONFIG_FSR_RESUME:
	case MPU_SLAVE_CONFIG_MOT_THS:
	case MPU_SLAVE_CONFIG_NMOT_THS:
	case MPU_SLAVE_CONFIG_MOT_DUR:
	case MPU_SLAVE_CONFIG_NMOT_DUR:
	case MPU_SLAVE_CONFIG_IRQ_SUSPEND:
	case MPU_SLAVE_CONFIG_IRQ_RESUME:
	default:
		return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
	};

	return ML_SUCCESS;
}

struct ext_slave_descr ak8975_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ ak8975_suspend,
	/*.resume           = */ ak8975_resume,
	/*.read             = */ ak8975_read,
	/*.config           = */ ak8975_config,
	/*.get_config       = */ ak8975_get_config,
	/*.name             = */ "ak8975",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_AKM,
	/*.reg              = */ 0x01,
	/*.len              = */ 9,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {9830, 4000}
};

struct ext_slave_descr *ak8975_get_slave_descr(void)
{
	return &ak8975_descr;
}
EXPORT_SYMBOL(ak8975_get_slave_descr);

/**
 *  @}
 */
