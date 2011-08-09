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
 *      @file   bma150.c
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

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*********************************************
    Accelerometer Initialization Functions
**********************************************/

static int bma150_suspend(void *mlsl_handle,
			  struct ext_slave_descr *slave,
			  struct ext_slave_platform_data *pdata)
{
	int result;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x0a, 0x01);
	MLOSSleep(3); /* 3 ms powerup time maximum */
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register and mask */
#define ACCEL_BOSCH_CTRL_REG       (0x14)
#define ACCEL_BOSCH_CTRL_MASK      (0x18)

static int bma150_resume(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg = 0;

	/* Soft reset */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x0a, 0x02);
	ERROR_CHECK(result);
	MLOSSleep(3);

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, 0x14, 1, &reg);
	ERROR_CHECK(result);

	/* Bandwidth */
	reg &= 0xc0;
	reg |= 3;		/* 3=190 Hz */

	/* Full Scale */
	reg &= ~ACCEL_BOSCH_CTRL_MASK;
	if (slave->range.mantissa == 4)
		reg |= 0x08;
	else if (slave->range.mantissa == 8)
		reg |= 0x10;
	else {
		slave->range.mantissa = 2;
		reg |= 0x00;
	}
	slave->range.fraction = 0;

	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x14, reg);
	ERROR_CHECK(result);

	return result;
}

static int bma150_read(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata,
		       unsigned char *data)
{
	int result;
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, slave->len, data);
	return result;
}

static struct ext_slave_descr bma150_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ bma150_suspend,
	/*.resume           = */ bma150_resume,
	/*.read             = */ bma150_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "bma150",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_BMA150,
	/*.reg              = */ 0x02,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *bma150_get_slave_descr(void)
{
	return &bma150_descr;
}
EXPORT_SYMBOL(bma150_get_slave_descr);

#ifdef __KERNEL__
MODULE_AUTHOR("Invensense");
MODULE_DESCRIPTION("User space IRQ handler for MPU3xxx devices");
MODULE_LICENSE("GPL");
MODULE_ALIAS("bma");
#endif

/**
 *  @}
 */
