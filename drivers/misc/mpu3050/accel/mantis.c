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
 *      @file   lis331.c
 *      @brief  Accelerometer setup and handling methods for ST LIS331
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

int mantis_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	return ML_SUCCESS;
}

/* full scale setting - register & mask */
#define ACCEL_ST_CTRL_REG          (0x23)
#define ACCEL_ST_CTRL_MASK         (0x30)

int mantis_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
#ifdef M_HW
	unsigned char reg;

	if (slave->range.mantissa == 2)
		reg = 0;
	else if (slave->range.mantissa == 4)
		reg = 1 << 3;
	else if (slave->range.mantissa == 8)
		reg = 2 << 3;
	else if (slave->range.mantissa == 16)
		reg = 3 << 3;
	else
		return ML_ERROR;

	result = MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				       MPUREG_ACCEL_CONFIG, reg);
#endif
	return result;
}

int mantis_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

struct ext_slave_descr mantis_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ mantis_suspend,
	/*.resume           = */ mantis_resume,
	/*.read             = */ mantis_read,
	/*.config           = */ NULL,
	/*.name             = */ "mantis",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ACCEL_ID_MPU6000,
	/*.reg              = */ 0xA8,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_BIG_ENDIAN,
	/*.range            = */ {2, 0},
};

struct ext_slave_descr *mantis_get_slave_descr(void)
{
	return &mantis_descr;
}
EXPORT_SYMBOL(mantis_get_slave_descr);

/**
 *  @}
 */

