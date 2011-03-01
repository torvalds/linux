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
 *      @file   cma3000.c
 *      @brief  Accelerometer setup and handling methods for VTI CMA3000
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
#include "accel.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-acc"

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Accelerometer Initialization Functions
*****************************************/

int cma3000_suspend(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata)
{
	int result;
	/* RAM reset */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address, 0x1d, 0xcd);
	return result;
}

int cma3000_resume(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;



	return ML_SUCCESS;
}

int cma3000_read(void *mlsl_handle,
		 struct ext_slave_descr *slave,
		 struct ext_slave_platform_data *pdata,
		 unsigned char *data)
{
	return ML_ERROR_FEATURE_NOT_IMPLEMENTED;
}

struct ext_slave_descr cma3000_descr = {
	/*.suspend          = */ cma3000_suspend,
	/*.resume           = */ cma3000_resume,
	/*.read             = */ cma3000_read,
	/*.name             = */ "cma3000",
	/*.type             = */ EXT_SLAVE_TYPE_ACCELEROMETER,
	/*.id               = */ ID_INVALID,
	/* fixme - id to added when support becomes available */
	/*.reg              = */ 0x06,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ 65536,
};

struct ext_slave_descr *cma3000_get_slave_descr(void)
{
	return &cma3000_descr;
}
EXPORT_SYMBOL(cma3000_get_slave_descr);

/**
 *  @}
**/
