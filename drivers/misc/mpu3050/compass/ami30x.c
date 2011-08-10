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
 *     @file   ami30x.c
 *     @brief  Magnetometer setup and handling methods for Aichi AMI304/AMI305
 *             compass.
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

#define AMI30X_REG_DATAX (0x10)
#define AMI30X_REG_STAT1 (0x18)
#define AMI30X_REG_CNTL1 (0x1B)
#define AMI30X_REG_CNTL2 (0x1C)
#define AMI30X_REG_CNTL3 (0x1D)

#define AMI30X_BIT_CNTL1_PC1  (0x80)
#define AMI30X_BIT_CNTL1_ODR1 (0x10)
#define AMI30X_BIT_CNTL1_FS1  (0x02)

#define AMI30X_BIT_CNTL2_IEN  (0x10)
#define AMI30X_BIT_CNTL2_DREN (0x08)
#define AMI30X_BIT_CNTL2_DRP  (0x04)
#define AMI30X_BIT_CNTL3_F0RCE (0x40)

int ami30x_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, AMI30X_REG_CNTL1,
			   1, &reg);
	ERROR_CHECK(result);

	reg &= ~(AMI30X_BIT_CNTL1_PC1|AMI30X_BIT_CNTL1_FS1);
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI30X_REG_CNTL1, reg);
	ERROR_CHECK(result);

	return result;
}

int ami30x_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	/* Set CNTL1 reg to power model active */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI30X_REG_CNTL1,
				  AMI30X_BIT_CNTL1_PC1|AMI30X_BIT_CNTL1_FS1);
	ERROR_CHECK(result);
	/* Set CNTL2 reg to DRDY active high and enabled */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI30X_REG_CNTL2,
				  AMI30X_BIT_CNTL2_DREN |
				  AMI30X_BIT_CNTL2_DRP);
	ERROR_CHECK(result);
	/* Set CNTL3 reg to forced measurement period */
	result =
		MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI30X_REG_CNTL3, AMI30X_BIT_CNTL3_F0RCE);

	return result;
}

int ami30x_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	unsigned char stat;
	int result = ML_SUCCESS;

	/* Read status reg and check if data ready (DRDY) */
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, AMI30X_REG_STAT1,
			   1, &stat);
	ERROR_CHECK(result);

	if (stat & 0x40) {
		result =
		    MLSLSerialRead(mlsl_handle, pdata->address,
				   AMI30X_REG_DATAX, 6,
				   (unsigned char *) data);
		ERROR_CHECK(result);
		/* start another measurement */
		result =
			MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					      AMI30X_REG_CNTL3,
					      AMI30X_BIT_CNTL3_F0RCE);
		ERROR_CHECK(result);

		return ML_SUCCESS;
	}

	return ML_ERROR_COMPASS_DATA_NOT_READY;
}

struct ext_slave_descr ami30x_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ ami30x_suspend,
	/*.resume           = */ ami30x_resume,
	/*.read             = */ ami30x_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "ami30x",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_AMI30X,
	/*.reg              = */ 0x06,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {5461, 3333}
	/* For AMI305,the range field needs to be modified to {9830.4f}*/
};

struct ext_slave_descr *ami30x_get_slave_descr(void)
{
	return &ami30x_descr;
}
EXPORT_SYMBOL(ami30x_get_slave_descr);

/**
 *  @}
**/
