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
 *      @file   lsm303a.c
 *      @brief  Accelerometer setup and handling methods for ST LSM303
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

#define ACCEL_ST_SLEEP_REG          (0x20)
#define ACCEL_ST_SLEEP_MASK         (0x20)

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int lsm303dlha_suspend(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, ACCEL_ST_SLEEP_REG,
			   1, &reg);
	ERROR_CHECK(result);
	reg &= ~(0x27);
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_ST_SLEEP_REG, reg);
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register & mask */
#define ACCEL_ST_CTRL_REG          (0x23)
#define ACCEL_ST_CTRL_MASK         (0x30)

int lsm303dlha_resume(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, ACCEL_ST_SLEEP_REG,
			   1, &reg);
	ERROR_CHECK(result);
	reg |= 0x27;
	/*wake up if sleeping */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_ST_SLEEP_REG, reg);
	ERROR_CHECK(result);

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x20, 0x37);
	ERROR_CHECK(result);
	MLOSSleep(500);

	reg = 0x40;

	/* Full Scale */
	reg &= ~ACCEL_ST_CTRL_MASK;
	if (slave->range.mantissa == 4) {
		slave->range.fraction = 960;
		reg |= 0x10;
	} else if (slave->range.mantissa == 8) {
		slave->range.fraction = 1920;
		reg |= 0x30;
	} else {
		slave->range.mantissa = 2;
		slave->range.fraction = 480;
		reg |= 0x00;
	}

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x23, reg);
	ERROR_CHECK(result);

	/* Configure high pass filter */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x21, 0x0F);
	ERROR_CHECK(result);
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x32, 0x00);
	ERROR_CHECK(result);
	/* Configure INT1_DURATION */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x33, 0x7F);
	ERROR_CHECK(result);
	/* Configure INT1_CFG */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x30, 0x95);
	ERROR_CHECK(result);
	MLOSSleep(50);
	return result;
}

int lsm303dlha_read(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata,
		    unsigned char *data)
{
	int result;
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, slave->len, data);
	return result;
}

struct ext_slave_descr lsm303dlha_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ lsm303dlha_suspend,
	/*.resume           = */ lsm303dlha_resume,
	/*.read             = */ lsm303dlha_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "lsm303dlha",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_LSM303,
	/*.reg              = */ (0x28 | 0x80), /* 0x80 for burst reads */
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {2, 480},
};

struct ext_slave_descr *lsm303dlha_get_slave_descr(void)
{
	return &lsm303dlha_descr;
}
EXPORT_SYMBOL(lsm303dlha_get_slave_descr);

/**
 *  @}
**/
