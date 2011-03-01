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
 *      @file   mma8450.c
 *      @brief  Accelerometer setup and handling methods for Freescale MMA8450
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

#define ACCEL_MMA8450_SLEEP_REG      (0x38)
#define ACCEL_MMA8450_SLEEP_MASK     (0x3)


/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int mma8450_suspend(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_MMA8450_SLEEP_REG, 1, &reg);
	ERROR_CHECK(result);
	reg &= ~ACCEL_MMA8450_SLEEP_MASK;
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_MMA8450_SLEEP_REG, reg);
	ERROR_CHECK(result);
	return result;
}

/* full scale setting - register & mask */
#define ACCEL_MMA8450_CTRL_REG      (0x38)
#define ACCEL_MMA8450_CTRL_MASK     (0x3)

int mma8450_resume(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char reg;

	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   ACCEL_MMA8450_CTRL_REG, 1, &reg);
	ERROR_CHECK(result);

	/* data rate = 200Hz */
	reg &= 0xE3;
	reg |= 0x4;

	/* Full Scale */
	reg &= ~ACCEL_MMA8450_CTRL_MASK;
	if (slave->range.mantissa == 4)
		reg |= 0x2;
	else if (slave->range.mantissa == 8)
		reg |= 0x3;
	else {
		slave->range.mantissa = 2;
		reg |= 0x1;
	}
	slave->range.fraction = 0;

	/* XYZ_DATA_CFG: event flag enabled on all axis */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x16, 0x05);
	ERROR_CHECK(result);
	/* CTRL_REG1: rate + scale config + wakeup */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  ACCEL_MMA8450_CTRL_REG, reg);
	ERROR_CHECK(result);

	return result;
}

int mma8450_read(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		 struct ext_slave_platform_data *pdata,
		 unsigned char *data)
{
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

struct ext_slave_descr mma8450_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ mma8450_suspend,
	/*.resume           = */ mma8450_resume,
	/*.read             = */ mma8450_read,
	/*.config           = */ NULL,
	/*.name             = */ "mma8450",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_MMA8450,
	/*.reg              = */ 0x00,
	/*.len              = */ 3,
	/*.endian           = */ EXT_SLAVE_FS8_BIG_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *mma8450_get_slave_descr(void)
{
	return &mma8450_descr;
}
EXPORT_SYMBOL(mma8450_get_slave_descr);

/**
 *  @}
**/
