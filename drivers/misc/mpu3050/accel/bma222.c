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

/*
 *  @defgroup   ACCELDL (Motion Library - Accelerometer Driver Layer)
 *  @brief      Provides the interface to setup and handle an accelerometers
 *              connected to the secondary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   bma222.c
 *      @brief  Accelerometer setup and handling methods.
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include "mpu.h"
#include "mlos.h"
#include "mlsl.h"

#define ACCEL_BMA222_RANGE_REG          (0x0F)
#define ACCEL_BMA222_BW_REG             (0x10)
#define ACCEL_BMA222_SUSPEND_REG        (0x11)
#define ACCEL_BMA222_SFT_RST_REG        (0x14)

/*********************************************
    Accelerometer Initialization Functions
**********************************************/

static int bma222_suspend(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result;

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_BMA222_SUSPEND_REG, 0x80);
	ERROR_CHECK(result);

	return result;
}

static int bma222_resume(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg = 0;

	/* Soft reset */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_BMA222_SFT_RST_REG, 0xB6);
	ERROR_CHECK(result);
	MLOSSleep(10);

	/*Bandwidth */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_BMA222_BW_REG, 0x0C);
	ERROR_CHECK(result);

	/* Full Scale */
	if (slave->range.mantissa == 4)
		reg |= 0x05;
	else if (slave->range.mantissa == 8)
		reg |= 0x08;
	else if (slave->range.mantissa == 16)
		reg |= 0x0C;
	else {
		slave->range.mantissa = 2;
		reg |= 0x03;
	}
	slave->range.fraction = 0;

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_BMA222_RANGE_REG, reg);
	ERROR_CHECK(result);

	return result;
}

static int bma222_read(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       unsigned char *data)
{
	int result;
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, slave->len, data);
	return result;
}

static struct ext_slave_descr bma222_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ bma222_suspend,
	/*.resume           = */ bma222_resume,
	/*.read             = */ bma222_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "bma222",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_BMA222,
	/*.reg              = */ 0x02,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *bma222_get_slave_descr(void)
{
	return &bma222_descr;
}
EXPORT_SYMBOL(bma222_get_slave_descr);

/*
 *  @}
 */
