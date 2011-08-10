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
 *      @file   mma8450.c
 *      @brief  Accelerometer setup and handling methods for Freescale MMA8450
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#include <stdlib.h>
#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"
#include <string.h>

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

#define ACCEL_MMA8450_SLEEP_REG      (0x38)
#define ACCEL_MMA8450_SLEEP_MASK     (0x3)


/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int mma8450_suspend(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_MMA8450_SLEEP_REG, 1, &reg);
	ERROR_CHECK(result);
	reg &= ~ACCEL_MMA8450_SLEEP_MASK;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_MMA8450_SLEEP_REG, reg);
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register & mask */
#define ACCEL_MMA8450_CTRL_REG      (0x38)
#define ACCEL_MMA8450_CTRL_MASK     (0x3)

int mma8450_resume(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_MMA8450_CTRL_REG, 1, &reg);
	ERROR_CHECK(result);

	/* data rate = 200Hz */
	reg &= 0xE3;
	reg |= 0x4;

	/* Full Scale */
	reg &= ~ACCEL_MMA8450_CTRL_MASK;
	if (slave->range.mantissa == 4)
		reg |= 0x2;
	else if (slave->range.mantissa == 8)
		reg |= 0x3;
	else {
		slave->range.mantissa = 2;
		reg |= 0x1;
	}
	slave->range.fraction = 0;

	/* XYZ_DATA_CFG: event flag enabled on all axis */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  0x16, 0x05);
	ERROR_CHECK(result);
	/* CTRL_REG1: rate + scale config + wakeup */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_MMA8450_CTRL_REG, reg);
	ERROR_CHECK(result);

	return result;
}

int mma8450_read(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		 struct ext_slave_platform_data *pdata,
		 unsigned char *data)
{
	int result;
	unsigned char local_data[4]; /* Status register + 3 bytes data */
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, sizeof(local_data), local_data);
    ERROR_CHECK(result);
    memcpy(data, &local_data[1], (slave->len)-1);
    return result;
}

struct ext_slave_descr mma8450_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ mma8450_suspend,
	/*.resume           = */ mma8450_resume,
	/*.read             = */ mma8450_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "mma8450",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_MMA8450,
	/*.reg              = */ 0x00,
	/*.len              = */ 4,
	/*.endian           = */ EXT_SLAVE_FS8_BIG_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *mma8450_get_slave_descr(void)
{
	return &mma8450_descr;
}
EXPORT_SYMBOL(mma8450_get_slave_descr);

/**
 *  @}
**/
