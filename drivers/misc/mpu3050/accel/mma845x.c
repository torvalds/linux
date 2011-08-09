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
 *      @file   mma845x.c
 *      @brief  Accelerometer setup and handling methods for Freescale MMA845X
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include <stdlib.h>
#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"
#include <string.h>

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

#define ACCEL_MMA845X_CTRL_REG1      (0x2A)
#define ACCEL_MMA845X_SLEEP_MASK     (0x01)

/* full scale setting - register & mask */
#define ACCEL_MMA845X_CFG_REG       (0x0E)
#define ACCEL_MMA845X_CTRL_MASK     (0x03)

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int mma845x_suspend(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_MMA845X_CTRL_REG1, 1, &reg);
	ERROR_CHECK(result);
	reg &= ~ACCEL_MMA845X_SLEEP_MASK;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_MMA845X_CTRL_REG1, reg);
	ERROR_CHECK(result);
	return result;
}


int mma845x_resume(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	result = MLSLSerialRead(mlsl_handle, pdata->address,
				ACCEL_MMA845X_CFG_REG, 1, &reg);
	ERROR_CHECK(result);

	/* data rate = 200Hz */

	/* Full Scale */
	reg &= ~ACCEL_MMA845X_CTRL_MASK;
	if (slave->range.mantissa == 4)
		reg |= 0x1;
	else if (slave->range.mantissa == 8)
		reg |= 0x2;
	else {
		slave->range.mantissa = 2;
		reg |= 0x0;
	}
	slave->range.fraction = 0;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				ACCEL_MMA845X_CFG_REG, reg);
	ERROR_CHECK(result);
	/* 200Hz + active mode */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
			    ACCEL_MMA845X_CTRL_REG1, 0x11);
	ERROR_CHECK(result);

	return result;
}

int mma845x_read(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		 struct ext_slave_platform_data *pdata,
		 unsigned char *data)
{
	int result;
	unsigned char local_data[7]; /* Status register + 6 bytes data */
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, sizeof(local_data), local_data);
	ERROR_CHECK(result);
	memcpy(data, &local_data[1], slave->len);
	return result;
}

struct ext_slave_descr mma845x_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ mma845x_suspend,
	/*.resume           = */ mma845x_resume,
	/*.read             = */ mma845x_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "mma845x",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_MMA845X,
	/*.reg              = */ 0x00,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_FS16_BIG_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *mma845x_get_slave_descr(void)
{
	return &mma845x_descr;
}
EXPORT_SYMBOL(mma845x_get_slave_descr);

/**
 *  @}
 */
