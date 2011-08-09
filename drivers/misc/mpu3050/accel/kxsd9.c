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
 *      @file   kxsd9.c
 *      @brief  Accelerometer setup and handling methods.
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/kernel.h>
#include <linux/module.h>
#endif

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

static int kxsd9_suspend(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	/* CTRL_REGB: low-power standby mode */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x0d, 0x0);
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register and mask */
#define ACCEL_KIONIX_CTRL_REG      (0x0C)
#define ACCEL_KIONIX_CTRL_MASK     (0x3)

static int kxsd9_resume(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	/* Full Scale */
	reg = 0x0;
	reg &= ~ACCEL_KIONIX_CTRL_MASK;
	reg |= 0x00;
	if (slave->range.mantissa == 4) {	/* 4g scale = 4.9951 */
		reg |= 0x2;
		slave->range.fraction = 9951;
	} else if (slave->range.mantissa == 7) { /* 6g scale = 7.5018 */
		reg |= 0x1;
		slave->range.fraction = 5018;
	} else if (slave->range.mantissa == 9) {	/* 8g scale = 9.9902 */
		reg |= 0x0;
		slave->range.fraction = 9902;
	} else {
		slave->range.mantissa = 2; /* 2g scale = 2.5006 */
		slave->range.fraction = 5006;
		reg |= 0x3;
	}
	reg |= 0xC0;		/* 100Hz LPF */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_KIONIX_CTRL_REG, reg);
	ERROR_CHECK(result);
	/* normal operation */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x0d, 0x40);
	ERROR_CHECK(result);

	return ML_SUCCESS;
}

static int kxsd9_read(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata,
		      unsigned char *data)
{
	int result;
	result = MLSLSerialRead(mlsl_handle, pdata->address,
				slave->reg, slave->len, data);
	return result;
}

static struct ext_slave_descr kxsd9_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ kxsd9_suspend,
	/*.resume           = */ kxsd9_resume,
	/*.read             = */ kxsd9_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "kxsd9",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_KXSD9,
	/*.reg              = */ 0x00,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {2, 5006},
};

struct ext_slave_descr *kxsd9_get_slave_descr(void)
{
	return &kxsd9_descr;
}
EXPORT_SYMBOL(kxsd9_get_slave_descr);

/**
 *  @}
**/
