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
 *      @file   hscdtd002b.c
 *      @brief  Magnetometer setup and handling methods for Alps hscdtd002b
 *              compass.
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

/*----- ALPS HSCDTD002B Registers ------*/
#define COMPASS_HSCDTD002B_STAT          (0x18)
#define COMPASS_HSCDTD002B_CTRL1         (0x1B)
#define COMPASS_HSCDTD002B_CTRL2         (0x1C)
#define COMPASS_HSCDTD002B_CTRL3         (0x1D)
#define COMPASS_HSCDTD002B_DATAX         (0x10)

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Compass Initialization Functions
*****************************************/

int hscdtd002b_suspend(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	/* Power mode: stand-by */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD002B_CTRL1, 0x00);
	ERROR_CHECK(result);
	MLOSSleep(1);		/* turn-off time */

	return result;
}

int hscdtd002b_resume(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	/* Soft reset */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD002B_CTRL3, 0x80);
	ERROR_CHECK(result);
	/* Force state; Power mode: active */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD002B_CTRL1, 0x82);
	ERROR_CHECK(result);
	/* Data ready enable */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD002B_CTRL2, 0x08);
	ERROR_CHECK(result);
	MLOSSleep(1);		/* turn-on time */

	return result;
}

int hscdtd002b_read(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata,
		    unsigned char *data)
{
	unsigned char stat;
	tMLError result = ML_SUCCESS;
	int status = ML_SUCCESS;

	/* Read status reg. to check if data is ready */
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   COMPASS_HSCDTD002B_STAT, 1, &stat);
	ERROR_CHECK(result);
	if (stat & 0x40) {
		result =
		    MLSLSerialRead(mlsl_handle, pdata->address,
				   COMPASS_HSCDTD002B_DATAX, 6,
				   (unsigned char *) data);
		ERROR_CHECK(result);
		status = ML_SUCCESS;
	} else if (stat & 0x20) {
		status = ML_ERROR_COMPASS_DATA_OVERFLOW;
	} else {
		status = ML_ERROR_COMPASS_DATA_NOT_READY;
	}
	/* trigger next measurement read */
	result =
		MLSLSerialWriteSingle(mlsl_handle, pdata->address,
			  COMPASS_HSCDTD002B_CTRL3, 0x40);
	ERROR_CHECK(result);

	return status;
}

struct ext_slave_descr hscdtd002b_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ hscdtd002b_suspend,
	/*.resume           = */ hscdtd002b_resume,
	/*.read             = */ hscdtd002b_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "hscdtd002b",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_HSCDTD002B,
	/*.reg              = */ 0x10,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {9830, 4000},
};

struct ext_slave_descr *hscdtd002b_get_slave_descr(void)
{
	return &hscdtd002b_descr;
}
EXPORT_SYMBOL(hscdtd002b_get_slave_descr);

/**
 *  @}
**/
