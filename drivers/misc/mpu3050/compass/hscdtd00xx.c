/*
 $License:
    Copyright (C) 2010 InvenSense Corporation, All Rights Reserved.
 $
 */

/**
 *  @brief      Provides the interface to setup and handle a compass
 *              connected to the primary I2C interface of the gyroscope.
 *
 *  @{
 *      @file   hscdtd00xx.c
 *      @brief  Magnetometer setup and handling methods for Alps hscdtd00xx
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

/*----- ALPS HSCDTD00XX Registers ------*/
#define COMPASS_HSCDTD00XX_STAT          (0x18)
#define COMPASS_HSCDTD00XX_CTRL1         (0x1B)
#define COMPASS_HSCDTD00XX_CTRL2         (0x1C)
#define COMPASS_HSCDTD00XX_CTRL3         (0x1D)
#define COMPASS_HSCDTD00XX_DATAX         (0x10)

/* --------------------- */
/* -    Variables.     - */
/* --------------------- */

/*****************************************
    Compass Initialization Functions
*****************************************/

int hscdtd00xx_suspend(void *mlsl_handle,
		       struct ext_slave_descr *slave,
		       struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	/* Power mode: stand-by */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD00XX_CTRL1, 0x00);
	ERROR_CHECK(result);
	MLOSSleep(1);		/* turn-off time */

	return result;
}

int hscdtd00xx_resume(void *mlsl_handle,
		      struct ext_slave_descr *slave,
		      struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;

	/* Soft reset */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD00XX_CTRL3, 0x80);
	ERROR_CHECK(result);
	/* Force state; Power mode: active */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD00XX_CTRL1, 0x82);
	ERROR_CHECK(result);
	/* Data ready enable */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  COMPASS_HSCDTD00XX_CTRL2, 0x08);
	ERROR_CHECK(result);
	MLOSSleep(1);		/* turn-on time */

	return result;
}

int hscdtd00xx_read(void *mlsl_handle,
		    struct ext_slave_descr *slave,
		    struct ext_slave_platform_data *pdata,
		    unsigned char *data)
{
	unsigned char stat;
	tMLError result = ML_SUCCESS;

	/* Read status reg. to check if data is ready */
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address,
			   COMPASS_HSCDTD00XX_STAT, 1, &stat);
	ERROR_CHECK(result);
	if (stat & 0x40) {
		result =
		    MLSLSerialRead(mlsl_handle, pdata->address,
				   COMPASS_HSCDTD00XX_DATAX, 6,
				   (unsigned char *) data);
		ERROR_CHECK(result);

		/* trigger next measurement read */
		result =
		    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					  COMPASS_HSCDTD00XX_CTRL3, 0x40);
		ERROR_CHECK(result);

		return ML_SUCCESS;
	} else if (stat & 0x20) {
		/* trigger next measurement read */
		result =
		    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					  COMPASS_HSCDTD00XX_CTRL3, 0x40);
		ERROR_CHECK(result);
		return ML_ERROR_COMPASS_DATA_OVERFLOW;
	} else {
		/* trigger next measurement read */
		result =
		    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
					  COMPASS_HSCDTD00XX_CTRL3, 0x40);
		ERROR_CHECK(result);
		return ML_ERROR_COMPASS_DATA_NOT_READY;
	}
}

struct ext_slave_descr hscdtd00xx_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ hscdtd00xx_suspend,
	/*.resume           = */ hscdtd00xx_resume,
	/*.read             = */ hscdtd00xx_read,
	/*.config           = */ NULL,
	/*.name             = */ "hscdtd00xx",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_HSCDTD00XX,
	/*.reg              = */ 0x10,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {9830, 4000},
};

struct ext_slave_descr *hscdtd00xx_get_slave_descr(void)
{
	return &hscdtd00xx_descr;
}
EXPORT_SYMBOL(hscdtd00xx_get_slave_descr);

/**
 *  @}
**/
