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
 *  @brief      Provides the interface to setup and handle a compass
 *              connected to the primary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   lsm303m.c
 *      @brief  Magnetometer setup and handling methods for ST LSM303.
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
#define MPL_LOG_TAG "MPL-compass"

/*----- ST LSM303 Registers ------*/
enum LSM_REG {
	LSM_REG_CONF_A = 0x0,
	LSM_REG_CONF_B = 0x1,
	LSM_REG_MODE = 0x2,
	LSM_REG_X_M = 0x3,
	LSM_REG_X_L = 0x4,
	LSM_REG_Z_M = 0x5,
	LSM_REG_Z_L = 0x6,
	LSM_REG_Y_M = 0x7,
	LSM_REG_Y_L = 0x8,
	LSM_REG_STATUS = 0x9,
	LSM_REG_ID_A = 0xA,
	LSM_REG_ID_B = 0xB,
	LSM_REG_ID_C = 0xC
};

enum LSM_CONF_A {
	LSM_CONF_A_DRATE_MASK = 0x1C,
	LSM_CONF_A_DRATE_0_75 = 0x00,
	LSM_CONF_A_DRATE_1_5 = 0x04,
	LSM_CONF_A_DRATE_3 = 0x08,
	LSM_CONF_A_DRATE_7_5 = 0x0C,
	LSM_CONF_A_DRATE_15 = 0x10,
	LSM_CONF_A_DRATE_30 = 0x14,
	LSM_CONF_A_DRATE_75 = 0x18,
	LSM_CONF_A_MEAS_MASK = 0x3,
	LSM_CONF_A_MEAS_NORM = 0x0,
	LSM_CONF_A_MEAS_POS = 0x1,
	LSM_CONF_A_MEAS_NEG = 0x2
};

enum LSM_CONF_B {
	LSM_CONF_B_GAIN_MASK = 0xE0,
	LSM_CONF_B_GAIN_0_9 = 0x00,
	LSM_CONF_B_GAIN_1_2 = 0x20,
	LSM_CONF_B_GAIN_1_9 = 0x40,
	LSM_CONF_B_GAIN_2_5 = 0x60,
	LSM_CONF_B_GAIN_4_0 = 0x80,
	LSM_CONF_B_GAIN_4_6 = 0xA0,
	LSM_CONF_B_GAIN_5_5 = 0xC0,
	LSM_CONF_B_GAIN_7_9 = 0xE0
};

enum LSM_MODE {
	LSM_MODE_MASK = 0x3,
	LSM_MODE_CONT = 0x0,
	LSM_MODE_SINGLE = 0x1,
	LSM_MODE_IDLE = 0x2,
	LSM_MODE_SLEEP = 0x3
};

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int lsm303dlhm_suspend(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  LSM_REG_MODE, LSM_MODE_SLEEP);
	ERROR_CHECK(result);
	MLOSSleep(3);

	return result;
}

int lsm303dlhm_resume(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	/* Use single measurement mode. Start at sleep state. */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  LSM_REG_MODE, LSM_MODE_SLEEP);
	ERROR_CHECK(result);
	/* Config normal measurement */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  LSM_REG_CONF_A, 0);
	ERROR_CHECK(result);
	/* Adjust gain to 320 LSB/Gauss */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  LSM_REG_CONF_B, LSM_CONF_B_GAIN_5_5);
	ERROR_CHECK(result);

	return result;
}

int lsm303dlhm_read(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata,
		    unsigned char *data)
{
	unsigned char stat;
	tMLError result = ML_SUCCESS;
	short axisFixed;

	/* Read status reg. to check if data is ready */
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, LSM_REG_STATUS, 1,
			   &stat);
	ERROR_CHECK(result);
	if (stat & 0x01) {
		result =
		    MLSLSerialRead(mlsl_handle, pdata->address,
				   LSM_REG_X_M, 6, (unsigned char *) data);
		ERROR_CHECK(result);

		/*drop data if overflows */
		if ((data[0] == 0xf0) || (data[2] == 0xf0)
		    || (data[4] == 0xf0)) {
			/* trigger next measurement read */
			result =
				MLSLSerialWriteSingle(mlsl_handle,
							pdata->address,
							LSM_REG_MODE,
							LSM_MODE_SINGLE);
			ERROR_CHECK(result);
			return ML_ERROR_COMPASS_DATA_OVERFLOW;
		}
		/* convert to fixed point and apply sensitivity correction for
		   Z-axis */
		axisFixed =
		    (short) ((unsigned short) data[5] +
			     (unsigned short) data[4] * 256);
		/* scale up by 1.125 (36/32) approximate of 1.122 (320/285) */
		axisFixed = (short) (axisFixed * 36);
		data[4] = axisFixed >> 8;
		data[5] = axisFixed & 0xFF;

		axisFixed =
		    (short) ((unsigned short) data[3] +
			     (unsigned short) data[2] * 256);
		axisFixed = (short) (axisFixed * 32);
		data[2] = axisFixed >> 8;
		data[3] = axisFixed & 0xFF;

		axisFixed =
		    (short) ((unsigned short) data[1] +
			     (unsigned short) data[0] * 256);
		axisFixed = (short) (axisFixed * 32);
		data[0] = axisFixed >> 8;
		data[1] = axisFixed & 0xFF;

		/* trigger next measurement read */
		result =
		    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					  LSM_REG_MODE, LSM_MODE_SINGLE);
		ERROR_CHECK(result);

		return ML_SUCCESS;
	} else {
		/* trigger next measurement read */
		result =
		    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					  LSM_REG_MODE, LSM_MODE_SINGLE);
		ERROR_CHECK(result);

		return ML_ERROR_COMPASS_DATA_NOT_READY;
	}
}

struct ext_slave_descr lsm303dlhm_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ lsm303dlhm_suspend,
	/*.resume           = */ lsm303dlhm_resume,
	/*.read             = */ lsm303dlhm_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "lsm303dlhm",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_LSM303,
	/*.reg              = */ 0x06,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {10240, 0},
};

struct ext_slave_descr *lsm303dlhm_get_slave_descr(void)
{
	return &lsm303dlhm_descr;
}
EXPORT_SYMBOL(lsm303dlhm_get_slave_descr);

/**
 *  @}
**/
