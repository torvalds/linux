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
 *      @file   adxl346.c
 *      @brief  Accelerometer setup and handling methods for AD adxl346.
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

#define ACCEL_ADI346_SLEEP_REG      (0x2D)
#define ACCEL_ADI346_SLEEP_MASK     (0x04)

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int adxl346_suspend(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_ADI346_SLEEP_REG, 1, &reg);
	ERROR_CHECK(result);
	reg |= ACCEL_ADI346_SLEEP_MASK;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_ADI346_SLEEP_REG, reg);
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register & mask */
#define ACCEL_ADI346_CTRL_REG      (0x31)
#define ACCEL_ADI346_CTRL_MASK     (0x03)

int adxl346_resume(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_ADI346_SLEEP_REG, 1, &reg);
	ERROR_CHECK(result);
	reg &= ~ACCEL_ADI346_SLEEP_MASK;
	/*wake up if sleeping */
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       ACCEL_ADI346_SLEEP_REG, reg);
	ERROR_CHECK(result);
	/*MLOSSleep(10) */

	/* Full Scale */
	reg = 0x04;
	reg &= ~ACCEL_ADI346_CTRL_MASK;
	if (slave->range.mantissa == 4)
		reg |= 0x1;
	else if (slave->range.mantissa == 8)
		reg |= 0x2;
	else if (slave->range.mantissa == 16)
		reg |= 0x3;
	else {
		slave->range.mantissa = 2;
		reg |= 0x0;
	}
	slave->range.fraction = 0;

	/* DATA_FORMAT: full resolution of +/-2g; data is left justified */
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x31, reg);
	ERROR_CHECK(result);
	/* BW_RATE: normal power operation with output data rate of 200Hz */
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x2C, 0x0B);
	ERROR_CHECK(result);
	/* POWER_CTL: power on in measurement mode */
	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x2D, 0x28);
	ERROR_CHECK(result);
	/*--- after wake up, it takes at least [1/(data rate) + 1.1]ms ==>
	  6.1ms to get valid sensor data ---*/
	MLOSSleep(10);

	return result;
}

int adxl346_read(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		 struct ext_slave_platform_data *pdata,
		 unsigned char *data)
{
	int result;
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, slave->len, data);
	return result;
}

struct ext_slave_descr adxl346_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ adxl346_suspend,
	/*.resume           = */ adxl346_resume,
	/*.read             = */ adxl346_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "adx1346",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_ADI346,
	/*.reg              = */ 0x32,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *adxl346_get_slave_descr(void)
{
	return &adxl346_descr;
}
EXPORT_SYMBOL(adxl346_get_slave_descr);

/**
 *  @}
**/
