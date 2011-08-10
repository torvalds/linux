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
 *      @file   mmc314x.c
 *      @brief  Magnetometer setup and handling methods for ???? compass.
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

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

static int reset_int = 1000;
static int read_count = 1;
static char reset_mode; /* in Z-init section */

#define MMC314X_REG_ST (0x00)
#define MMC314X_REG_X_MSB (0x01)

#define MMC314X_CNTL_MODE_WAKE_UP (0x01)
#define MMC314X_CNTL_MODE_SET (0x02)
#define MMC314X_CNTL_MODE_RESET (0x04)

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int mmc314x_suspend(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	return result;
}

int mmc314x_resume(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{

	int result;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  MMC314X_REG_ST, MMC314X_CNTL_MODE_RESET);
	ERROR_CHECK(result);
	MLOSSleep(10);
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  MMC314X_REG_ST, MMC314X_CNTL_MODE_SET);
	ERROR_CHECK(result);
	MLOSSleep(10);
	read_count = 1;
	return ML_SUCCESS;
}

int mmc314x_read(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		 struct ext_slave_platform_data *pdata,
		 unsigned char *data)
{
	int result, ii;
	short tmp[3];
	unsigned char tmpdata[6];


	if (read_count > 1000)
		read_count = 1;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, MMC314X_REG_X_MSB,
			   6, (unsigned char *) data);
	ERROR_CHECK(result);

	for (ii = 0; ii < 6; ii++)
		tmpdata[ii] = data[ii];

	for (ii = 0; ii < 3; ii++) {
		tmp[ii] =
		    (short) ((tmpdata[2 * ii] << 8) + tmpdata[2 * ii + 1]);
		tmp[ii] = tmp[ii] - 4096;
		tmp[ii] = tmp[ii] * 16;
	}

	for (ii = 0; ii < 3; ii++) {
		data[2 * ii] = (unsigned char) (tmp[ii] >> 8);
		data[2 * ii + 1] = (unsigned char) (tmp[ii]);
	}

	if (read_count % reset_int == 0) {
		if (reset_mode) {
			result =
			    MLSLSerialWriteSingle(mlsl_handle,
						  pdata->address,
						  MMC314X_REG_ST,
						  MMC314X_CNTL_MODE_RESET);
			ERROR_CHECK(result);
			reset_mode = 0;
			return ML_ERROR_COMPASS_DATA_NOT_READY;
		} else {
			result =
			    MLSLSerialWriteSingle(mlsl_handle,
						  pdata->address,
						  MMC314X_REG_ST,
						  MMC314X_CNTL_MODE_SET);
			ERROR_CHECK(result);
			reset_mode = 1;
			read_count++;
			return ML_ERROR_COMPASS_DATA_NOT_READY;
		}
	}
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  MMC314X_REG_ST,
				  MMC314X_CNTL_MODE_WAKE_UP);
	ERROR_CHECK(result);
	read_count++;

	return ML_SUCCESS;
}

struct ext_slave_descr mmc314x_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ mmc314x_suspend,
	/*.resume           = */ mmc314x_resume,
	/*.read             = */ mmc314x_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "mmc314x",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_MMC314X,
	/*.reg              = */ 0x01,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {400, 0},
};

struct ext_slave_descr *mmc314x_get_slave_descr(void)
{
	return &mmc314x_descr;
}
EXPORT_SYMBOL(mmc314x_get_slave_descr);

/**
 *  @}
**/
