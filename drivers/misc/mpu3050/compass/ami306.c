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
 *     @file   ami306.c
 *     @brief  Magnetometer setup and handling methods for Aichi AMI306
 *             compass.
 */

/* ------------------ */
/* - Include Files. - */
/* ------------------ */

#ifdef __KERNEL__
#include <linux/module.h>
#endif

#include <delay.h>

#include "mpu.h"
#include "mlsl.h"
#include "mlos.h"

#include <log.h>
#undef MPL_LOG_TAG
#define MPL_LOG_TAG "MPL-compass"

#define AMI306_REG_DATAX (0x10)
#define AMI306_REG_STAT1 (0x18)
#define AMI306_REG_CNTL1 (0x1B)
#define AMI306_REG_CNTL2 (0x1C)
#define AMI306_REG_CNTL3 (0x1D)
#define AMI306_REG_CNTL4_1 (0x5C)
#define AMI306_REG_CNTL4_2 (0x5D)

#define AMI306_BIT_CNTL1_PC1  (0x80)
#define AMI306_BIT_CNTL1_ODR1 (0x10)
#define AMI306_BIT_CNTL1_FS1  (0x02)

#define AMI306_BIT_CNTL2_IEN  (0x10)
#define AMI306_BIT_CNTL2_DREN (0x08)
#define AMI306_BIT_CNTL2_DRP  (0x04)
#define AMI306_BIT_CNTL3_F0RCE (0x40)

int ami306_suspend(void *mlsl_handle,
		   struct ext_slave_descr *slave,
		   struct ext_slave_platform_data *pdata)
{
	int result;
	unsigned char reg;
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, AMI306_REG_CNTL1,
			   1, &reg);
	ERROR_CHECK(result);

	reg &= ~(AMI306_BIT_CNTL1_PC1|AMI306_BIT_CNTL1_FS1);
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI306_REG_CNTL1, reg);
	ERROR_CHECK(result);

	return result;
}

int ami306_resume(void *mlsl_handle,
		  struct ext_slave_descr *slave,
		  struct ext_slave_platform_data *pdata)
{
	int result = ML_SUCCESS;
	unsigned char regs[] = {
		 AMI306_REG_CNTL4_1,
		 0x7E,
		 0xA0
	};
    /* Step1. Set CNTL1 reg to power model active (Write CNTL1:PC1=1) */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI306_REG_CNTL1,
				  AMI306_BIT_CNTL1_PC1|AMI306_BIT_CNTL1_FS1);
	ERROR_CHECK(result);

    /* Step2. Set CNTL2 reg to DRDY active high and enabled
       (Write CNTL2:DREN=1) */
	result =
	    MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI306_REG_CNTL2,
				  AMI306_BIT_CNTL2_DREN |
				  AMI306_BIT_CNTL2_DRP);
	ERROR_CHECK(result);

    /* Step3. Set CNTL4 reg to for measurement speed (Write CNTL4, 0xA07E) */
	result =
	    MLSLSerialWrite(mlsl_handle, pdata->address, DIM(regs), regs);
	ERROR_CHECK(result);

    /* Step4. skipped */

    /* Step5. Set CNTL3 reg to forced measurement period
       (Write CNTL3:FORCE=1) */
	result =
		MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				  AMI306_REG_CNTL3, AMI306_BIT_CNTL3_F0RCE);

	return result;
}

int ami306_read(void *mlsl_handle,
		struct ext_slave_descr *slave,
		struct ext_slave_platform_data *pdata, unsigned char *data)
{
	unsigned char stat;
	int result = ML_SUCCESS;
	int status = ML_SUCCESS;


	/* Measurement(x,y,z) */
	result =
		MLSLSerialWriteSingle(mlsl_handle, pdata->address,
				      AMI306_REG_CNTL3,
				      AMI306_BIT_CNTL3_F0RCE);
	ERROR_CHECK(result);
	udelay(500);

    /* Step6. Read status reg and check if data ready (DRDY) */
	result =
	    MLSLSerialRead(mlsl_handle, pdata->address, AMI306_REG_STAT1,
			   1, &stat);
	ERROR_CHECK(result);

    /* Step6. Does DRDY output the rising edge? */
	if (stat & 0x40) {
		result =
		    MLSLSerialRead(mlsl_handle, pdata->address,
				   AMI306_REG_DATAX, 6,
				   (unsigned char *) data);
		ERROR_CHECK(result);
		status = ML_SUCCESS;
	} else if (stat & 0x20)
		status = ML_ERROR_COMPASS_DATA_OVERFLOW;

	return status;
}

struct ext_slave_descr ami306_descr = {
	/*.init             = */ NULL,
	/*.exit             = */ NULL,
	/*.suspend          = */ ami306_suspend,
	/*.resume           = */ ami306_resume,
	/*.read             = */ ami306_read,
	/*.config           = */ NULL,
	/*.get_config       = */ NULL,
	/*.name             = */ "ami306",
	/*.type             = */ EXT_SLAVE_TYPE_COMPASS,
	/*.id               = */ COMPASS_ID_AMI306,
	/*.reg              = */ 0x10,
	/*.len              = */ 6,
	/*.endian           = */ EXT_SLAVE_LITTLE_ENDIAN,
	/*.range            = */ {5461, 3333}
	/* For AMI305,the range field needs to be modified to {9830.4f}*/
};

struct ext_slave_descr *ami306_get_slave_descr(void)
{
	return &ami306_descr;
}
EXPORT_SYMBOL(ami306_get_slave_descr);

/**
 *  @}
 */
