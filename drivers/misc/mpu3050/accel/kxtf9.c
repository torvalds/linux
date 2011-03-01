/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 $
 */

/**
 *  @defgroup   ACCELDL (Motion Library - Accelerometer Driver Layer)
 *  @brief      Provides the interface to setup and handle an accelerometers
 *              connected to the secondary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   kxtf9.c
 *      @brief  Accelerometer setup and handling methods.
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

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

static int kxtf9_suspend(void *mlsl_handle,
			 struct ext_slave_descr *slave,
			 struct ext_slave_platform_data *pdata)
{
	int result;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x1b, 0);
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register and mask */
#define ACCEL_KIONIX_CTRL_REG      (0x1b)
#define ACCEL_KIONIX_CTRL_MASK     (0x18)

static int kxtf9_resume(void *mlsl_handle,
			struct ext_slave_descr *slave,
			struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	/* RAM reset */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x1d, 0xcd);
	ERROR_CHECK(result);
	MLOSSleep(10);
	/* Wake up */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x1b, 0x42);
	ERROR_CHECK(result);
	/* INT_CTRL_REG1: */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x1e, 0x14);
	ERROR_CHECK(result);
	/* WUF_THRESH: */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x5a, 0x00);
	ERROR_CHECK(result);
	/* DATA_CTRL_REG */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x21, 0x04);
	ERROR_CHECK(result);
	/* WUF_TIMER */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x29, 0x02);
	ERROR_CHECK(result);

	/* Full Scale */
	reg = 0xc2;
	reg &= ~ACCEL_KIONIX_CTRL_MASK;
	reg |= 0x00;
	if (slave->range.mantissa == 4)
		reg |= 0x08;
	else if (slave->range.mantissa == 8)
		reg |= 0x10;
	else {
		slave->range.mantissa = 2;
		reg |= 0x00;
	}
	slave->range.fraction = 0;

	/* Normal operation  */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x1b, reg);
	ERROR_CHECK(result);
	MLOSSleep(50);

	return ML_SUCCESS;
}

static int kxtf9_read(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata,
		      unsigned char *data)
{
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

static struct ext_slave_descr kxtf9_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ kxtf9_suspend,
	/*.resume           = */ kxtf9_resume,
	/*.read             = */ kxtf9_read,
	/*.config           = */ NULL,
	/*.name             = */ "kxtf9",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_KXTF9,
	/*.reg              = */ 0x06,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *kxtf9_get_slave_descr(void)
{
	return &kxtf9_descr;
}
EXPORT_SYMBOL(kxtf9_get_slave_descr);

/**
 *  @}
**/
